/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Licensed under the GNU General Public License, version 2 only.
 * Provided without warranty. See LICENSE for the license text.
 */

#include "Driver.h"

ULONG g_Es8316TraceLevel = TRACE_LEVEL_INFORMATION;

#ifdef ALLOC_PRAGMA
#pragma alloc_text (INIT, DriverEntry)
#pragma alloc_text (PAGE, Es8316EvtDeviceAdd)
#pragma alloc_text (PAGE, Es8316EvtDriverContextCleanup)
#endif

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    WDF_DRIVER_CONFIG config;
    WDF_OBJECT_ATTRIBUTES attributes;
    NTSTATUS status;

    Trace(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "DriverEntry");

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.EvtCleanupCallback = Es8316EvtDriverContextCleanup;

    WDF_DRIVER_CONFIG_INIT(&config, Es8316EvtDeviceAdd);

    status = WdfDriverCreate(
        DriverObject,
        RegistryPath,
        &attributes,
        &config,
        WDF_NO_HANDLE);

    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_ERROR, TRACE_DRIVER, "WdfDriverCreate failed 0x%08lX", status);
    }

    return status;
}

NTSTATUS
Es8316EvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
    )
{
    UNREFERENCED_PARAMETER(Driver);

    PAGED_CODE();

    return Es8316CreateDevice(DeviceInit);
}

VOID
Es8316EvtDriverContextCleanup(
    _In_ WDFOBJECT DriverObject
    )
{
    UNREFERENCED_PARAMETER(DriverObject);

    PAGED_CODE();

    Trace(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "driver unload");
}
