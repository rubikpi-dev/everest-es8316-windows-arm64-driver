/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) Microsoft Corporation. All Rights Reserved.
 * Copyright (c) Bingxing Wang. All Rights Reserved.
 * Copyright (c) LumiaWoA authors. All Rights Reserved.
 * Copyright (c) DuoWoA authors. All Rights Reserved.
 * See ../LICENSES/BSD-3-Clause.txt.
 */

#include "Driver.h"
#include <spb.h>

static
NTSTATUS
SpbDoWriteDataSynchronously(
    _In_ PSPB_CONTEXT SpbContext,
    _In_ UCHAR Address,
    _In_reads_bytes_opt_(Length) PVOID Data,
    _In_ ULONG Length
    )
{
    PUCHAR buffer;
    ULONG length;
    WDFMEMORY memory;
    WDF_MEMORY_DESCRIPTOR memoryDescriptor;
    NTSTATUS status;

    length = Length + 1;
    memory = NULL;

    if (length > DEFAULT_SPB_BUFFER_SIZE)
    {
        status = WdfMemoryCreate(
            WDF_NO_OBJECT_ATTRIBUTES,
            NonPagedPoolNx,
            SPB_POOL_TAG,
            length,
            &memory,
            (PVOID *)&buffer);

        if (!NT_SUCCESS(status))
        {
            Trace(TRACE_LEVEL_ERROR, TRACE_SPB,
                  "Error allocating memory for Spb write - 0x%08lX", status);
            goto exit;
        }

        WDF_MEMORY_DESCRIPTOR_INIT_HANDLE(&memoryDescriptor, memory, NULL);
    }
    else
    {
        buffer = (PUCHAR)WdfMemoryGetBuffer(SpbContext->WriteMemory, NULL);
        WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&memoryDescriptor, (PVOID)buffer, length);
    }

    buffer[0] = Address;
    if (Length != 0 && Data != NULL)
    {
        RtlCopyMemory(buffer + 1, Data, Length);
    }

    status = WdfIoTargetSendWriteSynchronously(
        SpbContext->SpbIoTarget,
        NULL,
        &memoryDescriptor,
        NULL,
        NULL,
        NULL);

    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_ERROR, TRACE_SPB,
              "Error writing to Spb (reg 0x%02X) - 0x%08lX", Address, status);
    }

exit:
    if (NULL != memory)
    {
        WdfObjectDelete(memory);
    }

    return status;
}

NTSTATUS
SpbWriteDataSynchronously(
    _In_ PSPB_CONTEXT SpbContext,
    _In_ UCHAR Address,
    _In_reads_bytes_(Length) PVOID Data,
    _In_ ULONG Length
    )
{
    NTSTATUS status;

    WdfWaitLockAcquire(SpbContext->SpbLock, NULL);
    status = SpbDoWriteDataSynchronously(SpbContext, Address, Data, Length);
    WdfWaitLockRelease(SpbContext->SpbLock);

    return status;
}

NTSTATUS
SpbReadDataSynchronously(
    _In_ PSPB_CONTEXT SpbContext,
    _In_ UCHAR Address,
    _Out_writes_bytes_(Length) PVOID Data,
    _In_ ULONG Length
    )

{
    NTSTATUS status;
    UCHAR regAddress;
    PUCHAR readBuffer;
    WDFMEMORY readMemory;
    WDF_MEMORY_DESCRIPTOR inputDescriptor;
    ULONG_PTR bytesReturned;

    const ULONG transferCount = 2;
    SPB_TRANSFER_LIST_AND_ENTRIES(2) sequence;
    SPB_TRANSFER_LIST *transferList = &sequence.List;

    if (Length == 0 || Data == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    WdfWaitLockAcquire(SpbContext->SpbLock, NULL);

    readMemory = NULL;
    regAddress = Address;

    if (Length > DEFAULT_SPB_BUFFER_SIZE)
    {
        status = WdfMemoryCreate(
            WDF_NO_OBJECT_ATTRIBUTES,
            NonPagedPoolNx,
            SPB_POOL_TAG,
            Length,
            &readMemory,
            (PVOID *)&readBuffer);

        if (!NT_SUCCESS(status))
        {
            Trace(TRACE_LEVEL_ERROR, TRACE_SPB,
                  "Error allocating memory for Spb read - 0x%08lX", status);
            goto exit;
        }
    }
    else
    {
        readBuffer = (PUCHAR)WdfMemoryGetBuffer(SpbContext->ReadMemory, NULL);
    }

    SPB_TRANSFER_LIST_INIT(transferList, transferCount);

    transferList->Transfers[0] = SPB_TRANSFER_LIST_ENTRY_INIT_SIMPLE(
        SpbTransferDirectionToDevice,
        0,
        &regAddress,
        sizeof(regAddress));

    transferList->Transfers[1] = SPB_TRANSFER_LIST_ENTRY_INIT_SIMPLE(
        SpbTransferDirectionFromDevice,
        0,
        readBuffer,
        Length);

    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(
        &inputDescriptor,
        &sequence,
        sizeof(sequence));

    bytesReturned = 0;
    status = WdfIoTargetSendIoctlSynchronously(
        SpbContext->SpbIoTarget,
        NULL,
        IOCTL_SPB_EXECUTE_SEQUENCE,
        &inputDescriptor,
        NULL,
        NULL,
        &bytesReturned);

    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_ERROR, TRACE_SPB,
              "Error reading from Spb (reg 0x%02X) - 0x%08lX", Address, status);
        goto exit;
    }

    if (bytesReturned < (ULONG_PTR)Length)
    {
        Trace(TRACE_LEVEL_ERROR, TRACE_SPB,
              "Short Spb sequence (reg 0x%02X): %Iu of %lu bytes",
              Address, bytesReturned, Length);
        status = STATUS_DEVICE_PROTOCOL_ERROR;
        goto exit;
    }

    RtlCopyMemory(Data, readBuffer, Length);

