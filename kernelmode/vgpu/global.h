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

#include <osdep.h>
#include <WDF/VirtIOWdf.h>
#include <stdarg.h>

#define SGLIST_SIZE             64
#define MAX_INTERRUPT_COUNT     4
#define VIRTIO_VGPU_MEMORY_TAG  ((ULONG)'upgV')
#define ROUND_UP(x, n)          (((x) + (n) - 1) & (-(n)))

#pragma pack(1)
struct virtio_vgpu_config {
    __u8  staging;
    __u8  num_queues;
    __u32 num_capsets;
    __u64 memory_size;
    __u64 capabilities;
};
#pragma pack()

typedef struct _VGPU_MEMORY_DESCRIPTOR {
    PVOID               VirtualAddress;
    PHYSICAL_ADDRESS    PhysicalAddress;
}VGPU_MEMORY_DESCRIPTOR, * PVGPU_MEMORY_DESCRIPTOR;

typedef struct _VIRTIO_GPU_DRV_CAPSET {
    ULONG32 id;
    ULONG32 max_version;
    ULONG32 max_size;
}VIRTIO_GPU_DRV_CAPSET, * PVIRTIO_GPU_DRV_CAPSET;

typedef struct _DEVICE_CONTEXT {
    VIRTIO_WDF_DRIVER       VDevice;
    UINT8                   NumVirtQueues;
    struct virtqueue**      VirtQueues;
    WDFSPINLOCK*            VirtQueueLocks;
    WDFINTERRUPT		    WdfInterrupt[MAX_INTERRUPT_COUNT];
    PVOID                   VgpuMemoryAddress;
    LOOKASIDE_LIST_EX       VirglResourceLookAsideList;
    LOOKASIDE_LIST_EX       VgpuBufferLookAsideList;
    ULONG64                 Capabilities;
} DEVICE_CONTEXT, * PDEVICE_CONTEXT;

typedef struct _CAPSETS {
    BOOLEAN                 Initialized;
    ULONG32					NumCaps;
    PVIRTIO_GPU_DRV_CAPSET	Data;
    ULONG64                 CapsetIdMask;
}CAPSETS, * PCAPSETS;

typedef struct _SHARE_DESCRIPTOR {
    PMDL                pMdl;
    SIZE_T              Size;
    PVOID               UserAdderss;
    PVOID               KennelAddress;
}SHARE_DESCRIPTOR, * PSHARE_DESCRIPTOR;

typedef struct _VIRGL_RESOURCE_BUFFER {
    SIZE_T				    Size;
    SHARE_DESCRIPTOR        Share;
    VGPU_MEMORY_DESCRIPTOR  VgpuMemory;
}VIRGL_RESOURCE_BUFFER, * PVIRGL_RESOURCE_BUFFER;

typedef struct _VIRGL_RESOURCE {
    ULONG32				    Id;
    KEVENT                  StateEvent;
    LIST_ENTRY			    Entry;
    BOOLEAN                 bForBuffer;
    ULONG64                 FenceId;
    VIRGL_RESOURCE_BUFFER   Buffer;
}VIRGL_RESOURCE, * PVIRGL_RESOURCE;

typedef struct _VIRGL_CONTEXT {
    ULONG32		    Id;
    KSPIN_LOCK	    ResourceListSpinLock;
    LIST_ENTRY	    ResourceList;
    LIST_ENTRY	    Entry;
    PDEVICE_CONTEXT DeviceContext;
}VIRGL_CONTEXT, * PVIRGL_CONTEXT;

typedef struct _VGPU_BUFFER {
    PVOID           pBuf;
    PVOID           pRespBuf;
    PVOID           pDataBuf;
    KEVENT          Event;
    WDFREQUEST      Request;
    PVOID           Extend;
    SIZE_T          ExtendSize;
    PVOID           FenceObject;
}VGPU_BUFFER, * PVGPU_BUFFER;

// gloval variables
CAPSETS     Capsets;
LIST_ENTRY  VirglContextList;
KSPIN_LOCK  VirglContextListSpinLock;

#define VGPU_DEBUG_TAG() KdPrint(("[Tenclass] Func:%s Line:%d \n", __FUNCTION__, __LINE__))
#define VGPU_DEBUG_PRINT(string) KdPrint(("[Tenclass] Func:%s Line:%d " string "\n", __FUNCTION__, __LINE__))
#define VGPU_DEBUG_LOG(fmt, ...) KdPrint(("[Tenclass] Func:%s Line:%d " fmt "\n", __FUNCTION__, __LINE__, __VA_ARGS__))

VOID SpinLock(KIRQL* Irql, PKSPIN_LOCK SpinLock);
VOID SpinUnLock(KIRQL Irql, PKSPIN_LOCK SpinLock);
