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
#include "control.h"
#include "command.h"
#include "memory.h"
#include "idr.h"


PVIRGL_CONTEXT GetVirglContextFromList(ULONG32 VirglContextId)
{
    KIRQL           savedIrql;
    PLIST_ENTRY     item = NULL;
    PVIRGL_CONTEXT  virglContext = NULL;
    BOOLEAN         bFind = FALSE;

    SpinLock(&savedIrql, &VirglContextListSpinLock);
    for (item = VirglContextList.Flink; item != &VirglContextList; item = item->Flink) {
        virglContext = CONTAINING_RECORD(item, VIRGL_CONTEXT, Entry);
        if (virglContext->Id == VirglContextId)
        {
            bFind = TRUE;
            break;
        }
    }
    SpinUnLock(savedIrql, &VirglContextListSpinLock);

    return bFind ? virglContext : NULL;
}

PVIRGL_RESOURCE GetResourceFromListUnsafe(PVIRGL_CONTEXT virglContext, ULONG32 Id)
{
    PLIST_ENTRY     item = NULL;
    PVIRGL_RESOURCE resource = NULL;

    for (item = virglContext->ResourceList.Flink; item != &virglContext->ResourceList; item = item->Flink)
    {
        resource = CONTAINING_RECORD(item, VIRGL_RESOURCE, Entry);
        if (resource->Id == Id)
        {
            break;
        }
    }

    return resource;
}

PVIRGL_RESOURCE GetResourceFromList(PVIRGL_CONTEXT virglContext, ULONG32 Id)
{
    PVIRGL_RESOURCE resource = NULL;
    KIRQL           savedIrql;

    SpinLock(&savedIrql, &virglContext->ResourceListSpinLock);
    resource = GetResourceFromListUnsafe(virglContext, Id);
    SpinUnLock(savedIrql, &virglContext->ResourceListSpinLock);

    return resource;
}

VOID SetResourceState(PVIRGL_CONTEXT virglContext, ULONG32* Ids, SIZE_T IdsSize, BOOLEAN Busy, ULONG64 FenceId)
{
    PVIRGL_RESOURCE resource;
    KIRQL           savedIrql;
    SIZE_T          index;
    SIZE_T          count = IdsSize / sizeof(ULONG32);

    SpinLock(&savedIrql, &virglContext->ResourceListSpinLock);
    for (index = 0; index < count; index++)
    {
        resource = GetResourceFromListUnsafe(virglContext, Ids[index]);
        if (resource)
        {
            if (Busy)
            {
                if (FenceId > resource->FenceId)
                {
                    resource->FenceId = FenceId;
                }
                KeClearEvent(&resource->StateEvent);
            }
            else
            {
                if (FenceId >= resource->FenceId)
                {
                    KeSetEvent(&resource->StateEvent, 0, FALSE);
                }
            }
        }
    }
    SpinUnLock(savedIrql, &virglContext->ResourceListSpinLock);
}

BOOLEAN CreateUserShareMemory(PSHARE_DESCRIPTOR ShareMemory)
{
    if (ShareMemory->KennelAddress == NULL || ShareMemory->Size == 0)
    {
        VGPU_DEBUG_LOG("params invalid address=%p size=%lld", ShareMemory->KennelAddress, ShareMemory->Size);
        return FALSE;
    }

    ShareMemory->pMdl = IoAllocateMdl(ShareMemory->KennelAddress, (ULONG32)ShareMemory->Size, FALSE, FALSE, NULL);
    if (!ShareMemory->pMdl)
    {
        VGPU_DEBUG_PRINT("IoAllocateMdl failed");
        return FALSE;
    }
    MmBuildMdlForNonPagedPool(ShareMemory->pMdl);

    try {
        ShareMemory->UserAdderss = MmMapLockedPagesSpecifyCache(ShareMemory->pMdl, UserMode, MmNonCached, NULL, FALSE, NormalPagePriority);
    } except(EXCEPTION_EXECUTE_HANDLER) {
        IoFreeMdl(ShareMemory->pMdl);
        VGPU_DEBUG_PRINT("except: create share memory with user failed");
        return FALSE;
    }

    if (!ShareMemory->UserAdderss)
    {
        IoFreeMdl(ShareMemory->pMdl);
        return FALSE;
    }

    return TRUE;
}

