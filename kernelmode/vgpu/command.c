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
#include "command.h"

#define COMMAND_QUEUE 0
#define CONTROL_QUEUE 1


NTSTATUS PushQueue(PDEVICE_CONTEXT Context,
    ULONG32 QueueIndex,
    struct scatterlist sg[],
    unsigned int out_num,
    unsigned int in_num,
    void* opaque,
    void* va_indirect,
    ULONGLONG phys_indirect)
{
    int ret;
    WdfSpinLockAcquire(Context->VirtQueueLocks[QueueIndex]);
    ret = virtqueue_add_buf(Context->VirtQueues[QueueIndex], sg, out_num, in_num, opaque, va_indirect, phys_indirect);
    WdfSpinLockRelease(Context->VirtQueueLocks[QueueIndex]);

    if (ret == 0)
    {
        virtqueue_kick(Context->VirtQueues[QueueIndex]);
        return STATUS_SUCCESS;
    }
    else
    {
        VGPU_DEBUG_LOG("virtqueue_add_buf failed ret=%d QueueIndex=%d", ret, QueueIndex);
        return STATUS_UNSUCCESSFUL;
    }
}

PVGPU_BUFFER AllocateCommandBuffer(PDEVICE_CONTEXT Context, size_t CmdSize, size_t RespSize, BOOLEAN bSync, WDFREQUEST Request)
{
    PVGPU_BUFFER buffer = ExAllocateFromLookasideListEx(&Context->VgpuBufferLookAsideList);
    ASSERT(buffer != NULL);

    buffer->pBuf = ExAllocatePool2(POOL_FLAG_NON_PAGED, CmdSize + RespSize, VIRTIO_VGPU_MEMORY_TAG);
    ASSERT(buffer->pBuf != NULL);

    if (RespSize > 0)
    {
        buffer->pRespBuf = (PUINT8)buffer->pBuf + CmdSize;
    }

    if (bSync)
    {
        KeInitializeEvent(&buffer->Event, NotificationEvent, FALSE);
    }

    if (Request)
    {
        buffer->Request = Request;
    }

    return buffer;
}

UINT32 BuildSGElement(struct VirtIOBufferDescriptor* pSgList, SIZE_T MaxSgCount, PUINT8 pBuf, SIZE_T Size)
{
    ULONG   length;
    UINT32  sgCount = 0;

    while (Size)
    {
        if (sgCount > MaxSgCount)
        {
            VGPU_DEBUG_PRINT("sg overflow");
            return 0;
        }

        length = (ULONG)min(Size, PAGE_SIZE);
        pSgList[sgCount].length = length;
        pSgList[sgCount].physAddr = MmGetPhysicalAddress(pBuf);

        pBuf += length;
        Size -= length;
        sgCount++;
    }

    return sgCount;
}

VOID GetCapsInfo(PDEVICE_CONTEXT Context)
{
    UINT32 outNum, inNum;
    struct VirtIOBufferDescriptor sg[SGLIST_SIZE];

    for (ULONG i = 0; i < Capsets.NumCaps; i++)
    {
        PVGPU_BUFFER buffer = AllocateCommandBuffer(Context, sizeof(struct virtio_gpu_get_capset_info), sizeof(struct virtio_gpu_resp_capset_info), TRUE, NULL);
        struct virtio_gpu_get_capset_info* cmd = buffer->pBuf;
        struct virtio_gpu_resp_capset_info* resp = buffer->pRespBuf;

        cmd->hdr.type = VIRTIO_GPU_CMD_GET_CAPSET_INFO;
        cmd->capset_index = i;

        outNum = BuildSGElement(&sg[0], SGLIST_SIZE, (PUINT8)cmd, sizeof(*cmd));
        inNum = BuildSGElement(&sg[outNum], SGLIST_SIZE - outNum, (PUINT8)resp, sizeof(*resp));

        PushQueue(Context, CONTROL_QUEUE, sg, outNum, inNum, buffer, NULL, 0);

        KeWaitForSingleObject(&buffer->Event, Executive, KernelMode, FALSE, NULL);
        ASSERT(resp->hdr.type == VIRTIO_GPU_RESP_OK_CAPSET_INFO);

        Capsets.Data[i].id = resp->capset_id;
        Capsets.Data[i].max_size = resp->capset_max_size;
        Capsets.Data[i].max_version = resp->capset_max_version;
        Capsets.CapsetIdMask |= (1LL << resp->capset_id);
        FreeCommandBuffer(Context, buffer);
    }
}

