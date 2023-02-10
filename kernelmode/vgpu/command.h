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

    /* 3d commands */
    VIRTIO_GPU_CMD_CTX_CREATE = 0x0200,
    VIRTIO_GPU_CMD_CTX_DESTROY,
    VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE,
    VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE,
    VIRTIO_GPU_CMD_RESOURCE_CREATE_3D,
    VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D,
    VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D,
    VIRTIO_GPU_CMD_SUBMIT_3D,

    /* cursor commands */
    VIRTIO_GPU_CMD_UPDATE_CURSOR = 0x0300,
    VIRTIO_GPU_CMD_MOVE_CURSOR,

    /* success responses */
    VIRTIO_GPU_RESP_OK_NODATA = 0x1100,
    VIRTIO_GPU_RESP_OK_DISPLAY_INFO,
    VIRTIO_GPU_RESP_OK_CAPSET_INFO,
    VIRTIO_GPU_RESP_OK_CAPSET,
    VIRTIO_GPU_RESP_OK_EDID,


    /* error responses */
    VIRTIO_GPU_RESP_ERR_UNSPEC = 0x1200,
    VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY,
    VIRTIO_GPU_RESP_ERR_INVALID_SCANOUT_ID,
    VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID,
    VIRTIO_GPU_RESP_ERR_INVALID_CONTEXT_ID,
    VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER,
};

// make 1byte alignment to communicate with host
#pragma pack(push)
#pragma pack(1)

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


    // for migration
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

#pragma warning(disable:4200)
/* VIRTIO_GPU_RESP_OK_CAPSET */
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

// pop back alignment
#pragma pack(pop)


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

VOID GetCapsInfo(PDEVICE_CONTEXT Context);
VOID GetCaps(PDEVICE_CONTEXT Context, INT32 CapsIndex, UINT32 CapsVer, PVOID pCaps);
VOID CreateVirglContext(PDEVICE_CONTEXT Context, ULONG32 VirglContextId);
VOID DestroyVirglContext(PDEVICE_CONTEXT Context, ULONG32 VirglContextId);
VOID FreeCommandBuffer(PDEVICE_CONTEXT Context, PVGPU_BUFFER pBuffer);
VOID Create2DResource(PDEVICE_CONTEXT Context, ULONG32 VirglContextId, ULONG32 ResourceId, PVIRTGPU_RESOURCE_CREATE_PARAM Create, ULONG64 FenceId);
VOID Create3DResource(PDEVICE_CONTEXT Context, ULONG32 VirglContextId, ULONG32 ResourceId, PVIRTGPU_RESOURCE_CREATE_PARAM Create, ULONG64 FenceId);
VOID AttachResourceBacking(PDEVICE_CONTEXT Context, ULONG32 VirglContextId, PVIRGL_RESOURCE Resource);
VOID DetachResourceBacking(PDEVICE_CONTEXT Context, PVIRGL_RESOURCE Resource);
VOID AttachResource(PDEVICE_CONTEXT Context, ULONG32 VirglContextId, ULONG32 ResourceId);
VOID DetachResource(PDEVICE_CONTEXT Context, ULONG32 VirglContextId, ULONG32 ResourceId);
VOID UnrefResource(PDEVICE_CONTEXT Context, ULONG32 VirglContextId, ULONG32 ResourceId);
VOID TransferToHost2D(PDEVICE_CONTEXT Context, ULONG32 VirglContextId, PVIRTGPU_TRANSFER_HOST_2D_PARAM Transfer);
VOID TransferHost3D(PDEVICE_CONTEXT Context, ULONG32 VirglContextId, PVIRTGPU_TRANSFER_HOST_3D_PARAM Transfer, ULONG64 FenceId, BOOLEAN ToHost);
VOID SubmitCommand(PDEVICE_CONTEXT Context, ULONG32 VirglContextId, PVGPU_MEMORY_DESCRIPTOR Command, SIZE_T Size, PVOID Extend, SIZE_T ExtendSize, ULONG64 FenceId);