VOID DeleteUserShareMemory(PSHARE_DESCRIPTOR ShareMemory)
{
    if (ShareMemory->UserAdderss == NULL || ShareMemory->pMdl == NULL)
    {
        VGPU_DEBUG_PRINT("params invalid");
        return;
    }

    MmUnmapLockedPages(ShareMemory->UserAdderss, ShareMemory->pMdl);
    IoFreeMdl(ShareMemory->pMdl);
}

VOID DeleteResource(PVIRGL_CONTEXT VirglContext, PVIRGL_RESOURCE resource)
{
    if (resource->bForBuffer)
    {
        DetachResourceBacking(VirglContext->DeviceContext, resource);
        if (resource->Buffer.Share.pMdl)
        {
            DeleteUserShareMemory(&resource->Buffer.Share);
        }
        FreeVgpuMemory(resource->Buffer.VgpuMemory.VirtualAddress);
    }

    UnrefResource(VirglContext->DeviceContext, VirglContext->Id, resource->Id);
}

NTSTATUS CtlGetParams(IN PDEVICE_CONTEXT Context, IN WDFREQUEST Request, IN size_t OutputBufferLength, IN size_t InputBufferLength, OUT size_t* bytesReturn)
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    ULONG64 *input = NULL, *output = NULL;

    if (!Capsets.Initialized)
    {
        GetCapsInfo(Context);
        Capsets.Initialized = TRUE;
    }

    status = WdfRequestRetrieveInputBuffer(Request, InputBufferLength, &input, bytesReturn);
    if (!NT_SUCCESS(status))
    {
        VGPU_DEBUG_LOG("WdfRequestRetrieveInputBuffer failed status=0x%08x", status);
        return status;
    }

    if (*bytesReturn != sizeof(ULONG64))
    {
        VGPU_DEBUG_LOG("get wrong buffer size=%lld", *bytesReturn);
        return STATUS_UNSUCCESSFUL;
    }

    status = WdfRequestRetrieveOutputBuffer(Request, OutputBufferLength, &output, bytesReturn);
    if (!NT_SUCCESS(status))
    {
        VGPU_DEBUG_LOG("WdfRequestRetrieveOutputBuffer failed status=0x%08x", status);
        return status;
    }

    if (*bytesReturn != sizeof(ULONG64))
    {
        VGPU_DEBUG_LOG("get wrong buffer size=%lld", *bytesReturn);
        return STATUS_UNSUCCESSFUL;
    }

    switch (*input)
    {
    case 0x1000:
        // get vgpu staging config
        VirtIOWdfDeviceGet(&Context->VDevice, FIELD_OFFSET(struct virtio_vgpu_config, staging), output, sizeof(UINT8));
        break;
    case VIRTGPU_PARAM_SUPPORTED_CAPSET_IDs:
        *output = Capsets.CapsetIdMask;
        break;
    default:
        if ((Context->Capabilities & (1LL << ((*input) - 1))))
        {
            *output = 1;
        }
        else
        {
            *output = 0;
        }
        break;
    }
    VGPU_DEBUG_LOG("param=%lld result=%lld", *input, *output);

    return status;
}

NTSTATUS CtlGetCaps(IN PDEVICE_CONTEXT Context, IN WDFREQUEST Request, IN size_t OutputBufferLength, IN size_t InputBufferLength, OUT size_t* bytesReturn)
{
    NTSTATUS                        status;
    PVOID                           pCaps;
    struct drm_virtgpu_get_caps*    pGetCaps;
    INT32                           index = -1;

    status = WdfRequestRetrieveInputBuffer(Request, InputBufferLength, &pGetCaps, bytesReturn);
    if (!NT_SUCCESS(status))
    {
        VGPU_DEBUG_LOG("WdfRequestRetrieveInputBuffer failed status=0x%08x", status);
        return status;
    }

    if (*bytesReturn != sizeof(struct drm_virtgpu_get_caps))
    {
        VGPU_DEBUG_LOG("get wrong buffer size=%lld", *bytesReturn);
        return STATUS_UNSUCCESSFUL;
    }

    for (UINT32 i = 0; i < Capsets.NumCaps; i++)
    {
        if (Capsets.Data[i].id == pGetCaps->cap_set_id && Capsets.Data[i].max_version >= pGetCaps->cap_set_ver)
        {
            index = i;
            break;
        }
    }

    if (index == -1)
    {
        VGPU_DEBUG_PRINT("failed to get the right caps");
        return STATUS_UNSUCCESSFUL;
    }

    status = WdfRequestRetrieveOutputBuffer(Request, OutputBufferLength, &pCaps, bytesReturn);
    if (!NT_SUCCESS(status))
    {
        VGPU_DEBUG_LOG("WdfRequestRetrieveOutputBuffer failed status=0x%08x", status);
        return status;
    }

    if (*bytesReturn != Capsets.Data[index].max_size)
    {
        VGPU_DEBUG_LOG("get wrong buffer size=%lld", *bytesReturn);
        return STATUS_UNSUCCESSFUL;
    }

    GetCaps(Context, index, pGetCaps->cap_set_ver, pCaps);
    return STATUS_SUCCESS;
}

