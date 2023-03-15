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

#include <virtio_pci.h>
#include <virtio.h>
#include <VirtIO.h>

#define VIRTIO_GPU_FLAG_FENCE (1 << 0)

enum virtio_gpu_ctrl_type {
    VIRTIO_GPU_UNDEFINED = 0,

    /* 2d commands */
    VIRTIO_GPU_CMD_GET_DISPLAY_INFO = 0x0100,
    VIRTIO_GPU_CMD_RESOURCE_CREATE_2D,
    VIRTIO_GPU_CMD_RESOURCE_UNREF,
    VIRTIO_GPU_CMD_SET_SCANOUT,
    VIRTIO_GPU_CMD_RESOURCE_FLUSH,
    VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D,
    VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING,
    VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING,
    VIRTIO_GPU_CMD_GET_CAPSET_INFO,
    VIRTIO_GPU_CMD_GET_CAPSET,
    VIRTIO_GPU_CMD_GET_EDID,
    VIRTIO_GPU_CMD_RESOURCE_ASSIGN_UUID,
    VIRTIO_GPU_CMD_RESOURCE_CREATE_BLOB,
    VIRTIO_GPU_CMD_SET_SCANOUT_BLOB,

    /* 3d commands */
    VIRTIO_GPU_CMD_CTX_CREATE = 0x0200,
    VIRTIO_GPU_CMD_CTX_DESTROY,
    VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE,
    VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE,
    VIRTIO_GPU_CMD_RESOURCE_CREATE_3D,
    VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D,
    VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D,
    VIRTIO_GPU_CMD_SUBMIT_3D,
    VIRTIO_GPU_CMD_RESOURCE_MAP_BLOB,
    VIRTIO_GPU_CMD_RESOURCE_UNMAP_BLOB,

    /* cursor commands */
    VIRTIO_GPU_CMD_UPDATE_CURSOR = 0x0300,
    VIRTIO_GPU_CMD_MOVE_CURSOR,

    /* success responses */
    VIRTIO_GPU_RESP_OK_NODATA = 0x1100,
    VIRTIO_GPU_RESP_OK_DISPLAY_INFO,
    VIRTIO_GPU_RESP_OK_CAPSET_INFO,
    VIRTIO_GPU_RESP_OK_CAPSET,
    VIRTIO_GPU_RESP_OK_EDID,
    VIRTIO_GPU_RESP_OK_RESOURCE_UUID,
    VIRTIO_GPU_RESP_OK_MAP_INFO,

    /* error responses */
    VIRTIO_GPU_RESP_ERR_UNSPEC = 0x1200,
    VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY,
    VIRTIO_GPU_RESP_ERR_INVALID_SCANOUT_ID,
    VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID,
    VIRTIO_GPU_RESP_ERR_INVALID_CONTEXT_ID,
    VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER,
};

#pragma pack (push)
#pragma pack (1)

struct virtio_gpu_ctrl_hdr {
    __le32 type;
    __le32 flags;
    __le64 fence_id;
    __le32 ctx_id;
    __u8 ring_idx;
    __u8 padding[3];
};

/* VIRTIO_GPU_CMD_GET_CAPSET_INFO */
struct virtio_gpu_get_capset_info {
    struct virtio_gpu_ctrl_hdr hdr;
    __le32 capset_index;
    __le32 padding;
};

/* VIRTIO_GPU_RESP_OK_CAPSET_INFO */
struct virtio_gpu_resp_capset_info {
    struct virtio_gpu_ctrl_hdr hdr;
    __le32 capset_id;
    __le32 capset_max_version;
    __le32 capset_max_size;
    __le32 padding;
};

/* VIRTIO_GPU_CMD_CTX_CREATE */
#define VIRTIO_GPU_CONTEXT_INIT_CAPSET_ID_MASK 0x000000ff
struct virtio_gpu_ctx_create {
    struct virtio_gpu_ctrl_hdr hdr;
    __le32 nlen;
    __le32 context_init;
    char debug_name[64];
};

/* VIRTIO_GPU_CMD_RESOURCE_CREATE_3D */
#define VIRTIO_GPU_RESOURCE_FLAG_Y_0_TOP (1 << 0)
struct virtio_gpu_resource_create_3d {
    struct virtio_gpu_ctrl_hdr hdr;
    __le32 resource_id;
    __le32 target;
    __le32 format;
    __le32 bind;
    __le32 width;
    __le32 height;
    __le32 depth;
    __le32 array_size;
    __le32 last_level;
    __le32 nr_samples;
    __le32 flags;
    __le32 padding;
};

/* VIRTIO_GPU_CMD_RESOURCE_CREATE_2D: create a 2d resource with a format */
struct virtio_gpu_resource_create_2d {
    struct virtio_gpu_ctrl_hdr hdr;
    __le32 resource_id;
    __le32 format;
    __le32 width;
    __le32 height;
};

/* VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING */
struct virtio_gpu_resource_attach_backing {
    struct virtio_gpu_ctrl_hdr hdr;
    __le32 resource_id;
    __le32 nr_entries;
    __le64 gpa;
    __le32 size;
};

/* VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING */
struct virtio_gpu_resource_detach_backing {
    struct virtio_gpu_ctrl_hdr hdr;
    __le32 resource_id;
    __le32 padding;
};

/* VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE, VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE */
struct virtio_gpu_ctx_resource {
    struct virtio_gpu_ctrl_hdr hdr;
    __le32 resource_id;
    __le32 padding;
};

/* VIRTIO_GPU_CMD_RESOURCE_UNREF */
struct virtio_gpu_resource_unref {
    struct virtio_gpu_ctrl_hdr hdr;
    __le32 resource_id;
    __le32 padding;
};

/* VIRTIO_GPU_CMD_CTX_DESTROY */
struct virtio_gpu_ctx_destroy {
    struct virtio_gpu_ctrl_hdr hdr;
};

/* VIRTIO_GPU_CMD_GET_CAPSET */
struct virtio_gpu_get_capset {
    struct virtio_gpu_ctrl_hdr hdr;
    __le32 capset_id;
    __le32 capset_version;
};

/* VIRTIO_GPU_RESP_OK_CAPSET */
#pragma warning(disable:4200)
struct virtio_gpu_resp_capset {
    struct virtio_gpu_ctrl_hdr hdr;
    __u8 capset_data[];
};

struct virtio_gpu_box {
    __le32 x, y, z;
    __le32 w, h, d;
};

/* VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D, VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D */
struct virtio_gpu_transfer_host_3d {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_box box;
    __le64 offset;
    __le32 resource_id;
    __le32 level;
    __le32 stride;
    __le32 layer_stride;
};

struct virtio_gpu_rect {
    __le32 x;
    __le32 y;
    __le32 width;
    __le32 height;
};

/* VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D: simple transfer to_host */
struct virtio_gpu_transfer_to_host_2d {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    __le64 offset;
    __le32 resource_id;
    __le32 padding;
};

/* VIRTIO_GPU_CMD_SUBMIT_3D */
struct virtio_gpu_cmd_submit {
    struct virtio_gpu_ctrl_hdr hdr;
    __le32 size;
    __le32 padding;
};

/* VIRTIO_GPU_CMD_RESOURCE_CREATE_BLOB */
struct virtio_gpu_resource_create_blob {
    struct virtio_gpu_ctrl_hdr hdr;
    __le32 resource_id;
#define VIRTIO_GPU_BLOB_MEM_GUEST             0x0001
#define VIRTIO_GPU_BLOB_MEM_HOST3D            0x0002
#define VIRTIO_GPU_BLOB_MEM_HOST3D_GUEST      0x0003

#define VIRTIO_GPU_BLOB_FLAG_USE_MAPPABLE     0x0001
#define VIRTIO_GPU_BLOB_FLAG_USE_SHAREABLE    0x0002
#define VIRTIO_GPU_BLOB_FLAG_USE_CROSS_DEVICE 0x0004
    /* zero is invalid blob mem */
    __le32 blob_mem;
    __le32 blob_flags;
    __le32 nr_entries;
    __le64 blob_id;
    __le64 size;

    /* from cmd */
    __le32 format;
    __le32 bind;
    __le32 target;
    __le32 width;
    __le32 height;
    __le32 depth;
    __le32 array_size;
    __le32 last_level;
    __le32 nr_samples;
    __le32 flags;
};

/* VIRTIO_GPU_CMD_RESOURCE_MAP_BLOB */
struct virtio_gpu_resource_map_blob {
    struct virtio_gpu_ctrl_hdr hdr;
    __le32 resource_id;
    __le32 padding;
};

/* VIRTIO_GPU_RESP_OK_MAP_INFO */
#define VIRTIO_GPU_MAP_CACHE_MASK     0x0f
#define VIRTIO_GPU_MAP_CACHE_NONE     0x00
#define VIRTIO_GPU_MAP_CACHE_CACHED   0x01
#define VIRTIO_GPU_MAP_CACHE_UNCACHED 0x02
#define VIRTIO_GPU_MAP_CACHE_WC       0x03
struct virtio_gpu_resp_map_info {
    struct virtio_gpu_ctrl_hdr hdr;
    __u32 map_info;
    __le64 gpa;
    __le64 size;
    __u32 padding;
};

/* VIRTIO_GPU_CMD_RESOURCE_UNMAP_BLOB */
struct virtio_gpu_resource_unmap_blob {
    struct virtio_gpu_ctrl_hdr hdr;
    __le32 resource_id;
    __le32 padding;
};

#pragma pack (pop)

