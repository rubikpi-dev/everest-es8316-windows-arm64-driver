/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) Microsoft Corporation. All Rights Reserved.
 * Sample code. Dealpoint ID #843729.
 * See ../LICENSES/BSD-3-Clause.txt.
 */

#pragma once

#include <wdm.h>
#include <wdf.h>

#define DEFAULT_SPB_BUFFER_SIZE 64

#define SPB_POOL_TAG 'bpSE'

typedef struct _SPB_CONTEXT
{
    WDFIOTARGET SpbIoTarget;
    LARGE_INTEGER I2cResHubId;
    WDFMEMORY WriteMemory;
    WDFMEMORY ReadMemory;
    WDFWAITLOCK SpbLock;
} SPB_CONTEXT, *PSPB_CONTEXT;

NTSTATUS
SpbTargetInitialize(
    _In_ WDFDEVICE FxDevice,
    _In_ PSPB_CONTEXT SpbContext
    );

VOID
SpbTargetDeinitialize(
    _In_ WDFDEVICE FxDevice,
    _In_ PSPB_CONTEXT SpbContext
    );

NTSTATUS
SpbWriteDataSynchronously(
    _In_ PSPB_CONTEXT SpbContext,
    _In_ UCHAR Address,
    _In_reads_bytes_(Length) PVOID Data,
    _In_ ULONG Length
    );

NTSTATUS
SpbReadDataSynchronously(
    _In_ PSPB_CONTEXT SpbContext,
    _In_ UCHAR Address,
    _Out_writes_bytes_(Length) PVOID Data,
    _In_ ULONG Length
    );

