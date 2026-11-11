/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Licensed under the GNU General Public License, version 2 only.
 * Provided without warranty. See LICENSE for the license text.
 */

#include "Driver.h"
#include <gpio.h>

NTSTATUS
GpioOpenPin(
    _In_ WDFDEVICE FxDevice,
    _In_ LARGE_INTEGER ConnectionId,
    _In_ ACCESS_MASK DesiredAccess,
    _Out_ WDFIOTARGET *Target
    )
{
    NTSTATUS status;
    WDF_OBJECT_ATTRIBUTES objectAttributes;
    WDF_IO_TARGET_OPEN_PARAMS openParams;
    UNICODE_STRING pathString;
    WCHAR pathBuffer[RESOURCE_HUB_PATH_SIZE];

    *Target = NULL;

    RtlInitEmptyUnicodeString(&pathString, pathBuffer, sizeof(pathBuffer));

    status = RESOURCE_HUB_CREATE_PATH_FROM_ID(
        &pathString,
        ConnectionId.LowPart,
        ConnectionId.HighPart);

    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_ERROR, TRACE_DRIVER,
              "RESOURCE_HUB_CREATE_PATH_FROM_ID failed 0x%08lX", status);
        return status;
    }

    WDF_OBJECT_ATTRIBUTES_INIT(&objectAttributes);
    objectAttributes.ParentObject = FxDevice;

    status = WdfIoTargetCreate(FxDevice, &objectAttributes, Target);
    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_ERROR, TRACE_DRIVER,
              "WdfIoTargetCreate (GPIO) failed 0x%08lX", status);
        *Target = NULL;
        return status;
    }

    WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_NAME(&openParams, &pathString, DesiredAccess);
    openParams.ShareAccess = 0;

    status = WdfIoTargetOpen(*Target, &openParams);
    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_ERROR, TRACE_DRIVER,
              "WdfIoTargetOpen (GPIO %08lX%08lX) failed 0x%08lX",
              ConnectionId.HighPart, ConnectionId.LowPart, status);
        WdfObjectDelete(*Target);
        *Target = NULL;
        return status;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
GpioWritePin(
    _In_ WDFIOTARGET Target,
    _In_ BOOLEAN High
    )
{
    NTSTATUS status;
    UCHAR value;
    WDF_MEMORY_DESCRIPTOR inputDescriptor;
    WDF_MEMORY_DESCRIPTOR outputDescriptor;

    if (Target == NULL)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    value = High ? 1 : 0;

    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&inputDescriptor, &value, sizeof(value));
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&outputDescriptor, &value, sizeof(value));

    status = WdfIoTargetSendIoctlSynchronously(
        Target,
        NULL,
        IOCTL_GPIO_WRITE_PINS,
        &inputDescriptor,
        &outputDescriptor,
        NULL,
        NULL);

    return status;
}

NTSTATUS
GpioReadPin(
    _In_ WDFIOTARGET Target,
    _Out_ BOOLEAN *High
    )
{
    NTSTATUS status;
    UCHAR value;
    WDF_MEMORY_DESCRIPTOR outputDescriptor;

    *High = FALSE;

    if (Target == NULL)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    value = 0;
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&outputDescriptor, &value, sizeof(value));

    status = WdfIoTargetSendIoctlSynchronously(
        Target,
        NULL,
        IOCTL_GPIO_READ_PINS,
        NULL,
        &outputDescriptor,
        NULL,
        NULL);

    if (NT_SUCCESS(status))
    {
        *High = (value & 1) ? TRUE : FALSE;
    }

    return status;
}

VOID
GpioClosePin(
    _Inout_ WDFIOTARGET *Target
    )
{
    if (*Target != NULL)
    {
        WdfIoTargetClose(*Target);
        WdfObjectDelete(*Target);
        *Target = NULL;
    }
}
