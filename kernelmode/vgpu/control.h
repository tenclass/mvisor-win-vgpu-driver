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
#pragma once

#include "ioctl.h"


FORCEINLINE PVIRGL_RESOURCE GetResourceFromListUnsafe(PVIRGL_CONTEXT VirglContext, ULONG32 Id)
{
    PLIST_ENTRY     item;
    BOOLEAN         bFind = FALSE;
    PVIRGL_RESOURCE resource = NULL;

    for (item = VirglContext->ResourceList.Flink; item != &VirglContext->ResourceList; item = item->Flink)
    {
        resource = CONTAINING_RECORD(item, VIRGL_RESOURCE, Entry);
        if (resource->Id == Id)
        {
            bFind = TRUE;
            break;
        }
    }

    return bFind ? resource : NULL;
}

FORCEINLINE PVIRGL_CONTEXT GetVirglContextFromListUnsafe(ULONG32 VirglContextId)
{
    PLIST_ENTRY     item;
    PVIRGL_CONTEXT  virglContext = NULL;
    BOOLEAN         bFind = FALSE;

    for (item = VirglContextList.Flink; item != &VirglContextList; item = item->Flink) {
        virglContext = CONTAINING_RECORD(item, VIRGL_CONTEXT, Entry);
        if (virglContext->Id == VirglContextId)
        {
            bFind = TRUE;
            break;
        }
    }

    return bFind ? virglContext : NULL;
}

FORCEINLINE VOID UpdateResourceState(PVIRGL_CONTEXT VirglContext, PULONG32 ResourceIds, SIZE_T ResourceIdsCount, BOOLEAN Busy, ULONG64 FenceId)
{
    SIZE_T          index;
    BOOLEAN         bSignal;
    PVIRGL_RESOURCE resource;

    for (index = 0; index < ResourceIdsCount; index++)
    {
        resource = GetResourceFromListUnsafe(VirglContext, ResourceIds[index]);
        if (resource)
        {
            bSignal = FALSE;

            if (FenceId == 0)
            {
                bSignal = TRUE;
            }
            else if (FenceId >= resource->FenceId)
            {
                resource->FenceId = FenceId;
                bSignal = TRUE;
            }

            if (bSignal)
            {
                if (Busy)
                {
                    KeClearEvent(&resource->StateEvent);
                }
                else
                {
                    KeSetEvent(&resource->StateEvent, 0, FALSE);
                }
            }
        }
    }
}

VOID MapBlobResourceCallback(PVIRGL_CONTEXT VirglContext, ULONG32 Id, ULONG64 Gpa, SIZE_T Size);
PVIRGL_CONTEXT GetVirglContextFromList(ULONG32 VirglContextId);
PVIRGL_RESOURCE GetResourceFromList(PVIRGL_CONTEXT VirglContext, ULONG32 Id);

NTSTATUS CtlInitVirglContext(IN PDEVICE_CONTEXT Context, IN WDFREQUEST Request, IN size_t InputBufferLength, OUT size_t* bytesReturn);
NTSTATUS CtlDestroyVirglContext(IN PVIRGL_CONTEXT VirglContext);
NTSTATUS CtlGetParams(IN PDEVICE_CONTEXT Context, IN WDFREQUEST Request, IN size_t InputBufferLength, IN size_t OutputBufferLength, OUT size_t* bytesReturn);
NTSTATUS CtlGetCaps(IN PDEVICE_CONTEXT Context, IN WDFREQUEST Request, IN size_t OutputBufferLength, IN size_t InputBufferLength, OUT size_t* bytesReturn);
NTSTATUS CtlCreateResource(IN WDFREQUEST Request, IN size_t OutputBufferLength, IN size_t InputBufferLength, OUT size_t* bytesReturn);
NTSTATUS CtlCreateBlobResource(IN WDFREQUEST Request, IN size_t OutputBufferLength, IN size_t InputBufferLength, OUT size_t* bytesReturn);
NTSTATUS CtlCloseResource(IN WDFREQUEST Request, IN size_t InputBufferLength, OUT size_t* bytesReturn);
NTSTATUS CtlWait(IN WDFREQUEST Request, IN size_t OutputBufferLength, IN size_t InputBufferLength, OUT size_t* bytesReturn);
NTSTATUS CtlMap(IN WDFREQUEST Request, IN size_t OutputBufferLength, IN size_t InputBufferLength, OUT size_t* bytesReturn);
NTSTATUS CtlSubmitCommand(IN WDFREQUEST Request, IN size_t InputBufferLength, OUT size_t* bytesReturn);
NTSTATUS CtlTransferHost(IN BOOLEAN ToHost, IN WDFREQUEST Request, IN size_t InputBufferLength, OUT size_t* bytesReturn);
NTSTATUS CtlAllocateVgpuMemory(IN WDFREQUEST Request, IN size_t OutputBufferLength, IN size_t InputBufferLength, OUT size_t* bytesReturn);
NTSTATUS CtlFreeVgpuMemory(IN WDFREQUEST Request, IN size_t InputBufferLength, OUT size_t* bytesReturn);