VOID GetCaps(PDEVICE_CONTEXT Context, INT32 CapsIndex, UINT32 CapsVer, PVOID pCaps)
{
    struct VirtIOBufferDescriptor sg[SGLIST_SIZE];
    UINT32 outNum, inNum;
    size_t resp_size = sizeof(struct virtio_gpu_resp_capset) + Capsets.Data[CapsIndex].max_size;

    PVGPU_BUFFER buffer = AllocateCommandBuffer(Context, sizeof(struct virtio_gpu_get_capset), resp_size, TRUE, NULL);
    struct virtio_gpu_get_capset* cmd = buffer->pBuf;
    struct virtio_gpu_resp_capset* resp = buffer->pRespBuf;

    cmd->hdr.type = VIRTIO_GPU_CMD_GET_CAPSET;
    cmd->capset_id = Capsets.Data[CapsIndex].id;
    cmd->capset_version = CapsVer;

    outNum = BuildSGElement(&sg[0], SGLIST_SIZE, (PUINT8)cmd, sizeof(*cmd));
    inNum = BuildSGElement(&sg[outNum], SGLIST_SIZE - outNum, (PUINT8)resp, resp_size);

    PushQueue(Context, CONTROL_QUEUE, sg, outNum, inNum, buffer, NULL, 0);

    KeWaitForSingleObject(&buffer->Event, Executive, KernelMode, FALSE, NULL);
    ASSERT(resp->hdr.type == VIRTIO_GPU_RESP_OK_CAPSET);

    RtlCopyMemory(pCaps, resp->capset_data, Capsets.Data[CapsIndex].max_size);
    FreeCommandBuffer(Context, buffer);
}

VOID CreateVirglContext(PDEVICE_CONTEXT Context, ULONG32 VirglContextId, ULONG32 ContextInit)
{
    struct VirtIOBufferDescriptor sg[SGLIST_SIZE];
    UINT32 outNum;

    PVGPU_BUFFER buffer = AllocateCommandBuffer(Context, sizeof(struct virtio_gpu_ctx_create), 0, FALSE, NULL);
    struct virtio_gpu_ctx_create* cmd = buffer->pBuf;

    cmd->hdr.ctx_id = VirglContextId;
    cmd->hdr.type = VIRTIO_GPU_CMD_CTX_CREATE;
    cmd->context_init = ContextInit;

    STRING debugName = RTL_CONSTANT_STRING("tenclass");
    RtlCopyMemory(&cmd->debug_name, debugName.Buffer, debugName.Length);
    cmd->nlen = debugName.Length;

    outNum = BuildSGElement(&sg[0], SGLIST_SIZE, (PUINT8)cmd, sizeof(*cmd));

    PushQueue(Context, CONTROL_QUEUE, sg, outNum, 0, buffer, NULL, 0);
}

VOID DestroyVirglContext(PDEVICE_CONTEXT Context, ULONG32 VirglContextId)
{
    struct VirtIOBufferDescriptor sg[SGLIST_SIZE];
    UINT32 outNum;

    PVGPU_BUFFER buffer = AllocateCommandBuffer(Context, sizeof(struct virtio_gpu_ctx_destroy), 0, FALSE, NULL);
    struct virtio_gpu_ctx_destroy* cmd = buffer->pBuf;

    cmd->hdr.ctx_id = VirglContextId;
    cmd->hdr.type = VIRTIO_GPU_CMD_CTX_DESTROY;

    outNum = BuildSGElement(&sg[0], SGLIST_SIZE, (PUINT8)cmd, sizeof(*cmd));

    PushQueue(Context, CONTROL_QUEUE, sg, outNum, 0, buffer, NULL, 0);
}

