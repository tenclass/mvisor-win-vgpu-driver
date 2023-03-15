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


#define BITMAP_FAILED 0xFFFFFFFF

typedef struct _VGPU_MEMORY {
    BOOLEAN             bInitialize;
    PUINT8              VirtualAddress;
    PHYSICAL_ADDRESS    PhysicalAddress;
    SIZE_T              AvailableMemorySize;
    KSPIN_LOCK          SpinLock;
    RTL_BITMAP          RegionBitmap;
    PVOID               RegionBitmapBuffer;
    ULONG               LastFreeIndex;
}VGPU_MEMORY, * PVGPU_MEMORY;

static VGPU_MEMORY VgpuMemory = { 0 };

VOID InitializeVgpuMemory(PVOID VitrualAddress, PHYSICAL_ADDRESS PhysicalAddress, SIZE_T Size)
{
    ASSERT(!VgpuMemory.bInitialize);

    // 32K Align
    ASSERT(Size % (PAGE_SIZE * 8) == 0);

    VgpuMemory.RegionBitmapBuffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, Size / PAGE_SIZE / 8, VIRTIO_VGPU_MEMORY_TAG);
    if (!VgpuMemory.RegionBitmapBuffer)
    {
        VGPU_DEBUG_PRINT("WRONG: allocate bitmap buffer failed");
        return;
    }

    KeInitializeSpinLock(&VgpuMemory.SpinLock);
    RtlInitializeBitMap(&VgpuMemory.RegionBitmap, VgpuMemory.RegionBitmapBuffer, (ULONG)(Size / PAGE_SIZE));
    RtlClearAllBits(&VgpuMemory.RegionBitmap);

    VgpuMemory.VirtualAddress = VitrualAddress;
    VgpuMemory.PhysicalAddress = PhysicalAddress;
    VgpuMemory.AvailableMemorySize = Size;
    VgpuMemory.bInitialize = TRUE;
    VgpuMemory.LastFreeIndex = 0;

    VGPU_DEBUG_LOG("init vgpu memory va=%p gpa=0x%llx size=0x%llx", VitrualAddress, PhysicalAddress.QuadPart, Size);
}

VOID UninitializeVgpuMemory()
{
    ASSERT(VgpuMemory.bInitialize);

    ExFreePoolWithTag(VgpuMemory.RegionBitmapBuffer, VIRTIO_VGPU_MEMORY_TAG);
    RtlZeroMemory(&VgpuMemory, sizeof(VGPU_MEMORY));
}

BOOLEAN AllocateVgpuMemory(SIZE_T Size, PMEMORY_DESCRIPTOR Memory)
{
    KIRQL   savedIrql;
    ULONG   index;
    ULONG   page;
    ULONG64 offset;

    ASSERT(VgpuMemory.bInitialize);

    if (Size > VgpuMemory.AvailableMemorySize)
    {
        VGPU_DEBUG_LOG("WRONG: we can't find enough memory, available=0x%llx need=0x%llx", VgpuMemory.AvailableMemorySize, Size);
        return FALSE;
    }

    page = (ULONG)(Size / PAGE_SIZE);

    // start processing
    SpinLock(&savedIrql, &VgpuMemory.SpinLock);

    index = RtlFindClearBitsAndSet(&VgpuMemory.RegionBitmap, page, VgpuMemory.LastFreeIndex);
    if (index == BITMAP_FAILED)
    {
        SpinUnLock(savedIrql, &VgpuMemory.SpinLock);
        VGPU_DEBUG_PRINT("WRONG: can't find the bits to allocate");
        return FALSE;
    }

    VgpuMemory.LastFreeIndex = index + page;
    VgpuMemory.AvailableMemorySize -= Size;

    // finish processing
    SpinUnLock(savedIrql, &VgpuMemory.SpinLock);

    offset = (ULONG64)index * PAGE_SIZE;

    // return the suitable memory region
    Memory->PhysicalAddress.QuadPart = VgpuMemory.PhysicalAddress.QuadPart + offset;
    Memory->VirtualAddress = VgpuMemory.VirtualAddress + offset;

    return TRUE;
}

VOID FreeVgpuMemory(PVOID VitrualAddress, SIZE_T Size)
{
    KIRQL savedIrql;
    ULONG index;

    ASSERT(VgpuMemory.bInitialize);

    // start processing
    SpinLock(&savedIrql, &VgpuMemory.SpinLock);

    index = RtlFindSetBitsAndClear(
        &VgpuMemory.RegionBitmap, 
        (ULONG)(Size / PAGE_SIZE),
        (ULONG)(((PUINT8)VitrualAddress - VgpuMemory.VirtualAddress) / PAGE_SIZE));

    if (index == BITMAP_FAILED)
    {
        SpinUnLock(savedIrql, &VgpuMemory.SpinLock);
        VGPU_DEBUG_PRINT("WRONG: can't find the bits to free");
        return;
    }

    VgpuMemory.LastFreeIndex = index;
    VgpuMemory.AvailableMemorySize += Size;

    // end processing
    SpinUnLock(savedIrql, &VgpuMemory.SpinLock);

    VGPU_DEBUG_LOG("VGPU available memory size=0x%llx", VgpuMemory.AvailableMemorySize);
}

BOOLEAN ReallocVgpuMemory(PVOID VitrualAddress, SIZE_T OriginSize, SIZE_T TargetSize)
{
    KIRQL   savedIrql;
    ULONG   index;
    SIZE_T  change;

    ASSERT(VgpuMemory.bInitialize);

    if (OriginSize == TargetSize)
    {
        return FALSE;
    }
    else if (OriginSize < TargetSize)
    {
        VGPU_DEBUG_PRINT("WRONG: expanding size was not implemented");
        return FALSE;
    }

    change = OriginSize - TargetSize;

    // start processing
    SpinLock(&savedIrql, &VgpuMemory.SpinLock);

    index = RtlFindSetBitsAndClear(
        &VgpuMemory.RegionBitmap, 
        (ULONG)(change / PAGE_SIZE),
        (ULONG)(((PUINT8)VitrualAddress - VgpuMemory.VirtualAddress + TargetSize) / PAGE_SIZE));

    if (index == BITMAP_FAILED)
    {
        SpinUnLock(savedIrql, &VgpuMemory.SpinLock);
        VGPU_DEBUG_PRINT("WRONG: can't find the bits to free");
        return FALSE;
    }

    VgpuMemory.LastFreeIndex = index;
    VgpuMemory.AvailableMemorySize += change;

    // end processing
    SpinUnLock(savedIrql, &VgpuMemory.SpinLock);

    return TRUE;
}