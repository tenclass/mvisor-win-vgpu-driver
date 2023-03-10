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

#include "global.h"

VOID InitializeVgpuMemory(PVOID VitrualAddress, PHYSICAL_ADDRESS PhysicalAddress, SIZE_T Size);
VOID UninitializeVgpuMemory();
VOID FreeVgpuMemory(PVOID VitrualAddress);
VOID ReallocVgpuMemory(PVOID VitrualAddress, SIZE_T Size);
BOOLEAN AllocateVgpuMemory(SIZE_T Size, PMEMORY_DESCRIPTOR Memory);