VOID TransferToHost2D(PDEVICE_CONTEXT Context, ULONG32 VirglContextId, PVIRTGPU_TRANSFER_HOST_2D_PARAM Transfer)
{
    struct VirtIOBufferDescriptor sg[SGLIST_SIZE];
    UINT32 outNum;

    PVGPU_BUFFER buffer = AllocateCommandBuffer(Context, sizeof(struct virtio_gpu_transfer_to_host_2d), 0, FALSE, NULL);
    struct virtio_gpu_transfer_to_host_2d* cmd = buffer->pBuf;

    cmd->hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    cmd->hdr.ctx_id = VirglContextId;
    cmd->resource_id = Transfer->resource_id;

    cmd->offset = Transfer->offset;
    cmd->r.x = Transfer->r.x;
    cmd->r.y = Transfer->r.y;
    cmd->r.width = Transfer->r.width;
    cmd->r.height = Transfer->r.height;

    outNum = BuildSGElement(&sg[0], SGLIST_SIZE, (PUINT8)cmd, sizeof(*cmd));

    PushQueue(Context, COMMAND_QUEUE, sg, outNum, 0, buffer, NULL, 0);
}

VOID TransferHost3D(PDEVICE_CONTEXT Context, ULONG32 VirglContextId, PVIRTGPU_TRANSFER_HOST_3D_PARAM Transfer, ULONG64 FenceId, BOOLEAN ToHost)
{
    struct VirtIOBufferDescriptor sg[SGLIST_SIZE];
    UINT32 outNum;

    PVGPU_BUFFER buffer = AllocateCommandBuffer(Context, sizeof(struct virtio_gpu_transfer_host_3d), 0, FALSE, NULL);
    struct virtio_gpu_transfer_host_3d* cmd = buffer->pBuf;

    cmd->hdr.type = ToHost ? VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D : VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D;
    cmd->hdr.ctx_id = VirglContextId;
    cmd->resource_id = Transfer->resource_id;

    cmd->box.x = Transfer->box.x;
    cmd->box.y = Transfer->box.y;
    cmd->box.z = Transfer->box.z;
    cmd->box.w = Transfer->box.w;
    cmd->box.h = Transfer->box.h;
    cmd->box.d = Transfer->box.d;

    cmd->layer_stride = Transfer->layer_stride;
    cmd->level = Transfer->level;
    cmd->offset = Transfer->offset;
    cmd->stride = Transfer->stride;

    if (FenceId > 0)
    {
        cmd->hdr.flags |= VIRTIO_GPU_FLAG_FENCE;
        cmd->hdr.fence_id = FenceId;
    }

    outNum = BuildSGElement(&sg[0], SGLIST_SIZE, (PUINT8)cmd, sizeof(*cmd));

    PushQueue(Context, COMMAND_QUEUE, sg, outNum, 0, buffer, NULL, 0);
}

VOID Create2DResource(PDEVICE_CONTEXT Context, ULONG32 VirglContextId, ULONG32 ResourceId, PVIRTGPU_RESOURCE_CREATE_PARAM Create, ULONG64 FenceId)
{
    struct VirtIOBufferDescriptor sg[SGLIST_SIZE];
    UINT32 outNum;

    PVGPU_BUFFER buffer = AllocateCommandBuffer(Context, sizeof(struct virtio_gpu_resource_create_2d), 0, FALSE, NULL);
    struct virtio_gpu_resource_create_2d* cmd = buffer->pBuf;

    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    cmd->hdr.ctx_id = VirglContextId;
    cmd->resource_id = ResourceId;

    cmd->format = Create->format;
    cmd->width = Create->width;
    cmd->height = Create->height;

    if (FenceId > 0)
    {
        cmd->hdr.flags |= VIRTIO_GPU_FLAG_FENCE;
        cmd->hdr.fence_id = FenceId;
    }

    outNum = BuildSGElement(&sg[0], SGLIST_SIZE, (PUINT8)cmd, sizeof(*cmd));

    PushQueue(Context, COMMAND_QUEUE, sg, outNum, 0, buffer, NULL, 0);
}

