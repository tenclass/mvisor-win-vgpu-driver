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

typedef struct _FREEID {
    LARGE_INTEGER   Id;
    LIST_ENTRY      Entry;
}FREEID, * PFREEID;

typedef struct _IDRANDOM {
    LARGE_INTEGER           Id;
    KSPIN_LOCK	            SpinLock;
    LIST_ENTRY	            FreeIdList;
    BOOLEAN                 Initilaized;
    LOOKASIDE_LIST_EX       LookAsideList;
}IDRANDOM, * PIDRANDOM;

#define VIRGL_RESOURCE_ID_TYPE      0
#define FENCE_ID_TYPE               1
#define IDR_MAX_SIZE                2

VOID InitializeIdr();
VOID UnInitializeIdr();
VOID GetIdFromIdrWithoutCache(UINT8 type, PVOID Id, SIZE_T Size);
VOID GetIdFromIdr(UINT8 type, PVOID Id, SIZE_T Size);
VOID PutIdToIdr(UINT8 type, PVOID Id, SIZE_T Size);