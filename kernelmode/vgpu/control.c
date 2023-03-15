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
    PVIRGL_CONTEXT  virglContext;

    SpinLock(&savedIrql, &VirglContextListSpinLock);
    virglContext = GetVirglContextFromListUnsafe(VirglContextId);
    SpinUnLock(savedIrql, &VirglContextListSpinLock);

    return virglContext;
}

PVGPU_MEMORY_NODE DeleteVgpuMemoryNodeFromList(PVIRGL_CONTEXT VirglContext, PVOID UserAddress)
{
    KIRQL               savedIrql;
    PVGPU_MEMORY_NODE   memoryNode;

    SpinLock(&savedIrql, &VirglContext->VgpuMemoryNodeListSpinLock);
    memoryNode = GetVgpuMemoryNodeFromListUnsafe(VirglContext, UserAddress);
    if (memoryNode)
    {
        RemoveEntryListUnsafe(&memoryNode->Entry);
    }
    SpinUnLock(savedIrql, &VirglContext->VgpuMemoryNodeListSpinLock);

    return memoryNode;
}

PVIRGL_RESOURCE GetResourceFromList(PVIRGL_CONTEXT VirglContext, ULONG32 Id)
{
    KIRQL           savedIrql;
    PVIRGL_RESOURCE resource;

    SpinLock(&savedIrql, &VirglContext->ResourceListSpinLock);
    resource = GetResourceFromListUnsafe(VirglContext, Id);
    SpinUnLock(savedIrql, &VirglContext->ResourceListSpinLock);

    return resource;
}

VOID MapBlobResourceCallback(PVIRGL_CONTEXT VirglContext, ULONG32 Id, ULONG64 Gpa, SIZE_T Size)
{
    PHYSICAL_ADDRESS    phyaddr;
    PVIRGL_RESOURCE     resource;
    PVOID               mapAddress;

    resource = GetResourceFromListUnsafe(VirglContext, Id);
    if (resource)
    {
        phyaddr.QuadPart = Gpa;
        mapAddress = MmMapIoSpaceEx(phyaddr, Size, PAGE_READWRITE | PAGE_NOCACHE);
        if (!mapAddress)
        {
            VGPU_DEBUG_LOG("mapAddress failed Gpa=0x%llx", Gpa);
            return;
        }

        resource->Buffer.Size = Size;
        resource->Buffer.Memory.VirtualAddress = mapAddress;
        KeSetEvent(&resource->StateEvent, 0, FALSE);
    }
}

BOOLEAN CreateUserShareMemory(PSHARE_DESCRIPTOR ShareMemory)
{
    if (ShareMemory->KernelAddress == NULL || ShareMemory->Size == 0)
    {
        VGPU_DEBUG_LOG("params invalid address=%p size=%lld", ShareMemory->KernelAddress, ShareMemory->Size);
        return FALSE;
    }

    ShareMemory->pMdl = IoAllocateMdl(ShareMemory->KernelAddress, (ULONG32)ShareMemory->Size, FALSE, FALSE, NULL);
    if (!ShareMemory->pMdl)
    {
        VGPU_DEBUG_PRINT("IoAllocateMdl failed");
        return FALSE;
    }

    MmBuildMdlForNonPagedPool(ShareMemory->pMdl);

    try {
        ShareMemory->UserAdderss = MmMapLockedPagesSpecifyCache(ShareMemory->pMdl, UserMode, MmCached, NULL, FALSE, NormalPagePriority);
    } except(EXCEPTION_EXECUTE_HANDLER) {
        IoFreeMdl(ShareMemory->pMdl);
        VGPU_DEBUG_PRINT("except: create share memory with user failed");
        return FALSE;
    }

    if (!ShareMemory->UserAdderss)
    {
        IoFreeMdl(ShareMemory->pMdl);
        VGPU_DEBUG_PRINT("MmMapLockedPagesSpecifyCache failed");
        return FALSE;
    }

    return TRUE;
}

VOID DeleteUserShareMemory(PSHARE_DESCRIPTOR ShareMemory)
{
    if (ShareMemory->UserAdderss == NULL || ShareMemory->pMdl == NULL)
    {
        return;
    }

    MmUnmapLockedPages(ShareMemory->UserAdderss, ShareMemory->pMdl);
    IoFreeMdl(ShareMemory->pMdl);
}

