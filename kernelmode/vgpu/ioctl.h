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

#include <initguid.h> 
#include "linux/virtio_types.h"
#include "linux/types.h"

// 31c22912-7210-11ed-bf22-bce92fa2e22d
DEFINE_GUID(GUID_DEVINTERFACE_VGPU, 0x31c22912, 0x7210, 0x11ed, 0xbf, 0x22, 0xbc, 0xe9, 0x2f, 0xa2, 0xe2, 0x2d);

#define IOCTL_VIRTIO_VGPU_CONTEXT_INIT CTL_CODE(FILE_DEVICE_UNKNOWN, \
    0x800, \
    METHOD_OUT_DIRECT, \
    FILE_ANY_ACCESS)

#define IOCTL_VIRTIO_VGPU_DESTROY_CONTEXT CTL_CODE(FILE_DEVICE_UNKNOWN, \
    0x801, \
    METHOD_IN_DIRECT, \
    FILE_ANY_ACCESS)

#define IOCTL_VIRTIO_VGPU_MAP CTL_CODE(FILE_DEVICE_UNKNOWN, \
    0x802, \
    METHOD_OUT_DIRECT, \
    FILE_ANY_ACCESS)

#define IOCTL_VIRTIO_VGPU_EXECBUFFER CTL_CODE(FILE_DEVICE_UNKNOWN, \
    0x803, \
    METHOD_IN_DIRECT, \
    FILE_ANY_ACCESS)

#define IOCTL_VIRTIO_VGPU_GETPARAM CTL_CODE(FILE_DEVICE_UNKNOWN, \
    0x804, \
    METHOD_OUT_DIRECT, \
    FILE_ANY_ACCESS)

#define IOCTL_VIRTIO_VGPU_GET_CAPS CTL_CODE(FILE_DEVICE_UNKNOWN, \
    0x805, \
    METHOD_OUT_DIRECT, \
    FILE_ANY_ACCESS)

#define IOCTL_VIRTIO_VGPU_RESOURCE_CREATE CTL_CODE(FILE_DEVICE_UNKNOWN, \
    0x806, \
    METHOD_OUT_DIRECT, \
    FILE_ANY_ACCESS)

#define IOCTL_VIRTIO_VGPU_RESOURCE_CLOSE CTL_CODE(FILE_DEVICE_UNKNOWN, \
    0x807, \
    METHOD_IN_DIRECT, \
    FILE_ANY_ACCESS)

#define IOCTL_VIRTIO_VGPU_TRANSFER_FROM_HOST CTL_CODE(FILE_DEVICE_UNKNOWN, \
    0x808, \
    METHOD_IN_DIRECT, \
    FILE_ANY_ACCESS)

#define IOCTL_VIRTIO_VGPU_TRANSFER_TO_HOST CTL_CODE(FILE_DEVICE_UNKNOWN, \
    0x809, \
    METHOD_IN_DIRECT, \
    FILE_ANY_ACCESS)

#define IOCTL_VIRTIO_VGPU_WAIT CTL_CODE(FILE_DEVICE_UNKNOWN, \
    0x810, \
    METHOD_OUT_DIRECT, \
    FILE_ANY_ACCESS)

#define IOCTL_VIRTIO_VGPU_BLOB_RESOURCE_CREATE CTL_CODE(FILE_DEVICE_UNKNOWN, \
    0x811, \
    METHOD_OUT_DIRECT, \
    FILE_ANY_ACCESS)

#define IOCTL_VIRTIO_VGPU_ALLOCATE_VGPU_MEMORY CTL_CODE(FILE_DEVICE_UNKNOWN, \
    0x812, \
    METHOD_OUT_DIRECT, \
    FILE_ANY_ACCESS)

#define IOCTL_VIRTIO_VGPU_FREE_VGPU_MEMORY CTL_CODE(FILE_DEVICE_UNKNOWN, \
    0x813, \
    METHOD_IN_DIRECT, \
    FILE_ANY_ACCESS)

#define VIRTGPU_PARAM_3D_FEATURES           1 /* do we have 3D features in the hw */
#define VIRTGPU_PARAM_CAPSET_QUERY_FIX      2 /* do we have the capset fix */
#define VIRTGPU_PARAM_RESOURCE_BLOB         3 /* DRM_VIRTGPU_RESOURCE_CREATE_BLOB */
#define VIRTGPU_PARAM_HOST_VISIBLE          4 /* Host blob resources are mappable */
#define VIRTGPU_PARAM_CROSS_DEVICE          5 /* Cross virtio-device resource sharing  */
#define VIRTGPU_PARAM_CONTEXT_INIT          6 /* DRM_VIRTGPU_CONTEXT_INIT */
#define VIRTGPU_PARAM_SUPPORTED_CAPSET_IDs  7 /* Bitmask of supported capability set ids */