/* local use */
typedef struct _VIRTGPU_RESOURCE_CREATE_PARAM {
    __u32 target;
    __u32 format;
    __u32 bind;
    __u32 width;
    __u32 height;
    __u32 depth;
    __u32 array_size;
    __u32 last_level;
    __u32 nr_samples;
    __u32 flags;
}VIRTGPU_RESOURCE_CREATE_PARAM, * PVIRTGPU_RESOURCE_CREATE_PARAM;

typedef struct _VIRTGPU_BLOB_RESOURCE_CREATE_PARAM {
    __le32 blob_mem;
    __le32 blob_flags;
    __le32 nr_entries;
    __le64 blob_id;
    __le64 size;

    /* from cmd */
    __le32 format;
    __le32 bind;
    __le32 target;
    __le32 width;
    __le32 height;
    __le32 depth;
    __le32 array_size;
    __le32 last_level;
    __le32 nr_samples;
    __le32 flags;
}VIRTGPU_BLOB_RESOURCE_CREATE_PARAM, * PVIRTGPU_BLOB_RESOURCE_CREATE_PARAM;

typedef struct _VIRTGPU_TRANSFER_HOST_2D_PARAM {
    struct virtio_gpu_rect r;
    __le64 offset;
    __le32 resource_id;
}VIRTGPU_TRANSFER_HOST_2D_PARAM, * PVIRTGPU_TRANSFER_HOST_2D_PARAM;

typedef struct _VIRTGPU_TRANSFER_HOST_3D_PARAM {
    struct virtio_gpu_box box;
    __le64 offset;
    __le32 resource_id;
    __le32 level;
    __le32 stride;
    __le32 layer_stride;
}VIRTGPU_TRANSFER_HOST_3D_PARAM, * PVIRTGPU_TRANSFER_HOST_3D_PARAM;


FORCEINLINE VOID FreeCommandBuffer(PDEVICE_CONTEXT Context, PVGPU_BUFFER Buffer)
{
    if (Buffer->pBuf)
    {
        ExFreePoolWithTag(Buffer->pBuf, VIRTIO_VGPU_MEMORY_TAG);
    }
    ExFreeToLookasideListEx(&Context->VgpuBufferLookAsideList, Buffer);
}

VOID GetCapsInfo(PDEVICE_CONTEXT Context);
VOID GetCaps(PDEVICE_CONTEXT Context, INT32 CapsIndex, UINT32 CapsVer, PVOID pCaps);
VOID CreateVirglContext(PDEVICE_CONTEXT Context, ULONG32 VirglContextId, ULONG32 ContextInit);
VOID DestroyVirglContext(PDEVICE_CONTEXT Context, ULONG32 VirglContextId);
VOID Create2DResource(PDEVICE_CONTEXT Context, ULONG32 VirglContextId, ULONG32 ResourceId, PVIRTGPU_RESOURCE_CREATE_PARAM Create, ULONG64 FenceId);
VOID Create3DResource(PDEVICE_CONTEXT Context, ULONG32 VirglContextId, ULONG32 ResourceId, PVIRTGPU_RESOURCE_CREATE_PARAM Create, ULONG64 FenceId);
VOID CreateBlobResource(PDEVICE_CONTEXT Context, ULONG32 VirglContextId, ULONG32 ResourceId, PVIRTGPU_BLOB_RESOURCE_CREATE_PARAM Create, ULONG64 FenceId);
VOID MapBlobResource(PDEVICE_CONTEXT Context, ULONG32 VirglContextId, ULONG32 ResourceId, ULONG64 FenceId);
VOID UnMapBlobResource(PDEVICE_CONTEXT Context, ULONG32 VirglContextId, ULONG32 ResourceId, ULONG64 FenceId);
VOID AttachResourceBacking(PDEVICE_CONTEXT Context, ULONG32 VirglContextId, PVIRGL_RESOURCE Resource);
VOID DetachResourceBacking(PDEVICE_CONTEXT Context, PVIRGL_RESOURCE Resource);
VOID AttachResource(PDEVICE_CONTEXT Context, ULONG32 VirglContextId, ULONG32 ResourceId);
VOID DetachResource(PDEVICE_CONTEXT Context, ULONG32 VirglContextId, ULONG32 ResourceId);
VOID UnrefResource(PDEVICE_CONTEXT Context, ULONG32 VirglContextId, ULONG32 ResourceId);
VOID TransferToHost2D(PDEVICE_CONTEXT Context, ULONG32 VirglContextId, PVIRTGPU_TRANSFER_HOST_2D_PARAM Transfer);
VOID TransferHost3D(PDEVICE_CONTEXT Context, ULONG32 VirglContextId, PVIRTGPU_TRANSFER_HOST_3D_PARAM Transfer, ULONG64 FenceId, BOOLEAN ToHost);
NTSTATUS SubmitCommand(PDEVICE_CONTEXT Context, ULONG32 VirglContextId, PMEMORY_DESCRIPTOR Command, SIZE_T CommandBufSize, SIZE_T CommandSize,
    PVOID ResourceIds, SIZE_T ResourceIdsCount, ULONG64 FenceId, PVOID FenceObject);