VOID DeleteResource(PVIRGL_CONTEXT VirglContext, PVIRGL_RESOURCE Resource)
{
    if (Resource->bForBuffer)
    {
        if (Resource->Buffer.Share.pMdl)
        {
            DeleteUserShareMemory(&Resource->Buffer.Share);
        }

        if (Resource->bForBlob)
        {
            UnMapBlobResource(VirglContext->DeviceContext, VirglContext->Id, Resource->Id, 0);
            MmUnmapIoSpace(Resource->Buffer.Memory.VirtualAddress, Resource->Buffer.Size);
        }
        else
        {
            DetachResourceBacking(VirglContext->DeviceContext, Resource);
            FreeVgpuMemory(Resource->Buffer.Memory.VirtualAddress, Resource->Buffer.Size);
        }
    }

    UnrefResource(VirglContext->DeviceContext, VirglContext->Id, Resource->Id);
}

NTSTATUS CtlGetParams(IN PDEVICE_CONTEXT Context, IN WDFREQUEST Request, IN size_t OutputBufferLength, IN size_t InputBufferLength, OUT size_t* bytesReturn)
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    ULONG64* input = NULL, * output = NULL;

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
    NTSTATUS                                status;
    PVIRGL_CONTEXT                          virglContext;
    WDFMEMORY                               contextParamMem;
    ULONG                                   contextInit = 0;
    ULONG                                   contextId;
    struct drm_virtgpu_context_init*        init;
    struct drm_virtgpu_context_set_param*   params;

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
    KeInitializeSpinLock(&virglContext->VgpuMemoryNodeListSpinLock);
    InitializeListHead(&virglContext->ResourceList);
    InitializeListHead(&virglContext->VgpuMemoryNodeList);
    ExInterlockedInsertHeadList(&VirglContextList, &virglContext->Entry, &VirglContextListSpinLock);

    CreateVirglContext(Context, contextId, contextInit);
    VGPU_DEBUG_LOG("create virgl context id=%d", contextId);

    return status;
}

NTSTATUS CtlDestroyVirglContext(IN PVIRGL_CONTEXT VirglContext)
{
    KIRQL               savedIrql;
    PLIST_ENTRY         item;
    PVIRGL_RESOURCE     resource;
    PVGPU_MEMORY_NODE   memoryNode;

    SpinLock(&savedIrql, &VirglContextListSpinLock);
    RemoveEntryListUnsafe(&VirglContext->Entry);
    SpinUnLock(savedIrql, &VirglContextListSpinLock);

    SpinLock(&savedIrql, &VirglContext->ResourceListSpinLock);
    while (!IsListEmpty(&VirglContext->ResourceList))
    {
        item = RemoveTailList(&VirglContext->ResourceList);
        resource = CONTAINING_RECORD(item, VIRGL_RESOURCE, Entry);

        DeleteResource(VirglContext, resource);
        ExFreeToLookasideListEx(&VirglContext->DeviceContext->VirglResourceLookAsideList, resource);
    }
    SpinUnLock(savedIrql, &VirglContext->ResourceListSpinLock);

    SpinLock(&savedIrql, &VirglContext->VgpuMemoryNodeListSpinLock);
    for (item = VirglContext->VgpuMemoryNodeList.Flink; item != &VirglContext->VgpuMemoryNodeList; item = item->Flink)
    {
        item = RemoveTailList(&VirglContext->VgpuMemoryNodeList);
        memoryNode = CONTAINING_RECORD(item, VGPU_MEMORY_NODE, Entry);

        DeleteUserShareMemory(&memoryNode->Buffer.Share);
        FreeVgpuMemory(memoryNode->Buffer.Memory.VirtualAddress, memoryNode->Buffer.Size);
        ExFreeToLookasideListEx(&VirglContext->DeviceContext->VgpuMemoryNodeLookAsideList, memoryNode);
    }
    SpinUnLock(savedIrql, &VirglContext->VgpuMemoryNodeListSpinLock);

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

    virglContext = GetVirglContextFromListUnsafe(HandleToULong(PsGetCurrentProcessId()));
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
    resource->bForBlob = FALSE;

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

        if (!AllocateVgpuMemory(resource->Buffer.Size, &resource->Buffer.Memory))
        {
            VGPU_DEBUG_PRINT("allocate dma memory failed");
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        // clear memory to avoid crash in cinema4d sometime
        RtlZeroMemory(resource->Buffer.Memory.VirtualAddress, resource->Buffer.Size);
        AttachResourceBacking(virglContext->DeviceContext, virglContext->Id, resource);
    }

    // insert to the resource list
    ExInterlockedInsertHeadList(&virglContext->ResourceList, &resource->Entry, &virglContext->ResourceListSpinLock);

    // different from gem object in linux
    pCreateResourceResp->res_handle = pCreateResourceResp->bo_handle = resource->Id;
    VGPU_DEBUG_LOG("create resource id=%d bForBuffer=%d size=%d", resource->Id, resource->bForBuffer, pCreateResource->size);

    return status;
}

