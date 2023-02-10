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

#include "global.h"
#include "idr.h"

static IDRANDOM Idrs[IDR_MAX_SIZE];

VOID InitializeIdr()
{
    for (size_t i = 0; i < ARRAYSIZE(Idrs); i++)
    {
        ASSERT(!Idrs[i].Initilaized);

        RtlZeroMemory(&Idrs[i], sizeof(IDRANDOM));
        KeInitializeSpinLock(&Idrs[i].SpinLock);
        InitializeListHead(&Idrs[i].FreeIdList);
        ExInitializeLookasideListEx(
            &Idrs[i].LookAsideList,
            NULL,
            NULL,
            NonPagedPool,
            0,
            sizeof(FREEID),
            VIRTIO_VGPU_MEMORY_TAG,
            0
        );

        // 0 is reserved for virgl3d in some case
        Idrs[i].Id.QuadPart = 1;
        Idrs[i].Initilaized = TRUE;
    }
}

VOID UnInitializeIdr()
{
    for (size_t i = 0; i < ARRAYSIZE(Idrs); i++)
    {
        ASSERT(Idrs[i].Initilaized);
        while (!IsListEmpty(&Idrs[i].FreeIdList)) {
            PLIST_ENTRY item = RemoveHeadList(&Idrs[i].FreeIdList);
            PFREEID freeId = CONTAINING_RECORD(item, FREEID, Entry);
            ExFreeToLookasideListEx(&Idrs[i].LookAsideList, freeId);
        }
        ExDeleteLookasideListEx(&Idrs[i].LookAsideList);
        Idrs[i].Initilaized = FALSE;
    }
}

VOID GetIdFromIdrWithoutCache(UINT8 type, PVOID Id, SIZE_T Size)
{
    ASSERT(Idrs[type].Initilaized);

    KIRQL savedIrql;
    SpinLock(&savedIrql, &Idrs[type].SpinLock);
    RtlCopyMemory(Id, &Idrs[type].Id, Size);
    Idrs[type].Id.QuadPart++;
    SpinUnLock(savedIrql, &Idrs[type].SpinLock);
}

VOID GetIdFromIdr(UINT8 type, PVOID Id, SIZE_T Size)
{
    ASSERT(Idrs[type].Initilaized);

    KIRQL savedIrql;
    SpinLock(&savedIrql, &Idrs[type].SpinLock);
    if (IsListEmpty(&Idrs[type].FreeIdList))
    {
        RtlCopyMemory(Id, &Idrs[type].Id, Size);
        Idrs[type].Id.QuadPart++;
    }
    else
    {
        PLIST_ENTRY item = RemoveHeadList(&Idrs[type].FreeIdList);
        PFREEID freeId = CONTAINING_RECORD(item, FREEID, Entry);
        RtlCopyMemory(Id, &freeId->Id, Size);
        ExFreeToLookasideListEx(&Idrs[type].LookAsideList, freeId);
    }
    SpinUnLock(savedIrql, &Idrs[type].SpinLock);
}

VOID PutIdToIdr(UINT8 type, PVOID Id, SIZE_T Size)
{
    ASSERT(Idrs[type].Initilaized);

    PFREEID freeId = ExAllocateFromLookasideListEx(&Idrs[type].LookAsideList);
    ASSERT(freeId != NULL);

    freeId->Id.QuadPart = 0;
    RtlCopyMemory(&freeId->Id, Id, Size);
    ExInterlockedInsertTailList(&Idrs[type].FreeIdList, &freeId->Entry, &Idrs[type].SpinLock);
}