VOID Create3DResource(PDEVICE_CONTEXT Context, ULONG32 VirglContextId, ULONG32 ResourceId, PVIRTGPU_RESOURCE_CREATE_PARAM Create, ULONG64 FenceId)
{
    struct VirtIOBufferDescriptor sg[SGLIST_SIZE];
    UINT32 outNum;

    PVGPU_BUFFER buffer = AllocateCommandBuffer(Context, sizeof(struct virtio_gpu_resource_create_3d), 0, FALSE, NULL);
    struct virtio_gpu_resource_create_3d* cmd = buffer->pBuf;

    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_3D;
    cmd->hdr.ctx_id = VirglContextId;
    cmd->resource_id = ResourceId;

    cmd->format = Create->format;
    cmd->width = Create->width;
    cmd->height = Create->height;
    cmd->target = Create->target;
    cmd->bind = Create->bind;
    cmd->depth = Create->depth;
    cmd->array_size = Create->array_size;
    cmd->last_level = Create->last_level;
    cmd->nr_samples = Create->nr_samples;
    cmd->flags = Create->flags;

    if (FenceId > 0)
    {
        cmd->hdr.flags |= VIRTIO_GPU_FLAG_FENCE;
        cmd->hdr.fence_id = FenceId;
    }

    outNum = BuildSGElement(&sg[0], SGLIST_SIZE, (PUINT8)cmd, sizeof(*cmd));

    PushQueue(Context, COMMAND_QUEUE, sg, outNum, 0, buffer, NULL, 0);
}

VOID CreateBlobResource(PDEVICE_CONTEXT Context, ULONG32 VirglContextId, ULONG32 ResourceId, PVIRTGPU_BLOB_RESOURCE_CREATE_PARAM Create, ULONG64 FenceId)
{
    struct VirtIOBufferDescriptor sg[SGLIST_SIZE];
    UINT32 outNum;

    PVGPU_BUFFER buffer = AllocateCommandBuffer(Context, sizeof(struct virtio_gpu_resource_create_blob), 0, FALSE, NULL);
    struct virtio_gpu_resource_create_blob* cmd = buffer->pBuf;

    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_BLOB;
    cmd->hdr.ctx_id = VirglContextId;
    cmd->resource_id = ResourceId;

    cmd->blob_mem = Create->blob_mem;
    cmd->blob_flags = Create->blob_flags;
    cmd->blob_id = Create->blob_id;
    cmd->size = Create->size;
    cmd->nr_entries = Create->nr_entries;

    cmd->format = Create->format;
    cmd->bind = Create->bind;
    cmd->target = Create->target;
    cmd->width = Create->width;
    cmd->height = Create->height;
    cmd->depth = Create->depth;
    cmd->array_size = Create->array_size;
    cmd->last_level = Create->last_level;
    cmd->nr_samples = Create->nr_samples;
    cmd->flags = Create->flags;

    if (FenceId > 0)
    {
        cmd->hdr.flags |= VIRTIO_GPU_FLAG_FENCE;
        cmd->hdr.fence_id = FenceId;
    }

    outNum = BuildSGElement(&sg[0], SGLIST_SIZE, (PUINT8)cmd, sizeof(*cmd));

    PushQueue(Context, COMMAND_QUEUE, sg, outNum, 0, buffer, NULL, 0);
}