NTSTATUS CtlInitVirglContext(IN PDEVICE_CONTEXT Context, IN WDFREQUEST Request, IN size_t InputBufferLength, OUT size_t* bytesReturn)
{
    NTSTATUS        status;
    PVIRGL_CONTEXT  virglContext;
    WDFMEMORY       contextParamMem;
    ULONG           contextInit = 0;
    ULONG           contextId;
    struct drm_virtgpu_context_init* init;
    struct drm_virtgpu_context_set_param* params;

    // use current process id as virgl context id
    contextId = HandleToULong(PsGetCurrentProcessId());

    virglContext = GetVirglContextFromList(contextId);
    if (virglContext)
    {
        VGPU_DEBUG_PRINT("virgl context has already been initialized");
        return STATUS_UNSUCCESSFUL;
    }

    status = WdfRequestRetrieveInputBuffer(Request, InputBufferLength, &init, bytesReturn);
    if (!NT_SUCCESS(status))
    {
        VGPU_DEBUG_LOG("WdfRequestRetrieveInputBuffer failed status=0x%08x", status);
        return status;
    }

    if (*bytesReturn != sizeof(struct drm_virtgpu_context_init))
    {
        VGPU_DEBUG_LOG("get wrong buffer size=%lld", *bytesReturn);
        return STATUS_UNSUCCESSFUL;
    }

    status = WdfRequestProbeAndLockUserBufferForRead(Request, (PVOID)init->ctx_set_params, init->num_params * sizeof(struct drm_virtgpu_context_set_param), &contextParamMem);
    if (!NT_SUCCESS(status))
    {
        VGPU_DEBUG_PRINT("WdfRequestProbeAndLockUserBufferForRead bohandles failed");
        return status;
    }

    params = WdfMemoryGetBuffer(contextParamMem, NULL);
    if (!params)
    {
        VGPU_DEBUG_PRINT("WdfMemoryGetBuffer failed");
        return STATUS_UNSUCCESSFUL;
    }

    for (size_t i = 0; i < init->num_params; i++)
    {
        switch (params[i].param) {
        case VIRTGPU_CONTEXT_PARAM_CAPSET_ID:
            if (params[i].value > MAX_CAPSET_ID) 
            {
                status = STATUS_UNSUCCESSFUL;
                break;
            }
            if ((Capsets.CapsetIdMask & (1LL << params[i].value)) == 0)
            {
                status = STATUS_UNSUCCESSFUL;
                break;
            }
            contextInit |= params[i].value;
            break;
        case VIRTGPU_CONTEXT_PARAM_NUM_RINGS:
        case VIRTGPU_CONTEXT_PARAM_POLL_RINGS_MASK:
        default:
            VGPU_DEBUG_PRINT("unimplement features");
            status = STATUS_UNSUCCESSFUL;
            break;
        }
    }

    if (!NT_SUCCESS(status))
    {
        VGPU_DEBUG_PRINT("set context init param failed");
        return status;
    }

    virglContext = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(VIRGL_CONTEXT), VIRTIO_VGPU_MEMORY_TAG);
    if (virglContext == NULL)
    {
        VGPU_DEBUG_PRINT("allocate memory failed");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // initialize virgl context
    virglContext->Id = contextId;
    virglContext->DeviceContext = Context;
    KeInitializeSpinLock(&virglContext->ResourceListSpinLock);
    InitializeListHead(&virglContext->ResourceList);
    ExInterlockedInsertTailList(&VirglContextList, &virglContext->Entry, &VirglContextListSpinLock);
    
    CreateVirglContext(Context, contextId, contextInit);
    VGPU_DEBUG_LOG("create virgl context id=%d", contextId);

    return status;
}

