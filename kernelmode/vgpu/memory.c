/*
 * MVisor vgpu Device guest driver
 * Copyright (C) 2022 cair <rui.cai@tenclass.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "memory.h"

typedef struct _VGPU_MEMORY {
    BOOLEAN                 bInitialize;
    PUINT8                  VirtualAddress;
    PHYSICAL_ADDRESS        PhysicalAddress;
    SIZE_T                  TotalMemorySize;
    SIZE_T                  AvailableMemorySize;
    LIST_ENTRY              UsedList;
    ULONG64                 UsedRegionBegin;
    ULONG64                 UsedRegionEnd;
    KSPIN_LOCK              SpinLock;
    LOOKASIDE_LIST_EX       LookAsideList;
}VGPU_MEMORY, * PVGPU_MEMORY;

typedef struct _MEMORY_NODE {
    ULONG64     Begin;
    ULONG64     End;
    SIZE_T      Size;
    LIST_ENTRY  Entry;
}MEMORY_NODE, * PMEMORY_NODE;

typedef enum _NODE_POSITION {
    LEFT,
    MIDDLE,
    RIGHT
}NODE_POSITION;

static VGPU_MEMORY VgpuMemory;

VOID InitializeVgpuMemory(PVOID VitrualAddress, PHYSICAL_ADDRESS PhysicalAddress, SIZE_T Size)
{
    if (VgpuMemory.bInitialize)
    {
        return;
    }

    VgpuMemory.VirtualAddress = VitrualAddress;
    VgpuMemory.PhysicalAddress = PhysicalAddress;
    VgpuMemory.TotalMemorySize = Size;
    VgpuMemory.AvailableMemorySize = Size;
    VgpuMemory.UsedRegionBegin = 0;
    VgpuMemory.UsedRegionEnd = 0;
    VgpuMemory.bInitialize = TRUE;

    ExInitializeLookasideListEx(
        &VgpuMemory.LookAsideList,
        NULL,
        NULL,
        NonPagedPool,
        0,
        sizeof(MEMORY_NODE),
        VIRTIO_VGPU_MEMORY_TAG,
        0
    );
    InitializeListHead(&VgpuMemory.UsedList);
    KeInitializeSpinLock(&VgpuMemory.SpinLock);

    VGPU_DEBUG_LOG("init vgpu memory va=%p gpa=0x%llx size=0x%llx", VitrualAddress, PhysicalAddress.QuadPart, Size);
}

VOID UninitializeVgpuMemory()
{
    KIRQL           savedIrql;
    PLIST_ENTRY     item;
    PMEMORY_NODE    node;

    if (!VgpuMemory.bInitialize)
    {
        return;
    }

    SpinLock(&savedIrql, &VgpuMemory.SpinLock);
    while (!IsListEmpty(&VgpuMemory.UsedList)) {
        item = RemoveHeadList(&VgpuMemory.UsedList);
        node = CONTAINING_RECORD(item, MEMORY_NODE, Entry);
        ExFreeToLookasideListEx(&VgpuMemory.LookAsideList, node);
    }
    SpinUnLock(savedIrql, &VgpuMemory.SpinLock);

    ExDeleteLookasideListEx(&VgpuMemory.LookAsideList);
    RtlZeroMemory(&VgpuMemory, sizeof(VGPU_MEMORY));
}

VOID FreeVgpuMemory(PVOID VitrualAddress)
{
    KIRQL           savedIrql;
    PLIST_ENTRY     item;
    ULONG64         offset;
    PMEMORY_NODE    tmp;
    PMEMORY_NODE    node = NULL;
    BOOLEAN         bFind = FALSE;

    if (!VgpuMemory.bInitialize)
    {
        return;
    }
    offset = (PUINT8)VitrualAddress - VgpuMemory.VirtualAddress;

    // start processing
    SpinLock(&savedIrql, &VgpuMemory.SpinLock);

    // find the target node by offset
    for (item = VgpuMemory.UsedList.Flink; item != &VgpuMemory.UsedList; item = item->Flink)
    {
        node = CONTAINING_RECORD(item, MEMORY_NODE, Entry);
        if (node->Begin == offset)
        {
            bFind = TRUE;
            break;
        }
    }

    if (!bFind)
    {
        SpinUnLock(savedIrql, &VgpuMemory.SpinLock);
        VGPU_DEBUG_PRINT("WRONG: can't find the node to free");
        return;
    }

    // if the node was the first or last one in the list, update the used region
    if (node->Entry.Blink == &VgpuMemory.UsedList && node->Entry.Flink != &VgpuMemory.UsedList)
    {
        tmp = CONTAINING_RECORD(node->Entry.Flink, MEMORY_NODE, Entry);
        VgpuMemory.UsedRegionBegin = tmp->Begin;
    }
    else if (node->Entry.Flink == &VgpuMemory.UsedList && node->Entry.Blink != &VgpuMemory.UsedList)
    {
        tmp = CONTAINING_RECORD(node->Entry.Blink, MEMORY_NODE, Entry);
        VgpuMemory.UsedRegionEnd = tmp->End;
    }

    // remove the target node
    if (RemoveEntryListUnsafe(&node->Entry))
    {
        // if list was empty, reset used region
        VgpuMemory.UsedRegionBegin = 0;
        VgpuMemory.UsedRegionEnd = 0;
    }

    // update available size
    VgpuMemory.AvailableMemorySize += node->Size;

    // end processing
    SpinUnLock(savedIrql, &VgpuMemory.SpinLock);

    // release target node
    ExFreeToLookasideListEx(&VgpuMemory.LookAsideList, node);
    VGPU_DEBUG_LOG("VGPU available memory size=0x%llx", VgpuMemory.AvailableMemorySize);
}


BOOLEAN AllocateVgpuMemory(SIZE_T Size, PMEMORY_DESCRIPTOR Memory)
{
    KIRQL           savedIrql;
    PMEMORY_NODE    tmp;
    PMEMORY_NODE    node;
    ULONG64         offset;
    NODE_POSITION   position;
    PLIST_ENTRY     item = NULL;

    if (!VgpuMemory.bInitialize)
    {
        return FALSE;
    }

    if (Size > VgpuMemory.AvailableMemorySize)
    {
        VGPU_DEBUG_LOG("WRONG: we can't find enough memory, available=0x%llx need=0x%llx", VgpuMemory.AvailableMemorySize, Size);
        return FALSE;
    }

    node = ExAllocateFromLookasideListEx(&VgpuMemory.LookAsideList);
    if (!node)
    {
        VGPU_DEBUG_PRINT("WRONG: allocate node memory failed");
        return FALSE;
    }
    node->Size = Size;

    // start processing
    SpinLock(&savedIrql, &VgpuMemory.SpinLock);
    
    if (VgpuMemory.UsedRegionBegin >= Size)
    {
        position = LEFT;
        offset = VgpuMemory.UsedRegionBegin - Size;
    }
    else if (VgpuMemory.TotalMemorySize - VgpuMemory.UsedRegionEnd >= Size)
    {
        position = RIGHT;
        offset = VgpuMemory.UsedRegionEnd;
    }
    else
    {
        position = MIDDLE;
        offset = 0;

        // the unused region can't satisfy the require size directly, so we scan the used list to find unused hole region
        for (item = VgpuMemory.UsedList.Flink; item != &VgpuMemory.UsedList; item = item->Flink)
        {
            tmp = CONTAINING_RECORD(item, MEMORY_NODE, Entry);

            // if overlaped with current used node, continue
            if (offset < tmp->End && offset + Size > tmp->Begin)
            {
                offset = tmp->End;
                continue;
            }

            // get the unused hole region, jump out
            break;
        }

        if (item == &VgpuMemory.UsedList)
        {
            SpinUnLock(savedIrql, &VgpuMemory.SpinLock);
            VGPU_DEBUG_PRINT("WRONG: we can't locate the suitable region, never reach here");
            return FALSE;
        }
    }

    // now we get the suitable offset for target region
    node->Begin = offset;
    node->End = offset + Size;

    // insert the node into the used list
    switch (position)
    {
    case LEFT:
        InsertHeadList(&VgpuMemory.UsedList, &node->Entry);
        VgpuMemory.UsedRegionBegin = node->Begin;
        break;
    case MIDDLE:
        ASSERT(item != NULL);
        // get the right region between used nodes
        node->Entry.Blink = item->Blink;
        node->Entry.Flink = item;

        // repair the list Entry
        item->Blink->Flink = &node->Entry;
        item->Blink = &node->Entry;
        break;
    case RIGHT:
        InsertTailList(&VgpuMemory.UsedList, &node->Entry);
        VgpuMemory.UsedRegionEnd = node->End;
        break;
    }

    // update available size
    VgpuMemory.AvailableMemorySize -= Size;

    // finish processing
    SpinUnLock(savedIrql, &VgpuMemory.SpinLock);

    // return the suitable memory region
    Memory->PhysicalAddress.QuadPart = VgpuMemory.PhysicalAddress.QuadPart + offset;
    Memory->VirtualAddress = VgpuMemory.VirtualAddress + offset;

    VGPU_DEBUG_LOG("position=%d UsedRegionBegin=0x%llx UsedRegionEnd=0x%llx", position, VgpuMemory.UsedRegionBegin, VgpuMemory.UsedRegionEnd);
    return TRUE;
}

VOID ReallocVgpuMemory(PVOID VitrualAddress, SIZE_T Size)
{
    KIRQL           savedIrql;
    PLIST_ENTRY     item;
    ULONG64         offset;
    PMEMORY_NODE    node = NULL;
    BOOLEAN         bFind = FALSE;
    SIZE_T          change;

    if (!VgpuMemory.bInitialize)
    {
        return;
    }
    offset = (PUINT8)VitrualAddress - VgpuMemory.VirtualAddress;

    // start processing
    SpinLock(&savedIrql, &VgpuMemory.SpinLock);

    // find the target node by offset
    for (item = VgpuMemory.UsedList.Flink; item != &VgpuMemory.UsedList; item = item->Flink)
    {
        node = CONTAINING_RECORD(item, MEMORY_NODE, Entry);
        if (node->Begin == offset)
        {
            bFind = TRUE;
            break;
        }
    }

    if (!bFind)
    {
        VGPU_DEBUG_PRINT("WRONG: can't find the node to free");
        goto end;
    }

    if (node->Size == Size)
    {
        goto end;
    }
    else if (node->Size < Size)
    {
        VGPU_DEBUG_PRINT("WRONG: expanding current node was not implemented");
        goto end;
    }
    else
    {
        change = node->Size - Size;
        node->End -= change;
        node->Size -= change;

        // if last node, update the used region
        if (node->Entry.Flink == &VgpuMemory.UsedList)
        {
            VgpuMemory.UsedRegionEnd = node->End;
        }

        // update available size
        VgpuMemory.AvailableMemorySize += change;
    }

 end:
    // end processing
    SpinUnLock(savedIrql, &VgpuMemory.SpinLock);
}