VOID MapBlobResource(PDEVICE_CONTEXT Context, ULONG32 VirglContextId, ULONG32 ResourceId, ULONG64 FenceId)
{
    struct VirtIOBufferDescriptor sg[SGLIST_SIZE];
    UINT32 outNum, inNum;

    PVGPU_BUFFER buffer = AllocateCommandBuffer(Context, sizeof(struct virtio_gpu_resource_map_blob), sizeof(struct virtio_gpu_resp_map_info), FALSE, NULL);
    struct virtio_gpu_resource_map_blob* cmd = buffer->pBuf;
    struct virtio_gpu_resp_map_info* resp = buffer->pRespBuf;

    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_MAP_BLOB;
    cmd->hdr.ctx_id = VirglContextId;
    cmd->resource_id = ResourceId;

    if (FenceId > 0)
    {
        cmd->hdr.flags |= VIRTIO_GPU_FLAG_FENCE;
        cmd->hdr.fence_id = FenceId;
    }

    outNum = BuildSGElement(&sg[0], SGLIST_SIZE, (PUINT8)cmd, sizeof(*cmd));
    inNum = BuildSGElement(&sg[outNum], SGLIST_SIZE - outNum, (PUINT8)resp, sizeof(*resp));

    PushQueue(Context, COMMAND_QUEUE, sg, outNum, inNum, buffer, NULL, 0);
}

VOID UnMapBlobResource(PDEVICE_CONTEXT Context, ULONG32 VirglContextId, ULONG32 ResourceId, ULONG64 FenceId)
{
    struct VirtIOBufferDescriptor sg[SGLIST_SIZE];
    UINT32 outNum;

    PVGPU_BUFFER buffer = AllocateCommandBuffer(Context, sizeof(struct virtio_gpu_resource_unmap_blob), 0, FALSE, NULL);
    struct virtio_gpu_resource_unmap_blob* cmd = buffer->pBuf;

    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_UNMAP_BLOB;
    cmd->hdr.ctx_id = VirglContextId;
    cmd->resource_id = ResourceId;

    if (FenceId > 0)
    {
        cmd->hdr.flags |= VIRTIO_GPU_FLAG_FENCE;
        cmd->hdr.fence_id = FenceId;
    }

    outNum = BuildSGElement(&sg[0], SGLIST_SIZE, (PUINT8)cmd, sizeof(*cmd));

    PushQueue(Context, COMMAND_QUEUE, sg, outNum, 0, buffer, NULL, 0);
}

VOID AttachResourceBacking(PDEVICE_CONTEXT Context, ULONG32 VirglContextId, PVIRGL_RESOURCE Resource)
{
    struct VirtIOBufferDescriptor sg[SGLIST_SIZE];
    UINT32 outNum;

    PVGPU_BUFFER buffer = AllocateCommandBuffer(Context, sizeof(struct virtio_gpu_resource_attach_backing), 0, FALSE, NULL);
    struct virtio_gpu_resource_attach_backing* cmd = buffer->pBuf;

    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    cmd->hdr.ctx_id = VirglContextId;
    cmd->resource_id = Resource->Id;

    // resource use contiguous physical memory from vgpu memory
    cmd->gpa = Resource->Buffer.Memory.PhysicalAddress.QuadPart;
    cmd->size = (ULONG32)Resource->Buffer.Size;

    outNum = BuildSGElement(&sg[0], SGLIST_SIZE, (PUINT8)cmd, sizeof(*cmd));

    PushQueue(Context, COMMAND_QUEUE, sg, outNum, 0, buffer, NULL, 0);
}

VOID DetachResourceBacking(PDEVICE_CONTEXT Context, PVIRGL_RESOURCE Resource)
{
    struct VirtIOBufferDescriptor sg[SGLIST_SIZE];
    UINT32 outNum;

    PVGPU_BUFFER buffer = AllocateCommandBuffer(Context, sizeof(struct virtio_gpu_resource_detach_backing), 0, FALSE, NULL);
    struct virtio_gpu_resource_detach_backing* cmd = buffer->pBuf;

    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING;
    cmd->resource_id = Resource->Id;

    outNum = BuildSGElement(&sg[0], SGLIST_SIZE, (PUINT8)cmd, sizeof(*cmd));

    PushQueue(Context, COMMAND_QUEUE, sg, outNum, 0, buffer, NULL, 0);
}