NTSTATUS CtlCreateBlobResource(IN WDFREQUEST Request, IN size_t OutputBufferLength, IN size_t InputBufferLength, OUT size_t* bytesReturn)
{
    NTSTATUS                                    status;
    PVIRGL_RESOURCE                             resource;
    PVIRGL_CONTEXT                              virglContext;
    VIRTGPU_BLOB_RESOURCE_CREATE_PARAM          create;
    struct drm_virtgpu_resource_create_blob*    pCreateResourceBlob;
    struct drm_virtgpu_resource_create_resp*    pCreateResourceResp;

    status = WdfRequestRetrieveInputBuffer(Request, InputBufferLength, &pCreateResourceBlob, bytesReturn);
    if (!NT_SUCCESS(status))
    {
        VGPU_DEBUG_LOG("WdfRequestRetrieveInputBuffer failed status=0x%08x", status);
        return status;
    }

    if (*bytesReturn != sizeof(struct drm_virtgpu_resource_create_blob))
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

    virglContext = GetVirglContextFromListUnsafe(HandleToULong(PsGetCurrentProcessId()));
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

    resource->bForBlob = TRUE;
    resource->bForBuffer = TRUE;
    resource->Buffer.Share.pMdl = NULL;
    resource->FenceId = 0;
    GetIdFromIdrWithoutCache(VIRGL_RESOURCE_ID_TYPE, &resource->Id, sizeof(ULONG32));

    // initialize blob resource as busy until map blob callback was completed
    KeInitializeEvent(&resource->StateEvent, NotificationEvent, FALSE);

    create.nr_entries = 0;
    create.blob_flags = pCreateResourceBlob->blob_flags;
    create.blob_id = pCreateResourceBlob->blob_id;
    create.blob_mem = pCreateResourceBlob->blob_mem;
    create.size = ROUND_UP(pCreateResourceBlob->size, PAGE_SIZE);
    create.format = pCreateResourceBlob->format;
    create.bind = pCreateResourceBlob->bind;
    create.target = pCreateResourceBlob->target;
    create.width = pCreateResourceBlob->width;
    create.height = pCreateResourceBlob->height;
    create.depth = pCreateResourceBlob->depth;
    create.array_size = pCreateResourceBlob->array_size;
    create.last_level = pCreateResourceBlob->last_level;
    create.nr_samples = pCreateResourceBlob->nr_samples;
    create.flags = pCreateResourceBlob->flags;
    CreateBlobResource(virglContext->DeviceContext, virglContext->Id, resource->Id, &create, 0);

    // insert into resource list before map it
    ExInterlockedInsertHeadList(&virglContext->ResourceList, &resource->Entry, &virglContext->ResourceListSpinLock);

    // map blob resource
    MapBlobResource(virglContext->DeviceContext, virglContext->Id, resource->Id, 0);

    pCreateResourceResp->res_handle = pCreateResourceResp->bo_handle = resource->Id;
    VGPU_DEBUG_LOG("create blob resource id=%d size=%d", resource->Id, pCreateResourceBlob->size);

    return STATUS_SUCCESS;
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

    virglContext = GetVirglContextFromListUnsafe(HandleToULong(PsGetCurrentProcessId()));
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
    RemoveEntryListUnsafe(&resource->Entry);
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

    virglContext = GetVirglContextFromListUnsafe(HandleToULong(PsGetCurrentProcessId()));
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
        status = KeWaitForSingleObject(&resource->StateEvent, Executive, KernelMode, FALSE, NULL);
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

    virglContext = GetVirglContextFromListUnsafe(HandleToULong(PsGetCurrentProcessId()));
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
        // wait for resource to be idle
        if (!KeReadStateEvent(&resource->StateEvent))
        {
            KeWaitForSingleObject(&resource->StateEvent, Executive, KernelMode, FALSE, NULL);
        }

        resource->Buffer.Share.KernelAddress = resource->Buffer.Memory.VirtualAddress;
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

NTSTATUS CtlTransferHost(IN BOOLEAN ToHost, IN WDFREQUEST Request, IN size_t InputBufferLength, OUT size_t* bytesReturn)
{
    NTSTATUS                        status;
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

    virglContext = GetVirglContextFromListUnsafe(HandleToULong(PsGetCurrentProcessId()));
    if (!virglContext)
    {
        VGPU_DEBUG_PRINT("get virgl context failed");
        return STATUS_UNSUCCESSFUL;
    }

    resource = GetResourceFromListUnsafe(virglContext, cmd->bo_handle);
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

    UpdateResourceState(virglContext, &resource->Id, 1, TRUE, 0);
    TransferHost3D(virglContext->DeviceContext, virglContext->Id, &transfer3d, 0, ToHost);

    return status;
}

NTSTATUS CtlSubmitCommand(IN WDFREQUEST Request, IN size_t InputBufferLength, OUT size_t* bytesReturn)
{
    NTSTATUS                        status;
    ULONG64                         fenceId;
    PVOID                           inFence = NULL;
    PVOID                           outFence = NULL;
    PVOID                           boHandles = NULL;
    SIZE_T                          boHandlesSize;
    SIZE_T                          alignSize;
    PVIRGL_CONTEXT                  virglContext;
    PVGPU_MEMORY_NODE               memoryNode;
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

    virglContext = GetVirglContextFromListUnsafe(HandleToULong(PsGetCurrentProcessId()));
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

    memoryNode = DeleteVgpuMemoryNodeFromList(virglContext, (PVOID)cmd->command);
    if (!memoryNode)
    {
        VGPU_DEBUG_PRINT("get vgpu memory node failed");
        return STATUS_UNSUCCESSFUL;
    }

    // delete user space mdl now because mesa icd will create new command buffer and this memory node would never be accessed by user space again
    // this memory node would be freed in VIRTIO_GPU_CMD_SUBMIT_3D callback
    DeleteUserShareMemory(&memoryNode->Buffer.Share);

    // get new fence
    GetIdFromIdrWithoutCache(FENCE_ID_TYPE, &fenceId, sizeof(ULONG64));

    if (cmd->num_bo_handles > 0)
    {
        boHandles = (PUINT8)memoryNode->Buffer.Memory.VirtualAddress + cmd->size;
        boHandlesSize = cmd->num_bo_handles * sizeof(ULONG32);

        // move bohandles buffer to the end of cmd buffer
        memmove(boHandles, (PUINT8)memoryNode->Buffer.Memory.VirtualAddress + (cmd->bo_handles - cmd->command), boHandlesSize);

        // make all resources referenced busy
        UpdateResourceState(virglContext, boHandles, cmd->num_bo_handles, TRUE, fenceId);

        // reset current node size
        alignSize = ROUND_UP(cmd->size + boHandlesSize, PAGE_SIZE);
        if (ReallocVgpuMemory(memoryNode->Buffer.Memory.VirtualAddress, memoryNode->Buffer.Size, alignSize))
        {
            memoryNode->Buffer.Size = alignSize;
        }
    }
    else
    {
        // reset current node size
        alignSize = ROUND_UP(cmd->size, PAGE_SIZE);
        if (ReallocVgpuMemory(memoryNode->Buffer.Memory.VirtualAddress, memoryNode->Buffer.Size, alignSize))
        {
            memoryNode->Buffer.Size = alignSize;
        }
    }

    return SubmitCommand(virglContext->DeviceContext, virglContext->Id, memoryNode, cmd->size, boHandles, cmd->num_bo_handles, fenceId, outFence);
}

NTSTATUS CtlAllocateVgpuMemory(IN WDFREQUEST Request, IN size_t OutputBufferLength, IN size_t InputBufferLength, OUT size_t* bytesReturn)
{
    NTSTATUS                                        status;
    PVIRGL_CONTEXT                                  virglContext;
    PVGPU_MEMORY_NODE                               memoryNode;
    struct drm_virtgpu_vgpu_memory_allocate*        cmd;
    struct drm_virtgpu_vgpu_memory_allocate_resp*   resp;

    status = WdfRequestRetrieveInputBuffer(Request, InputBufferLength, &cmd, bytesReturn);
    if (!NT_SUCCESS(status))
    {
        VGPU_DEBUG_LOG("WdfRequestRetrieveInputBuffer failed status=0x%08x", status);
        return status;
    }

    if (*bytesReturn != sizeof(struct drm_virtgpu_vgpu_memory_allocate))
    {
        VGPU_DEBUG_LOG("get wrong buffer size=%lld", *bytesReturn);
        return STATUS_UNSUCCESSFUL;
    }

    status = WdfRequestRetrieveOutputBuffer(Request, OutputBufferLength, &resp, bytesReturn);
    if (!NT_SUCCESS(status))
    {
        VGPU_DEBUG_LOG("WdfRequestRetrieveOutputBuffer failed status=0x%08x", status);
        return status;
    }

    if (*bytesReturn != sizeof(struct drm_virtgpu_vgpu_memory_allocate_resp))
    {
        VGPU_DEBUG_LOG("get wrong buffer size=%lld", *bytesReturn);
        return STATUS_UNSUCCESSFUL;
    }

    virglContext = GetVirglContextFromListUnsafe(HandleToULong(PsGetCurrentProcessId()));
    if (!virglContext)
    {
        VGPU_DEBUG_PRINT("get virgl context failed");
        return STATUS_UNSUCCESSFUL;
    }

    memoryNode = ExAllocateFromLookasideListEx(&virglContext->DeviceContext->VgpuMemoryNodeLookAsideList);
    if (!memoryNode)
    {
        VGPU_DEBUG_PRINT("allocate memory failed");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    memoryNode->Buffer.Size = ROUND_UP(cmd->size, PAGE_SIZE);
    if (!AllocateVgpuMemory(memoryNode->Buffer.Size, &memoryNode->Buffer.Memory))
    {
        ExFreeToLookasideListEx(&virglContext->DeviceContext->VgpuMemoryNodeLookAsideList, memoryNode);
        VGPU_DEBUG_PRINT("allocate memory failed");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    memoryNode->Buffer.Share.Size = memoryNode->Buffer.Size;
    memoryNode->Buffer.Share.KernelAddress = memoryNode->Buffer.Memory.VirtualAddress;
    if (!CreateUserShareMemory(&memoryNode->Buffer.Share))
    {
        FreeVgpuMemory(memoryNode->Buffer.Memory.VirtualAddress, memoryNode->Buffer.Size);
        ExFreeToLookasideListEx(&virglContext->DeviceContext->VgpuMemoryNodeLookAsideList, memoryNode);
        VGPU_DEBUG_PRINT("create use share memory failed");
        return STATUS_UNSUCCESSFUL;
    }

    ExInterlockedInsertHeadList(&virglContext->VgpuMemoryNodeList, &memoryNode->Entry, &virglContext->VgpuMemoryNodeListSpinLock);
    resp->address = (ULONG64)memoryNode->Buffer.Share.UserAdderss;

    return status;
}

NTSTATUS CtlFreeVgpuMemory(IN WDFREQUEST Request, IN size_t InputBufferLength, OUT size_t* bytesReturn)
{
    NTSTATUS                                status;
    PVIRGL_CONTEXT                          virglContext;
    PVGPU_MEMORY_NODE                       memoryNode;
    struct drm_virtgpu_vgpu_memory_free*    cmd;

    status = WdfRequestRetrieveInputBuffer(Request, InputBufferLength, &cmd, bytesReturn);
    if (!NT_SUCCESS(status))
    {
        VGPU_DEBUG_LOG("WdfRequestRetrieveInputBuffer failed status=0x%08x", status);
        return status;
    }

    if (*bytesReturn != sizeof(struct drm_virtgpu_vgpu_memory_free))
    {
        VGPU_DEBUG_LOG("get wrong buffer size=%lld", *bytesReturn);
        return STATUS_UNSUCCESSFUL;
    }

    virglContext = GetVirglContextFromListUnsafe(HandleToULong(PsGetCurrentProcessId()));
    if (!virglContext)
    {
        VGPU_DEBUG_PRINT("get virgl context failed");
        return STATUS_UNSUCCESSFUL;
    }

    memoryNode = DeleteVgpuMemoryNodeFromList(virglContext, (PVOID)cmd->address);
    if (!memoryNode)
    {
        VGPU_DEBUG_PRINT("get vgpu memory node failed");
        return STATUS_UNSUCCESSFUL;
    }

    DeleteUserShareMemory(&memoryNode->Buffer.Share);
    FreeVgpuMemory(memoryNode->Buffer.Memory.VirtualAddress, memoryNode->Buffer.Size);
    ExFreeToLookasideListEx(&virglContext->DeviceContext->VgpuMemoryNodeLookAsideList, memoryNode);

    return status;
}