NTSTATUS CtlDestroyVirglContext(IN PVIRGL_CONTEXT VirglContext)
{
    KIRQL           savedIrql;
    PLIST_ENTRY     item;
    PVIRGL_RESOURCE resource;

    SpinLock(&savedIrql, &VirglContextListSpinLock);
    RemoveEntryList(&VirglContext->Entry);
    SpinUnLock(savedIrql, &VirglContextListSpinLock);

    SpinLock(&savedIrql, &VirglContext->ResourceListSpinLock);
    while (!IsListEmpty(&VirglContext->ResourceList))
    {
        item = RemoveTailList(&VirglContext->ResourceList);
        resource = CONTAINING_RECORD(item, VIRGL_RESOURCE, Entry);

        // free resource
        DeleteResource(VirglContext, resource);
        ExFreeToLookasideListEx(&VirglContext->DeviceContext->VirglResourceLookAsideList, resource);
    }
    SpinUnLock(savedIrql, &VirglContext->ResourceListSpinLock);

    // tell the host to destroy the virgl context
    DestroyVirglContext(VirglContext->DeviceContext, VirglContext->Id);
    VGPU_DEBUG_LOG("destroy virgl context id=%d", VirglContext->Id);

    ExFreePoolWithTag(VirglContext, VIRTIO_VGPU_MEMORY_TAG);

    return STATUS_SUCCESS;
}