VOID AttachResource(PDEVICE_CONTEXT Context, ULONG32 VirglContextId, ULONG32 ResourceId)
{
    struct VirtIOBufferDescriptor sg[SGLIST_SIZE];
    UINT32 outNum;

    PVGPU_BUFFER buffer = AllocateCommandBuffer(Context, sizeof(struct virtio_gpu_ctx_resource), 0, FALSE, NULL);
    struct virtio_gpu_ctx_resource* cmd = buffer->pBuf;

    cmd->hdr.type = VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE;
    cmd->hdr.ctx_id = VirglContextId;
    cmd->resource_id = ResourceId;

    outNum = BuildSGElement(&sg[0], SGLIST_SIZE, (PUINT8)cmd, sizeof(*cmd));

    PushQueue(Context, COMMAND_QUEUE, sg, outNum, 0, buffer, NULL, 0);
}

VOID DetachResource(PDEVICE_CONTEXT Context, ULONG32 VirglContextId, ULONG32 ResourceId)
{
    struct VirtIOBufferDescriptor sg[SGLIST_SIZE];
    UINT32 outNum;

    PVGPU_BUFFER buffer = AllocateCommandBuffer(Context, sizeof(struct virtio_gpu_ctx_resource), 0, FALSE, NULL);
    struct virtio_gpu_ctx_resource* cmd = buffer->pBuf;

    cmd->hdr.type = VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE;
    cmd->hdr.ctx_id = VirglContextId;
    cmd->resource_id = ResourceId;

    outNum = BuildSGElement(&sg[0], SGLIST_SIZE, (PUINT8)cmd, sizeof(*cmd));

    PushQueue(Context, COMMAND_QUEUE, sg, outNum, 0, buffer, NULL, 0);
}

VOID UnrefResource(PDEVICE_CONTEXT Context, ULONG32 VirglContextId, ULONG32 ResourceId)
{
    struct VirtIOBufferDescriptor sg[SGLIST_SIZE];
    UINT32 outNum;

    PVGPU_BUFFER buffer = AllocateCommandBuffer(Context, sizeof(struct virtio_gpu_resource_unref), 0, FALSE, NULL);
    struct virtio_gpu_resource_unref* cmd = buffer->pBuf;

    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_UNREF;
    cmd->hdr.ctx_id = VirglContextId;
    cmd->resource_id = ResourceId;

    outNum = BuildSGElement(&sg[0], SGLIST_SIZE, (PUINT8)cmd, sizeof(*cmd));

    PushQueue(Context, COMMAND_QUEUE, sg, outNum, 0, buffer, NULL, 0);
}

NTSTATUS SubmitCommand(PDEVICE_CONTEXT Context, ULONG32 VirglContextId, PMEMORY_DESCRIPTOR Command, SIZE_T CommandBufSize, SIZE_T CommandSize,
    PVOID ResourceIds, SIZE_T ResourceIdsCount, ULONG64 FenceId, PVOID FenceObject)
{
    UINT32 outNum;
    struct VirtIOBufferDescriptor sg[SGLIST_SIZE];

    PVGPU_BUFFER buffer = AllocateCommandBuffer(Context, sizeof(struct virtio_gpu_cmd_submit), 0, FALSE, NULL);
    buffer->ResourceIds = ResourceIds;
    buffer->ResourceIdsCount = ResourceIdsCount;
    buffer->pDataBuf = Command->VirtualAddress;
    buffer->DataBufSize = CommandBufSize;
    buffer->FenceObject = FenceObject;

    struct virtio_gpu_cmd_submit* cmd = buffer->pBuf;
    cmd->hdr.ctx_id = VirglContextId;
    cmd->hdr.type = VIRTIO_GPU_CMD_SUBMIT_3D;
    cmd->size = (ULONG32)CommandSize;

    if (FenceId > 0)
    {
        cmd->hdr.flags |= VIRTIO_GPU_FLAG_FENCE;
        cmd->hdr.fence_id = FenceId;
    }

    // cmd buffer use contiguous physical memory from vgpu memory
    outNum = BuildSGElement(&sg[0], SGLIST_SIZE, (PUINT8)cmd, sizeof(*cmd));
    sg[outNum].physAddr = Command->PhysicalAddress;
    sg[outNum].length = (ULONG32)CommandSize;
    outNum++;

    return PushQueue(Context, COMMAND_QUEUE, sg, outNum, 0, buffer, NULL, 0);
}