exit:
    if (NULL != readMemory)
    {
        WdfObjectDelete(readMemory);
    }

    WdfWaitLockRelease(SpbContext->SpbLock);

    return status;
}

VOID
SpbTargetDeinitialize(
    _In_ WDFDEVICE FxDevice,
    _In_ PSPB_CONTEXT SpbContext
    )
{
    UNREFERENCED_PARAMETER(FxDevice);

    if (SpbContext->SpbLock != NULL)
    {
        WdfObjectDelete(SpbContext->SpbLock);
        SpbContext->SpbLock = NULL;
    }

    if (SpbContext->ReadMemory != NULL)
    {
        WdfObjectDelete(SpbContext->ReadMemory);
        SpbContext->ReadMemory = NULL;
    }

    if (SpbContext->WriteMemory != NULL)
    {
        WdfObjectDelete(SpbContext->WriteMemory);
        SpbContext->WriteMemory = NULL;
    }

    if (SpbContext->SpbIoTarget != NULL)
    {
        WdfIoTargetClose(SpbContext->SpbIoTarget);
        WdfObjectDelete(SpbContext->SpbIoTarget);
        SpbContext->SpbIoTarget = NULL;
    }
}

NTSTATUS
SpbTargetInitialize(
    _In_ WDFDEVICE FxDevice,
    _In_ PSPB_CONTEXT SpbContext
    )
{
    WDF_OBJECT_ATTRIBUTES objectAttributes;
    WDF_IO_TARGET_OPEN_PARAMS openParams;
    UNICODE_STRING spbDeviceName;
    WCHAR spbDeviceNameBuffer[RESOURCE_HUB_PATH_SIZE];
    NTSTATUS status;

    WDF_OBJECT_ATTRIBUTES_INIT(&objectAttributes);
    objectAttributes.ParentObject = FxDevice;

    status = WdfIoTargetCreate(FxDevice, &objectAttributes, &SpbContext->SpbIoTarget);
    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_ERROR, TRACE_SPB,
              "Error creating IoTarget object - 0x%08lX", status);
        SpbContext->SpbIoTarget = NULL;
        goto exit;
    }

    RtlInitEmptyUnicodeString(&spbDeviceName, spbDeviceNameBuffer, sizeof(spbDeviceNameBuffer));

    status = RESOURCE_HUB_CREATE_PATH_FROM_ID(
        &spbDeviceName,
        SpbContext->I2cResHubId.LowPart,
        SpbContext->I2cResHubId.HighPart);

    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_ERROR, TRACE_SPB,
              "Error creating Spb resource hub path string - 0x%08lX", status);
        goto exit;
    }

    WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_NAME(
        &openParams,
        &spbDeviceName,
        (GENERIC_READ | GENERIC_WRITE));

    openParams.ShareAccess = 0;
    openParams.CreateDisposition = FILE_OPEN;
    openParams.FileAttributes = FILE_ATTRIBUTE_NORMAL;

    status = WdfIoTargetOpen(SpbContext->SpbIoTarget, &openParams);
    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_ERROR, TRACE_SPB,
              "Error opening Spb target for communication - 0x%08lX", status);
        goto exit;
    }

    status = WdfMemoryCreate(
        WDF_NO_OBJECT_ATTRIBUTES,
        NonPagedPoolNx,
        SPB_POOL_TAG,
        DEFAULT_SPB_BUFFER_SIZE,
        &SpbContext->WriteMemory,
        NULL);

    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_ERROR, TRACE_SPB,
              "Error allocating default memory for Spb write - 0x%08lX", status);
        goto exit;
    }

    status = WdfMemoryCreate(
        WDF_NO_OBJECT_ATTRIBUTES,
        NonPagedPoolNx,
        SPB_POOL_TAG,
        DEFAULT_SPB_BUFFER_SIZE,
        &SpbContext->ReadMemory,
        NULL);

    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_ERROR, TRACE_SPB,
              "Error allocating default memory for Spb read - 0x%08lX", status);
        goto exit;
    }

    status = WdfWaitLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &SpbContext->SpbLock);
    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_ERROR, TRACE_SPB,
              "Error creating Spb Waitlock - 0x%08lX", status);
        goto exit;
    }

exit:
    if (!NT_SUCCESS(status))
    {
        SpbTargetDeinitialize(FxDevice, SpbContext);
    }

    return status;
}

