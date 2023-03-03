/*
 * MVisor vgpu Device guest driver
 * It delivers virgl3d command from guest to host libvirglrenderer.so,
 * but it doesn't display anything for guest like vga.
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
#include "vgpu.h"
#include "control.h"
#include "command.h"
#include "memory.h"
#include "idr.h"

#pragma code_seg(push)
#pragma code_seg()


VOID ProcessNotify(IN HANDLE ParentId, IN HANDLE ProcessId, IN BOOLEAN Create)
{
    UNREFERENCED_PARAMETER(ParentId);

    PVIRGL_CONTEXT  virglContext;

    if (!Create)
    {
        virglContext = GetVirglContextFromList(HandleToULong(ProcessId));
        if (virglContext)
        {
            CtlDestroyVirglContext(virglContext);
            VGPU_DEBUG_LOG("delete virgl context in process notify context_id=%d", virglContext->Id);
        }
    }
}

VOID SpinLock(KIRQL* Irql, PKSPIN_LOCK SpinLock)
{
    KIRQL savedIrql = KeGetCurrentIrql();

    if (savedIrql < DISPATCH_LEVEL) 
    {
        KeAcquireSpinLock(SpinLock, &savedIrql);
    }
    else
    {
        KeAcquireSpinLockAtDpcLevel(SpinLock);
    }

    *Irql = savedIrql;
}

VOID SpinUnLock(KIRQL Irql, PKSPIN_LOCK SpinLock)
{
    if (Irql < DISPATCH_LEVEL) 
    {
        KeReleaseSpinLock(SpinLock, Irql);
    }
    else 
    {
        KeReleaseSpinLockFromDpcLevel(SpinLock);
    }
}

VOID VirtioVgpuReadFromQueue(PDEVICE_CONTEXT Context, struct virtqueue* pVirtQueue, WDFSPINLOCK vqLock)
{
    UINT32                      length;
    PVGPU_BUFFER                buffer;
    struct virtio_gpu_ctrl_hdr* header;

    while (TRUE)
    {
        WdfSpinLockAcquire(vqLock);

        buffer = virtqueue_get_buf(pVirtQueue, &length);
        if (buffer == NULL)
        {
            WdfSpinLockRelease(vqLock);
            break;
        }

        header = (struct virtio_gpu_ctrl_hdr*)buffer->pBuf;
        switch (header->type)
        {
        case VIRTIO_GPU_CMD_GET_CAPSET:
        case VIRTIO_GPU_CMD_GET_CAPSET_INFO:
            KeSetEvent(&buffer->Event, IO_NO_INCREMENT, FALSE);
            break;
        case VIRTIO_GPU_CMD_SUBMIT_3D:
        {
            if (buffer->Extend != NULL)
            {
                PVIRGL_CONTEXT virglContext = GetVirglContextFromList(header->ctx_id);
                if (virglContext)
                {
                    UpdateResourceState(virglContext, buffer->Extend, buffer->ExtendSize, FALSE, header->fence_id);
                }
                ExFreePoolWithTag(buffer->Extend, VIRTIO_VGPU_MEMORY_TAG);
            }

            if (buffer->FenceObject != NULL)
            {
                KeSetEvent(buffer->FenceObject, IO_NO_INCREMENT, FALSE);
            }

            // relesase the contiguous cmd data buffer
            if (buffer->pDataBuf != NULL)
            {
                FreeVgpuMemory(buffer->pDataBuf);
            }

            FreeCommandBuffer(Context, buffer);
            break;
        }
        case VIRTIO_GPU_CMD_RESOURCE_MAP_BLOB:
        {
            PVIRGL_CONTEXT virglContext = GetVirglContextFromList(header->ctx_id);
            if (virglContext)
            {
                struct virtio_gpu_resource_map_blob* cmd = (struct virtio_gpu_resource_map_blob*)buffer->pRespBuf;
                struct virtio_gpu_resp_map_info* resp = (struct virtio_gpu_resp_map_info*)buffer->pRespBuf;
                //FIXME: how to use map_info ?
                MapBlobResourceCallback(virglContext, cmd->resource_id, resp->gpa, resp->size);
            }
            FreeCommandBuffer(Context, buffer);
            break;
        }
        case VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D:
        case VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D:
        {
            PVIRGL_CONTEXT virglContext = GetVirglContextFromList(header->ctx_id);
            if (virglContext)
            {
                struct virtio_gpu_transfer_host_3d* transfer = (struct virtio_gpu_transfer_host_3d*)buffer->pBuf;
                UpdateResourceState(virglContext, &((ULONG32)transfer->resource_id), sizeof(ULONG32), FALSE, header->fence_id);
            }
            break;
        }
        case VIRTIO_GPU_CMD_RESOURCE_CREATE_2D:
        case VIRTIO_GPU_CMD_RESOURCE_CREATE_3D:
        case VIRTIO_GPU_CMD_RESOURCE_CREATE_BLOB:
        case VIRTIO_GPU_CMD_RESOURCE_UNREF:
        case VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING:
        case VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING:
        case VIRTIO_GPU_CMD_CTX_CREATE:
        case VIRTIO_GPU_CMD_CTX_DESTROY:
        case VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE:
        case VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE:
        case VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D:
        case VIRTIO_GPU_CMD_RESOURCE_UNMAP_BLOB:
            FreeCommandBuffer(Context, buffer);
            break;
        default:
            VGPU_DEBUG_LOG("unknown cmd type=%d", header->type);
            FreeCommandBuffer(Context, buffer);
            break;
        }

        WdfSpinLockRelease(vqLock);
    }
}

VOID VirtioVgpuInterruptDpc(IN WDFINTERRUPT Interrupt, IN WDFOBJECT AssociatedObject)
{
    UNREFERENCED_PARAMETER(AssociatedObject);

    WDF_INTERRUPT_INFO  info;
    PDEVICE_CONTEXT     context;
    struct virtqueue*   pVirtqueue = NULL;
    WDFSPINLOCK         vqLock = NULL;

    WDF_INTERRUPT_INFO_INIT(&info);
    WdfInterruptGetInfo(Interrupt, &info);
    context = GetDeviceContext(WdfInterruptGetDevice(Interrupt));

    if ((info.MessageSignaled == TRUE) && (info.MessageNumber < context->NumVirtQueues))
    {
        pVirtqueue = context->VirtQueues[info.MessageNumber];
        vqLock = context->VirtQueueLocks[info.MessageNumber];
    }

    if (pVirtqueue != NULL)
    {
        VirtioVgpuReadFromQueue(context, pVirtqueue, vqLock);
    }
    else
    {
        for (size_t i = 0; i < context->NumVirtQueues; i++)
        {
            pVirtqueue = context->VirtQueues[i];
            vqLock = context->VirtQueueLocks[i];
            VirtioVgpuReadFromQueue(context, pVirtqueue, vqLock);
        }
    }
}

BOOLEAN VirtioVgpuInterruptIsr(IN WDFINTERRUPT Interrupt, IN ULONG MessageId)
{
    PDEVICE_CONTEXT     context;
    WDF_INTERRUPT_INFO  info;
    BOOLEAN             serviced;

    VGPU_DEBUG_LOG("--> %!FUNC! Interrupt: %p MessageId: %u", Interrupt, MessageId);

    WDF_INTERRUPT_INFO_INIT(&info);
    WdfInterruptGetInfo(Interrupt, &info);
    context = GetDeviceContext(WdfInterruptGetDevice(Interrupt));

    if ((info.MessageSignaled && (MessageId < context->NumVirtQueues)) || VirtIOWdfGetISRStatus(&context->VDevice))
    {
        WdfInterruptQueueDpcForIsr(Interrupt);
        serviced = TRUE;
    }
    else
    {
        serviced = FALSE;
    }

    return serviced;
}

NTSTATUS VirtioVgpuInterruptEnable(IN WDFINTERRUPT Interrupt, IN WDFDEVICE wdfDevice)
{
    UNREFERENCED_PARAMETER(wdfDevice);

    WDF_INTERRUPT_INFO  info;
    PDEVICE_CONTEXT     context;

    WDF_INTERRUPT_INFO_INIT(&info);
    WdfInterruptGetInfo(Interrupt, &info);
    context = GetDeviceContext(WdfInterruptGetDevice(Interrupt));

    VGPU_DEBUG_LOG("info.MessageNumber=%d", info.MessageNumber);

    if (info.MessageSignaled)
    {
        if (info.MessageNumber < context->NumVirtQueues)
        {
            virtqueue_enable_cb(context->VirtQueues[info.MessageNumber]);
            virtqueue_kick(context->VirtQueues[info.MessageNumber]);
        }
    }
    else
    {
        for (size_t i = 0; i < context->NumVirtQueues; i++)
        {
            virtqueue_enable_cb(context->VirtQueues[i]);
            virtqueue_kick(context->VirtQueues[i]);
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS VirtioVgpuInterruptDisable(IN WDFINTERRUPT Interrupt, IN WDFDEVICE wdfDevice)
{
    UNREFERENCED_PARAMETER(wdfDevice);

    WDF_INTERRUPT_INFO  info;
    PDEVICE_CONTEXT     context;

    WDF_INTERRUPT_INFO_INIT(&info);
    WdfInterruptGetInfo(Interrupt, &info);
    context = GetDeviceContext(WdfInterruptGetDevice(Interrupt));

    VGPU_DEBUG_LOG("info.MessageNumber=%d", info.MessageNumber);

    if (info.MessageSignaled)
    {
        if (info.MessageNumber < context->NumVirtQueues)
        {
            virtqueue_disable_cb(context->VirtQueues[info.MessageNumber]);
        }
    }
    else
    {
        for (size_t i = 0; i < context->NumVirtQueues; i++)
        {
            virtqueue_disable_cb(context->VirtQueues[i]);
        }
    }

    return STATUS_SUCCESS;
}

VOID VirtioVgpuIoControl(IN WDFQUEUE Queue, IN WDFREQUEST Request, IN size_t OutputBufferLength, IN size_t InputBufferLength, IN ULONG IoControlCode)
{
    NTSTATUS        status;
    SIZE_T          bytesReturn = 0;

    switch (IoControlCode)
    {
    case IOCTL_VIRTIO_VGPU_CONTEXT_INIT:
        status = CtlInitVirglContext(GetDeviceContext(WdfIoQueueGetDevice(Queue)), Request, InputBufferLength, &bytesReturn);
        break;
    case IOCTL_VIRTIO_VGPU_DESTROY_CONTEXT: 
    {
        ULONG32 virglContextId = HandleToULong(PsGetCurrentProcessId());
        PVIRGL_CONTEXT virglContext = GetVirglContextFromList(virglContextId);
        if (virglContext)
        {
            status = CtlDestroyVirglContext(virglContext);
        }
        else
        {
            VGPU_DEBUG_LOG("can't find the virgl context=%d, maybe it was cleared already", virglContextId);
            status = STATUS_UNSUCCESSFUL;
        }
        break;
    }
    case IOCTL_VIRTIO_VGPU_GETPARAM:
        status = CtlGetParams(GetDeviceContext(WdfIoQueueGetDevice(Queue)), Request, OutputBufferLength, InputBufferLength, &bytesReturn);
        break;
    case IOCTL_VIRTIO_VGPU_GET_CAPS:
        status = CtlGetCaps(GetDeviceContext(WdfIoQueueGetDevice(Queue)), Request, OutputBufferLength, InputBufferLength, &bytesReturn);
        break;
    case IOCTL_VIRTIO_VGPU_RESOURCE_CREATE:
        status = CtlCreateResource(Request, OutputBufferLength, InputBufferLength, &bytesReturn);
        break;
    case IOCTL_VIRTIO_VGPU_RESOURCE_CLOSE:
        status = CtlCloseResource(Request, InputBufferLength, &bytesReturn);
        break;
    case IOCTL_VIRTIO_VGPU_WAIT:
        status = CtlWait(Request, OutputBufferLength, InputBufferLength, &bytesReturn);
        break;
    case IOCTL_VIRTIO_VGPU_MAP:
        status = CtlMap(Request, OutputBufferLength, InputBufferLength, &bytesReturn);
        break;
    case IOCTL_VIRTIO_VGPU_EXECBUFFER:
        status = CtlSubmitCommand(Request, InputBufferLength, &bytesReturn);
        break;
    case IOCTL_VIRTIO_VGPU_TRANSFER_FROM_HOST:
        status = CtlTransferHost(FALSE, Request, InputBufferLength, &bytesReturn);
        break;
    case IOCTL_VIRTIO_VGPU_TRANSFER_TO_HOST:
        status = CtlTransferHost(TRUE, Request, InputBufferLength, &bytesReturn);
        break;
    case IOCTL_VIRTIO_VGPU_BLOB_RESOURCE_CREATE:
        status = CtlCreateBlobResource(Request, OutputBufferLength, InputBufferLength, &bytesReturn);
        break;
    default:
        status = STATUS_NOT_SUPPORTED;
        VGPU_DEBUG_LOG("unsupport ioctl code=%d", IoControlCode);
        break;
    }

    if (!NT_SUCCESS(status))
    {
        VGPU_DEBUG_LOG("io control failed status=0x%08x code=%d", status, IoControlCode);
    }

    if (status != STATUS_PENDING)
    {
        WdfRequestCompleteWithInformation(Request, status, bytesReturn);
    }
}

// end non-paged pool code
#pragma code_seg(pop)

#pragma code_seg(push)
#pragma code_seg("PAGE")

VOID VirtioVgpuDeviceContextCleanup(IN WDFOBJECT DeviceObject)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    PAGED_CODE();

    VGPU_DEBUG_TAG();
}

NTSTATUS VirtioVgpuDevicePrepareHardware(IN WDFDEVICE Device, IN WDFCMRESLIST Resources, IN WDFCMRESLIST ResourcesTranslated)
{
    UNREFERENCED_PARAMETER(Resources);
    VGPU_DEBUG_TAG();

    NTSTATUS                    status;
    PDEVICE_CONTEXT             context;
    WDF_OBJECT_ATTRIBUTES       attributes;
    PHYSICAL_ADDRESS            highestAcceptableAddress;
    SIZE_T                      vgpuMemorySize;

    PAGED_CODE();

    context = GetDeviceContext(Device);
    ASSERT(context != NULL);

    // initialize virtio device driver context
    status = VirtIOWdfInitialize(&context->VDevice, Device, ResourcesTranslated, NULL, VIRTIO_VGPU_MEMORY_TAG);
    if (!NT_SUCCESS(status))
    {
        VGPU_DEBUG_LOG("VirtIOWdfInitialize failed status=0x%08x", status);
        return status;
    }

    // get virtio config NumVirtQueues
    VirtIOWdfDeviceGet(&context->VDevice, FIELD_OFFSET(struct virtio_vgpu_config, num_queues), &context->NumVirtQueues, sizeof(UINT8));
    ASSERT(context->NumVirtQueues > 0 && context->NumVirtQueues <= MAX_INTERRUPT_COUNT);
    VGPU_DEBUG_LOG("get virtio queue num=%d", context->NumVirtQueues);

    // allocate virtqueue pointer
    context->VirtQueues = ExAllocatePool2(POOL_FLAG_NON_PAGED, context->NumVirtQueues * sizeof(struct virtqueue*), VIRTIO_VGPU_MEMORY_TAG);
    ASSERT(context->VirtQueues != NULL);

    // create spin lock for each queue
    context->VirtQueueLocks = ExAllocatePool2(POOL_FLAG_NON_PAGED, context->NumVirtQueues * sizeof(WDFSPINLOCK), VIRTIO_VGPU_MEMORY_TAG);
    ASSERT(context->VirtQueueLocks != NULL);

    // get vgpu capabilities
    VirtIOWdfDeviceGet(&context->VDevice, FIELD_OFFSET(struct virtio_vgpu_config, capabilities), &context->Capabilities, sizeof(ULONG64));

    // get vgpu memory config
    VirtIOWdfDeviceGet(&context->VDevice, FIELD_OFFSET(struct virtio_vgpu_config, memory_size), &vgpuMemorySize, sizeof(ULONG64));

    // get vgpu num_capsets config
    VirtIOWdfDeviceGet(&context->VDevice, FIELD_OFFSET(struct virtio_vgpu_config, num_capsets), &Capsets.NumCaps, sizeof(ULONG32));

    // initialize Capsets
    Capsets.Initialized = FALSE;
    Capsets.Data = ExAllocatePool2(POOL_FLAG_NON_PAGED, Capsets.NumCaps * sizeof(VIRTIO_GPU_DRV_CAPSET), VIRTIO_VGPU_MEMORY_TAG);
    ASSERT(Capsets.Data != NULL);

    highestAcceptableAddress.QuadPart = 0xFFFFFFFFFF;// 512G
    context->VgpuMemoryAddress = MmAllocateContiguousMemory(vgpuMemorySize, highestAcceptableAddress);
    if (!context->VgpuMemoryAddress)
    {
        VGPU_DEBUG_PRINT("map vgpu memory failed");
        return STATUS_UNSUCCESSFUL;
    }
    RtlZeroMemory(context->VgpuMemoryAddress, vgpuMemorySize);
    InitializeVgpuMemory(context->VgpuMemoryAddress, MmGetPhysicalAddress(context->VgpuMemoryAddress), vgpuMemorySize);

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = Device;
    for (size_t i = 0; i < context->NumVirtQueues; i++)
    {
        status = WdfSpinLockCreate(&attributes, &context->VirtQueueLocks[i]);
        if (!NT_SUCCESS(status))
        {
            VGPU_DEBUG_LOG("WdfSpinLockCreate failed status=0x%08x", status);
            return status;
        }
    }

    PsSetCreateProcessNotifyRoutine(ProcessNotify, FALSE);
    InitializeListHead(&VirglContextList);
    KeInitializeSpinLock(&VirglContextListSpinLock);
    InitializeIdr();

    ExInitializeLookasideListEx(
        &context->VirglResourceLookAsideList,
        NULL,
        NULL,
        NonPagedPool,
        0,
        sizeof(VIRGL_RESOURCE),
        VIRTIO_VGPU_MEMORY_TAG,
        0
    );
    ExInitializeLookasideListEx(
        &context->VgpuBufferLookAsideList,
        NULL,
        NULL,
        NonPagedPool,
        0,
        sizeof(VGPU_BUFFER),
        VIRTIO_VGPU_MEMORY_TAG,
        0
    );

    return STATUS_SUCCESS;
}

NTSTATUS VirtioVgpuDeviceReleaseHardware(IN WDFDEVICE Device, IN WDFCMRESLIST ResourcesTranslated)
{
    UNREFERENCED_PARAMETER(ResourcesTranslated);
    VGPU_DEBUG_TAG();

    PDEVICE_CONTEXT context;

    PAGED_CODE();

    context = GetDeviceContext(Device);
    ASSERT(context != NULL);

    if (!IsListEmpty(&VirglContextList))
    {
        VGPU_DEBUG_PRINT("virgl context list was not empty, it may cause memory leaked");
    }

    if (context->VirtQueues)
    {
        ExFreePoolWithTag(context->VirtQueues, VIRTIO_VGPU_MEMORY_TAG);
        context->VirtQueues = NULL;
    }

    if (context->VirtQueueLocks)
    {
        ExFreePoolWithTag(context->VirtQueueLocks, VIRTIO_VGPU_MEMORY_TAG);
        context->VirtQueueLocks = NULL;
    }

    if (Capsets.Data)
    {
        ExFreePoolWithTag(Capsets.Data, VIRTIO_VGPU_MEMORY_TAG);
        Capsets.Data = NULL;
    }

    if (context->VgpuMemoryAddress)
    {
        UninitializeVgpuMemory();
        MmFreeContiguousMemory(context->VgpuMemoryAddress);
    }

    UnInitializeIdr();
    ExDeleteLookasideListEx(&context->VirglResourceLookAsideList);
    ExDeleteLookasideListEx(&context->VgpuBufferLookAsideList);
    PsSetCreateProcessNotifyRoutine(ProcessNotify, TRUE);
    VirtIOWdfShutdown(&context->VDevice);

    return STATUS_SUCCESS;
}

NTSTATUS VirtioVgpuDeviceD0Entry(IN WDFDEVICE Device, IN WDF_POWER_DEVICE_STATE PreviousState)
{
    UNREFERENCED_PARAMETER(PreviousState);

    VGPU_DEBUG_LOG("from D%d", PreviousState - WdfPowerDeviceD0);

    PAGED_CODE();

    PDEVICE_CONTEXT context = GetDeviceContext(Device);
    if (context == NULL || context->NumVirtQueues <= 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    VIRTIO_WDF_QUEUE_PARAM* params = ExAllocatePool2(POOL_FLAG_PAGED, context->NumVirtQueues * sizeof(VIRTIO_WDF_QUEUE_PARAM), VIRTIO_VGPU_MEMORY_TAG);
    if (!params)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // bind each virtio queue to interrupt
    for (ULONG i = 0; i < context->NumVirtQueues; i++)
    {
        params[i].Interrupt = context->WdfInterrupt[i];
    }

    NTSTATUS status = VirtIOWdfInitQueues(&context->VDevice, context->NumVirtQueues, context->VirtQueues, params);
    if (NT_SUCCESS(status))
    {
        VirtIOWdfSetDriverOK(&context->VDevice);
        VGPU_DEBUG_LOG("init virtio queue index=0 size=%d", context->VirtQueues[0]->vdev->info->num);
    }
    else
    {
        VGPU_DEBUG_LOG("VirtIOWdfInitialize failed status=0x%08x", status);
    }
    ExFreePoolWithTag(params, VIRTIO_VGPU_MEMORY_TAG);

    return status;
}

NTSTATUS VirtioVgpuDeviceD0Exit(IN WDFDEVICE Device, IN WDF_POWER_DEVICE_STATE TargetState)
{
    UNREFERENCED_PARAMETER(TargetState);

    VGPU_DEBUG_LOG("to D%d", TargetState - WdfPowerDeviceD0);

    PDEVICE_CONTEXT context = GetDeviceContext(Device);
    ASSERT(context != NULL);

    PAGED_CODE();

    VirtIOWdfDestroyQueues(&context->VDevice);

    return STATUS_SUCCESS;
}

VOID VirtioVgpuDriverContextCleanup(IN WDFOBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    PAGED_CODE();

    VGPU_DEBUG_TAG();
}

VOID VirtioVgpuIoStop(IN WDFQUEUE Queue, IN WDFREQUEST Request, IN ULONG ActionFlags)
{
    UNREFERENCED_PARAMETER(Queue);
    BOOLEAN bCancellable = (ActionFlags & WdfRequestStopRequestCancelable) != 0;

    VGPU_DEBUG_LOG("Req %p, action %X, the request is %scancellable",
        Request, ActionFlags, bCancellable ? "" : "not ");

    if (ActionFlags & WdfRequestStopActionSuspend)
    {
        // the driver owns the request and it will not be able to process it
        VGPU_DEBUG_LOG("Req %p can't be suspended", Request);
        if (!bCancellable || WdfRequestUnmarkCancelable(Request) != STATUS_CANCELLED)
        {
            WdfRequestComplete(Request, STATUS_CANCELLED);
        }
    }
    else if (ActionFlags & WdfRequestStopActionPurge)
    {
        VGPU_DEBUG_LOG("Req %p purged", Request);
        if (!bCancellable || WdfRequestUnmarkCancelable(Request) != STATUS_CANCELLED)
        {
            WdfRequestComplete(Request, STATUS_CANCELLED);
        }
    }
}

NTSTATUS VirtioVgpuDeviceAdd(IN WDFDRIVER Driver, IN PWDFDEVICE_INIT DeviceInit)
{
    NTSTATUS                        status;
    WDFDEVICE                       device;
    WDF_PNPPOWER_EVENT_CALLBACKS    pnpPowerCallbacks;
    WDF_OBJECT_ATTRIBUTES           attributes;
    WDFQUEUE                        queue;
    WDF_IO_QUEUE_CONFIG             queueConfig;
    PDEVICE_CONTEXT                 context;

    UNREFERENCED_PARAMETER(Driver);

    PAGED_CODE();

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
    pnpPowerCallbacks.EvtDevicePrepareHardware = VirtioVgpuDevicePrepareHardware;
    pnpPowerCallbacks.EvtDeviceReleaseHardware = VirtioVgpuDeviceReleaseHardware;
    pnpPowerCallbacks.EvtDeviceD0Entry = VirtioVgpuDeviceD0Entry;
    pnpPowerCallbacks.EvtDeviceD0Exit = VirtioVgpuDeviceD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);
    WdfDeviceInitSetIoType(DeviceInit, WdfDeviceIoDirect);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DEVICE_CONTEXT);
    attributes.EvtCleanupCallback = VirtioVgpuDeviceContextCleanup;

    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    ASSERT(NT_SUCCESS(status));

    context = GetDeviceContext(device);
    RtlZeroMemory(context, sizeof(*context));

    status = WdfDeviceCreateDeviceInterface(device, &GUID_DEVINTERFACE_VGPU, NULL);
    if (!NT_SUCCESS(status))
    {
        VGPU_DEBUG_LOG("WdfDeviceCreateDeviceInterface failed status=0x%08x", status);
        return STATUS_UNSUCCESSFUL;
    }

    WDF_INTERRUPT_CONFIG interruptConfig;
    WDF_INTERRUPT_CONFIG_INIT(&interruptConfig, VirtioVgpuInterruptIsr, VirtioVgpuInterruptDpc);
    interruptConfig.EvtInterruptEnable = VirtioVgpuInterruptEnable;
    interruptConfig.EvtInterruptDisable = VirtioVgpuInterruptDisable;
    for (UINT8 i = 0; i < MAX_INTERRUPT_COUNT; i++)
    {
        status = WdfInterruptCreate(device, &interruptConfig, WDF_NO_OBJECT_ATTRIBUTES, &context->WdfInterrupt[i]);
        if (!NT_SUCCESS(status))
        {
            VGPU_DEBUG_LOG("WdfInterruptCreate failed status=0x%08x", status);
            return status;
        }
    }

    // WdfIoQueueDispatchSequential vs WdfIoQueueDispatchParallel
    // WdfIoQueueDispatchParallel may conflict with "indirect page" in virtio
    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchParallel);
    queueConfig.EvtIoDeviceControl = VirtioVgpuIoControl;
    queueConfig.EvtIoStop = VirtioVgpuIoStop;
    queueConfig.AllowZeroLengthRequests = FALSE;
    status = WdfIoQueueCreate(device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &queue);
    if (!NT_SUCCESS(status))
    {
        VGPU_DEBUG_LOG("WdfIoQueueCreate failed status=0x%08x", status);
        return STATUS_UNSUCCESSFUL;
    }

    status = WdfDeviceConfigureRequestDispatching(device, queue, WdfRequestTypeDeviceControl);
    if (!NT_SUCCESS(status))
    {
        VGPU_DEBUG_LOG("WdfDeviceConfigureRequestDispatching failed status=0x%08x", status);
        return STATUS_UNSUCCESSFUL;
    }

    return STATUS_SUCCESS;
}

// end paged pool code
#pragma code_seg(pop)

#pragma code_seg(push)
#pragma code_seg("INIT")

NTSTATUS DriverEntry(IN PDRIVER_OBJECT DriverObject, IN PUNICODE_STRING RegistryPath)
{
    NTSTATUS                status;
    WDF_DRIVER_CONFIG       config;
    WDF_OBJECT_ATTRIBUTES   attributes;

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.EvtCleanupCallback = VirtioVgpuDriverContextCleanup;

    WDF_DRIVER_CONFIG_INIT(&config, VirtioVgpuDeviceAdd);

    status = WdfDriverCreate(DriverObject, RegistryPath, &attributes, &config, WDF_NO_HANDLE);
    if (!NT_SUCCESS(status))
    {
        VGPU_DEBUG_LOG("WdfDriverCreate failed status=0x%08x", status);
    }

    return status;
}

#pragma code_seg(pop)
