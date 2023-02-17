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


PVIRGL_CONTEXT GetVirglContextFromList(ULONG32 VirglContextId);
PVIRGL_RESOURCE GetResourceFromList(PVIRGL_CONTEXT virglContext, ULONG32 Id);
VOID SetResourceState(PVIRGL_CONTEXT virglContext, ULONG32* Ids, SIZE_T IdsSize, BOOLEAN Busy, ULONG64 FenceId);

NTSTATUS CtlCreateVirglContext(IN PDEVICE_CONTEXT Context, IN ULONG32 virglContextId);
NTSTATUS CtlDestroyVirglContext(IN PVIRGL_CONTEXT VirglContext);
NTSTATUS CtlGetParams(IN PDEVICE_CONTEXT Context, IN WDFREQUEST Request, IN size_t InputBufferLength, IN size_t OutputBufferLength, OUT size_t* bytesReturn);
NTSTATUS CtlGetCaps(IN PDEVICE_CONTEXT Context, IN WDFREQUEST Request, IN size_t OutputBufferLength, IN size_t InputBufferLength, OUT size_t* bytesReturn);
NTSTATUS CtlCreateResource(IN WDFREQUEST Request, IN size_t OutputBufferLength, IN size_t InputBufferLength, OUT size_t* bytesReturn);
NTSTATUS CtlCloseResource(IN WDFREQUEST Request, IN size_t InputBufferLength, OUT size_t* bytesReturn);
NTSTATUS CtlWait(IN WDFREQUEST Request, IN size_t OutputBufferLength, IN size_t InputBufferLength, OUT size_t* bytesReturn);
NTSTATUS CtlMap(IN WDFREQUEST Request, IN size_t OutputBufferLength, IN size_t InputBufferLength, OUT size_t* bytesReturn);
NTSTATUS CtlSubmitCommand(IN WDFREQUEST Request, IN size_t InputBufferLength, OUT size_t* bytesReturn);
NTSTATUS CtlTransferHost(IN BOOLEAN ToHost, IN WDFREQUEST Request, IN size_t InputBufferLength, OUT size_t* bytesReturn);