NTSTATUS CtlCreateResource(IN WDFREQUEST Request, IN size_t OutputBufferLength, IN size_t InputBufferLength, OUT size_t* bytesReturn)
{
    NTSTATUS                                    status;
    PVIRGL_RESOURCE                             resource;
    PVIRGL_CONTEXT                              virglContext;
    VIRTGPU_RESOURCE_CREATE_PARAM               create;
    struct drm_virtgpu_resource_create*         pCreateResource;
    struct drm_virtgpu_resource_create_resp*    pCreateResourceResp;

    status = WdfRequestRetrieveInputBuffer(Request, InputBufferLength, &pCreateResource, bytesReturn);
    if (!NT_SUCCESS(status))
    {
        VGPU_DEBUG_LOG("WdfRequestRetrieveInputBuffer failed status=0x%08x", status);
        return status;
    }

    if (*bytesReturn != sizeof(struct drm_virtgpu_resource_create))
    {
        VGPU_DEBUG_LOG("get wrong buffer size=%lld", *bytesReturn);
        return STATUS_UNSUCCESSFUL;
    }

    status = WdfRequestRetrieveOutputBuffer(Request, OutputBufferLength, &pCreateResourceResp, bytesReturn);
    if (!NT_SUCCESS(status))
    {
        VGPU_DEBUG_LOG("WdfRequestRetrieveOutputBuffer failed status=0x%08x", status);
        return status;
    }

    if (*bytesReturn != sizeof(struct drm_virtgpu_resource_create_resp))
    {
        VGPU_DEBUG_LOG("get wrong buffer size=%lld", *bytesReturn);
        return STATUS_UNSUCCESSFUL;
    }

    virglContext = GetVirglContextFromList(HandleToULong(PsGetCurrentProcessId()));
    if (!virglContext)
    {
        VGPU_DEBUG_PRINT("get virgl context failed");
        return STATUS_UNSUCCESSFUL;
    }

    resource = ExAllocateFromLookasideListEx(&virglContext->DeviceContext->VirglResourceLookAsideList);
    if (resource == NULL)
    {
        VGPU_DEBUG_PRINT("allocate memory failed");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    create.format = pCreateResource->format;
    create.width = pCreateResource->width;
    create.height = pCreateResource->height;
    create.array_size = pCreateResource->array_size;
    create.bind = pCreateResource->bind;
    create.target = pCreateResource->target;
    create.depth = pCreateResource->depth;
    create.flags = pCreateResource->flags;
    create.last_level = pCreateResource->last_level;
    create.nr_samples = pCreateResource->nr_samples;

    GetIdFromIdrWithoutCache(VIRGL_RESOURCE_ID_TYPE, &resource->Id, sizeof(ULONG32));
    resource->FenceId = 0;

    // init state event to false means the resource is busy now
    // we make all resource as idle when created
    KeInitializeEvent(&resource->StateEvent, NotificationEvent, TRUE);

    // treat all kind of resources as 3d resoures, they would be handled in virglrenderer
    Create3DResource(virglContext->DeviceContext, virglContext->Id, resource->Id, &create, 0);

    // VIRGL_CAP_COPY_TRANSFER set size=1 of resource without buffer
    resource->bForBuffer = pCreateResource->size != 1;

    if (resource->bForBuffer)
    {
        // initialize pMdl to null in case double free in map/close resource
        resource->Buffer.Share.pMdl = NULL;

        if (pCreateResource->size == 0)
        {
            resource->Buffer.Size = PAGE_SIZE;
        }
        else
        {
            resource->Buffer.Size = ROUND_UP(pCreateResource->size, PAGE_SIZE);
        }

        if (!AllocateVgpuMemory(resource->Buffer.Size, &resource->Buffer.VgpuMemory))
        {
            VGPU_DEBUG_PRINT("allocate dma memory failed");
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        // clear memory to avoid crash in cinema4d sometime
        RtlZeroMemory(resource->Buffer.VgpuMemory.VirtualAddress, resource->Buffer.Size);
        AttachResourceBacking(virglContext->DeviceContext, virglContext->Id, resource);
    }

    // insert to the resource list
    ExInterlockedInsertTailList(&virglContext->ResourceList, &resource->Entry, &virglContext->ResourceListSpinLock);
    VGPU_DEBUG_LOG("create resource id=%d bForBuffer=%d size=%d", resource->Id, resource->bForBuffer, pCreateResource->size);
    
    // different from gem object in linux
    pCreateResourceResp->res_handle = pCreateResourceResp->bo_handle = resource->Id;
    return status;
}

NTSTATUS CtlCloseResource(IN WDFREQUEST Request, IN size_t InputBufferLength, OUT size_t* bytesReturn)
{
    NTSTATUS                status;
    KIRQL                   savedIrql;
    PVIRGL_CONTEXT          virglContext;
    PVIRGL_RESOURCE         resource;
    struct drm_gem_close*   close;

    status = WdfRequestRetrieveInputBuffer(Request, InputBufferLength, &close, bytesReturn);
    if (!NT_SUCCESS(status))
    {
        VGPU_DEBUG_LOG("WdfRequestRetrieveInputBuffer failed status=0x%08x", status);
        return status;
    }

    if (*bytesReturn != sizeof(struct drm_gem_close))
    {
        VGPU_DEBUG_LOG("get wrong buffer size=%lld", *bytesReturn);
        return STATUS_UNSUCCESSFUL;
    }

    virglContext = GetVirglContextFromList(HandleToULong(PsGetCurrentProcessId()));
    if (!virglContext)
    {
        VGPU_DEBUG_PRINT("get virgl context failed");
        return STATUS_UNSUCCESSFUL;
    }

    resource = GetResourceFromList(virglContext, close->handle);
    if (!resource)
    {
        VGPU_DEBUG_LOG("get resource failed id=%d", close->handle);
        return STATUS_UNSUCCESSFUL;
    }
    
    // FIXME: we didn't put resource id back to idr, because it may conflict with migration

    // delete the resource from the virgl context resource list
    SpinLock(&savedIrql, &virglContext->ResourceListSpinLock);
    RemoveEntryList(&resource->Entry);
    SpinUnLock(savedIrql, &virglContext->ResourceListSpinLock);

    // free resource
    DeleteResource(virglContext, resource);
    ExFreeToLookasideListEx(&virglContext->DeviceContext->VirglResourceLookAsideList, resource);
    VGPU_DEBUG_LOG("close resource id=%d", close->handle);

    return status;
}

NTSTATUS CtlWait(IN WDFREQUEST Request, IN size_t OutputBufferLength, IN size_t InputBufferLength, OUT size_t* bytesReturn)
{
    NTSTATUS                    status = STATUS_UNSUCCESSFUL;
    ULONG32*                    result;
    PVIRGL_CONTEXT              virglContext;
    PVIRGL_RESOURCE             resource;
    struct drm_virtgpu_3d_wait* cmd;

    status = WdfRequestRetrieveInputBuffer(Request, InputBufferLength, &cmd, bytesReturn);
    if (!NT_SUCCESS(status))
    {
        VGPU_DEBUG_LOG("WdfRequestRetrieveInputBuffer failed status=0x%08x", status);
        return status;
    }

    if (*bytesReturn != sizeof(struct drm_virtgpu_3d_wait))
    {
        VGPU_DEBUG_LOG("get wrong buffer size=%lld", *bytesReturn);
        return STATUS_UNSUCCESSFUL;
    }

    status = WdfRequestRetrieveOutputBuffer(Request, OutputBufferLength, &result, bytesReturn);
    if (!NT_SUCCESS(status))
    {
        VGPU_DEBUG_LOG("WdfRequestRetrieveOutputBuffer failed status=0x%08x", status);
        return status;
    }

    if (*bytesReturn != sizeof(ULONG32))
    {
        VGPU_DEBUG_LOG("get wrong buffer size=%lld", *bytesReturn);
        return STATUS_UNSUCCESSFUL;
    }

    virglContext = GetVirglContextFromList(HandleToULong(PsGetCurrentProcessId()));
    if (!virglContext)
    {
        VGPU_DEBUG_PRINT("get virgl context failed");
        return STATUS_UNSUCCESSFUL;
    }

    resource = GetResourceFromList(virglContext, cmd->handle);
    if (!resource)
    {
        VGPU_DEBUG_LOG("get resource failed id=%d", cmd->handle);
        return STATUS_UNSUCCESSFUL;
    }

    if (cmd->flags & VIRTGPU_WAIT_NOWAIT)
    {
        if (KeReadStateEvent(&resource->StateEvent) == 0)
        {
            // resource is busy now
            *result = 1;
        }
        else
        {
            // resource is idle now
            *result = 0;
        }
    }
    else
    {
        LARGE_INTEGER timeout;
        timeout.QuadPart = (-10 * 1000 * 1000); // 1s
        status = KeWaitForSingleObject(&resource->StateEvent, Executive, KernelMode, FALSE, &timeout);
        if (NT_SUCCESS(status))
        {
            *result = 0;
        }
        else
        {
            *result = 1;
            VGPU_DEBUG_LOG("wait for resource failed id=%d status=0x%08x", cmd->handle, status);
        }
    }

    return status;
}

NTSTATUS CtlMap(IN WDFREQUEST Request, IN size_t OutputBufferLength, IN size_t InputBufferLength, OUT size_t* bytesReturn)
{
    NTSTATUS                status;
    ULONG64*                ptr;
    PVIRGL_CONTEXT          virglContext;
    PVIRGL_RESOURCE         resource;
    struct drm_virtgpu_map* cmd;

    status = WdfRequestRetrieveInputBuffer(Request, InputBufferLength, &cmd, bytesReturn);
    if (!NT_SUCCESS(status))
    {
        VGPU_DEBUG_LOG("WdfRequestRetrieveInputBuffer failed status=0x%08x", status);
        return status;
    }

    if (*bytesReturn != sizeof(struct drm_virtgpu_map))
    {
        VGPU_DEBUG_LOG("get wrong buffer size=%lld", *bytesReturn);
        return STATUS_UNSUCCESSFUL;
    }

    status = WdfRequestRetrieveOutputBuffer(Request, OutputBufferLength, &ptr, bytesReturn);
    if (!NT_SUCCESS(status))
    {
        VGPU_DEBUG_LOG("WdfRequestRetrieveOutputBuffer failed status=0x%08x", status);
        return status;
    }

    if (*bytesReturn != sizeof(ULONG64))
    {
        VGPU_DEBUG_LOG("get wrong buffer size=%lld", *bytesReturn);
        return STATUS_UNSUCCESSFUL;
    }

    virglContext = GetVirglContextFromList(HandleToULong(PsGetCurrentProcessId()));
    if (!virglContext)
    {
        VGPU_DEBUG_PRINT("get virgl context failed");
        return STATUS_UNSUCCESSFUL;
    }

    resource = GetResourceFromList(virglContext, cmd->handle);
    if (!resource)
    {
        VGPU_DEBUG_LOG("get resource failed id=%d", cmd->handle);
        return STATUS_UNSUCCESSFUL;
    }

    if (!resource->bForBuffer)
    {
        VGPU_DEBUG_LOG("fence resource can't be mapped id=%d", cmd->handle);
        return STATUS_UNSUCCESSFUL;
    }

    if (resource->Buffer.Share.pMdl != NULL)
    {
        *ptr = (ULONG64)resource->Buffer.Share.UserAdderss;
        VGPU_DEBUG_LOG("this resource had already been mapped id=%d", cmd->handle);
    }
    else
    {
        resource->Buffer.Share.KennelAddress = resource->Buffer.VgpuMemory.VirtualAddress;
        resource->Buffer.Share.Size = resource->Buffer.Size;
        if (CreateUserShareMemory(&resource->Buffer.Share))
        {
            *ptr = (ULONG64)resource->Buffer.Share.UserAdderss;
        }
        else
        {
            resource->Buffer.Share.pMdl = NULL;
            VGPU_DEBUG_PRINT("create share memory failed");
            return STATUS_UNSUCCESSFUL;
        }
    }

    return status;
}

NTSTATUS CtlSubmitCommand(IN WDFREQUEST Request, IN size_t InputBufferLength, OUT size_t* bytesReturn)
{
    NTSTATUS                        status;
    ULONG64                         fenceId;
    WDFMEMORY                       commandMem;
    WDFMEMORY                       handlesMem;
    PVOID                           inFence = NULL;
    PVOID                           outFence = NULL;
    PVOID                           wdfCommandBuf;
    PVOID                           extend = NULL;
    ULONG32*                        bohandles;
    SIZE_T                          boHandlesSize = 0;
    VGPU_MEMORY_DESCRIPTOR          vgpuMemory;
    PVIRGL_CONTEXT                  virglContext;
    struct drm_virtgpu_execbuffer*  cmd;

    status = WdfRequestRetrieveInputBuffer(Request, InputBufferLength, &cmd, bytesReturn);
    if (!NT_SUCCESS(status))
    {
        VGPU_DEBUG_LOG("WdfRequestRetrieveInputBuffer failed status=0x%08x", status);
        return status;
    }

    if (*bytesReturn != sizeof(struct drm_virtgpu_execbuffer))
    {
        VGPU_DEBUG_LOG("get wrong buffer size=%lld", *bytesReturn);
        return STATUS_UNSUCCESSFUL;
    }

    status = WdfRequestProbeAndLockUserBufferForRead(Request, (PVOID)cmd->command, cmd->size, &commandMem);
    if (!NT_SUCCESS(status))
    {
        VGPU_DEBUG_LOG("WdfRequestProbeAndLockUserBufferForRead failed status=0x%08x", status);
        return status;
    }

    wdfCommandBuf = WdfMemoryGetBuffer(commandMem, NULL);
    if (!wdfCommandBuf)
    {
        VGPU_DEBUG_PRINT("WdfMemoryGetBuffer failed");
        return STATUS_UNSUCCESSFUL;
    }

    virglContext = GetVirglContextFromList(HandleToULong(PsGetCurrentProcessId()));
    if (!virglContext)
    {
        VGPU_DEBUG_PRINT("get virgl context failed");
        return STATUS_UNSUCCESSFUL;
    }

    if (cmd->flags & VIRTGPU_EXECBUF_FENCE_FD_IN)
    {
        ASSERT(cmd->in_fence_fd != NULL);
        status = ObReferenceObjectByHandle(cmd->in_fence_fd, GENERIC_ALL, NULL, KernelMode, &inFence, NULL);
        if (!NT_SUCCESS(status))
        {
            VGPU_DEBUG_LOG("ObReferenceObjectByHandle failed in_fence_fd=%p status=0x%08x", cmd->in_fence_fd, status);
            return status;
        }

        status = KeWaitForSingleObject(inFence, Executive, KernelMode, FALSE, 0);
        if (!NT_SUCCESS(status))
        {
            VGPU_DEBUG_LOG("KeWaitForSingleObject failed in_fence_fd=%p status=0x%08x", cmd->in_fence_fd, status);
            return status;
        }
    }

    if (cmd->flags & VIRTGPU_EXECBUF_FENCE_FD_OUT)
    {
        ASSERT(cmd->out_fence_fd != NULL);
        status = ObReferenceObjectByHandle(cmd->out_fence_fd, GENERIC_ALL, NULL, KernelMode, &outFence, NULL);
        if (!NT_SUCCESS(status))
        {
            VGPU_DEBUG_LOG("ObReferenceObjectByHandle failed out_fence_fd=%p status=0x%08x", cmd->out_fence_fd, status);
            return status;
        }
    }

    // get new fence
    GetIdFromIdrWithoutCache(FENCE_ID_TYPE, &fenceId, sizeof(ULONG64));

    if (cmd->num_bo_handles > 0)
    {
        boHandlesSize = sizeof(ULONG32) * cmd->num_bo_handles;
        status = WdfRequestProbeAndLockUserBufferForRead(Request, (PVOID)cmd->bo_handles, boHandlesSize, &handlesMem);
        if (!NT_SUCCESS(status))
        {
            VGPU_DEBUG_PRINT("WdfRequestProbeAndLockUserBufferForRead bohandles failed");
            return status;
        }

        bohandles = WdfMemoryGetBuffer(handlesMem, NULL);
        if (!bohandles)
        {
            VGPU_DEBUG_PRINT("WdfMemoryGetBuffer failed");
            return STATUS_UNSUCCESSFUL;
        }

        // make all resources referenced busy
        SetResourceState(virglContext, bohandles, boHandlesSize, TRUE, fenceId);

        // backup these handles to make them idle later
        extend = ExAllocatePool2(POOL_FLAG_NON_PAGED | POOL_FLAG_UNINITIALIZED, boHandlesSize, VIRTIO_VGPU_MEMORY_TAG);
        if (extend == NULL)
        {
            VGPU_DEBUG_PRINT("allocate memory failed");
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlCopyMemory(extend, bohandles, boHandlesSize);
    }

    // the command buffer from user mode was allocated from paged pool memory, we had to copy it to nonpaged pool to get the physical address
    if (!AllocateVgpuMemory(cmd->size, &vgpuMemory))
    {
        if (extend)
        {
            ExFreePoolWithTag(extend, VIRTIO_VGPU_MEMORY_TAG);
        }
        VGPU_DEBUG_PRINT("allocate memory failed");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // copy user cmd buffer to vgpu memory
    RtlCopyMemory(vgpuMemory.VirtualAddress, wdfCommandBuf, cmd->size);

    return SubmitCommand(virglContext->DeviceContext, virglContext->Id, &vgpuMemory, cmd->size, extend, boHandlesSize, fenceId, outFence);
}

NTSTATUS CtlTransferHost(IN BOOLEAN ToHost, IN WDFREQUEST Request, IN size_t InputBufferLength, OUT size_t* bytesReturn)
{
    NTSTATUS                        status;
    ULONG64                         fenceId;
    PVIRGL_CONTEXT                  virglContext;
    PVIRGL_RESOURCE                 resource;
    struct drm_virtgpu_3d_transfer* cmd;

    status = WdfRequestRetrieveInputBuffer(Request, InputBufferLength, &cmd, bytesReturn);
    if (!NT_SUCCESS(status))
    {
        VGPU_DEBUG_LOG("WdfRequestRetrieveInputBuffer failed status=0x%08x", status);
        return status;
    }

    if (*bytesReturn != sizeof(struct drm_virtgpu_3d_transfer))
    {
        VGPU_DEBUG_LOG("get wrong buffer size=%lld", *bytesReturn);
        return STATUS_UNSUCCESSFUL;
    }

    virglContext = GetVirglContextFromList(HandleToULong(PsGetCurrentProcessId()));
    if (!virglContext)
    {
        VGPU_DEBUG_PRINT("get virgl context failed");
        return STATUS_UNSUCCESSFUL;
    }

    resource = GetResourceFromList(virglContext, cmd->bo_handle);
    if (!resource)
    {
        VGPU_DEBUG_LOG("get resource failed id=%d", cmd->bo_handle);
        return STATUS_UNSUCCESSFUL;
    }

    VIRTGPU_TRANSFER_HOST_3D_PARAM  transfer3d;
    transfer3d.box.x = cmd->box.x;
    transfer3d.box.y = cmd->box.y;
    transfer3d.box.z = cmd->box.z;
    transfer3d.box.w = cmd->box.w;
    transfer3d.box.h = cmd->box.h;
    transfer3d.box.d = cmd->box.d;

    transfer3d.layer_stride = cmd->layer_stride;
    transfer3d.level = cmd->level;
    transfer3d.offset = cmd->offset;
    transfer3d.stride = cmd->stride;
    transfer3d.resource_id = resource->Id;

    GetIdFromIdrWithoutCache(FENCE_ID_TYPE, &fenceId, sizeof(ULONG64));
    SetResourceState(virglContext, &resource->Id, sizeof(ULONG32), TRUE, fenceId);
    TransferHost3D(virglContext->DeviceContext, virglContext->Id, &transfer3d, fenceId, ToHost);

    return status;
}