#define VIRTGPU_EXECBUF_FENCE_FD_IN	0x01
#define VIRTGPU_EXECBUF_FENCE_FD_OUT	0x02
#define VIRTGPU_EXECBUF_RING_IDX	0x04
#define VIRTGPU_EXECBUF_FLAGS  (\
		VIRTGPU_EXECBUF_FENCE_FD_IN |\
		VIRTGPU_EXECBUF_FENCE_FD_OUT |\
		VIRTGPU_EXECBUF_RING_IDX |\
		0)

struct drm_virtgpu_getparam {
    __u64 param;
    __u64 value;
};

#define MAX_CAPS_SIZE 2048
struct drm_virtgpu_get_caps {
    __u32 cap_set_id;
    __u32 cap_set_ver;
};

/* NO_BO flags? NO resource flag? */
/* resource flag for y_0_top */
struct drm_virtgpu_resource_create {
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
    __u32 bo_handle; /* if this is set - recreate a new resource attached to this bo ? */
    __u32 res_handle;  /* returned by kernel */
    __u32 size;        /* validate transfer in the host */
    __u32 stride;      /* validate transfer in the host */
};

struct drm_virtgpu_resource_create_resp {
    __u32 bo_handle; /* if this is set - recreate a new resource attached to this bo ? */
    __u32 res_handle;  /* returned by kernel */
};

struct drm_virtgpu_resource_create_blob {
#define VIRTGPU_BLOB_MEM_GUEST             0x0001
#define VIRTGPU_BLOB_MEM_HOST3D            0x0002
#define VIRTGPU_BLOB_MEM_HOST3D_GUEST      0x0003

#define VIRTGPU_BLOB_FLAG_USE_MAPPABLE     0x0001
#define VIRTGPU_BLOB_FLAG_USE_SHAREABLE    0x0002
#define VIRTGPU_BLOB_FLAG_USE_CROSS_DEVICE 0x0004
    /* zero is invalid blob_mem */
    __u32 blob_mem;
    __u32 blob_flags;
    __u32 bo_handle;
    __u32 res_handle;
    __u64 size;
    __u64 blob_id;

    /* from cmd */
    __u32 format;
    __u32 bind;
    __u32 target;
    __u32 width;
    __u32 height;
    __u32 depth;
    __u32 array_size;
    __u32 last_level;
    __u32 nr_samples;
    __u32 flags;
};

struct drm_virtgpu_execbuffer {
    __u32 flags;
    __u32 size;
    __u64 command; /* void* */
    __u64 bo_handles;
    __u32 num_bo_handles;
    __u32 ring_idx; /* command ring index (see VIRTGPU_EXECBUF_RING_IDX) */

    HANDLE in_fence_fd;
    HANDLE out_fence_fd;
};

/* DRM_IOCTL_GEM_CLOSE ioctl argument type */
struct drm_gem_close {
    /** Handle of the object to be closed. */
    __u32 handle;
    __u32 pad;
};

#define VIRTGPU_WAIT_NOWAIT 1 /* like it */
struct drm_virtgpu_3d_wait {
    __u32 handle; /* 0 is an invalid handle */
    __u32 flags;
};

struct drm_virtgpu_map {
    __u64 offset; /* use for mmap system call */
    __u32 handle;
    __u32 pad;
};

struct drm_virtgpu_3d_box {
    __u32 x;
    __u32 y;
    __u32 z;
    __u32 w;
    __u32 h;
    __u32 d;
};

struct drm_virtgpu_3d_transfer {
    __u32 bo_handle;
    struct drm_virtgpu_3d_box box;
    __u32 level;
    __u32 offset;
    __u32 stride;
    __u32 layer_stride;
    __u32 pad;
};

#define MAX_CAPSET_ID 63
#define VIRTGPU_CONTEXT_PARAM_CAPSET_ID       0x0001
#define VIRTGPU_CONTEXT_PARAM_NUM_RINGS       0x0002
#define VIRTGPU_CONTEXT_PARAM_POLL_RINGS_MASK 0x0003
struct drm_virtgpu_context_set_param {
    __u64 param;
    __u64 value;
};

struct drm_virtgpu_context_init {
    __u32 num_params;
    __u32 pad;

    /* pointer to drm_virtgpu_context_set_param array */
    __u64 ctx_set_params;
};

struct drm_virtgpu_vgpu_memory_allocate {
    __u64 size;
};

struct drm_virtgpu_vgpu_memory_allocate_resp {
    __u64 address;
};

struct drm_virtgpu_vgpu_memory_free {
    __u64 address;
};