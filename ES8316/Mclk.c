/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Licensed under the GNU General Public License, version 2 only.
 * Provided without warranty. See LICENSE for the license text.
 */

#include "Driver.h"
#include <wdmguid.h>

#define ES8316_MCLK_RETRY_MS        2000
#define ES8316_MCLK_MAX_RETRIES     60

static
PES8316_MCLK_CONTEXT
Es8316MclkFromDevice(
    _In_ WDFDEVICE Device
    )
{
    return &DeviceGetContext(Device)->Mclk;
}

static NTSTATUS Es8316MclkRequest(_In_ PES8316_MCLK_CONTEXT M, _In_ BOOLEAN Enable);

EVT_WDF_IO_TARGET_QUERY_REMOVE    Es8316MclkEvtTargetQueryRemove;
EVT_WDF_IO_TARGET_REMOVE_CANCELED Es8316MclkEvtTargetRemoveCanceled;
EVT_WDF_IO_TARGET_REMOVE_COMPLETE Es8316MclkEvtTargetRemoveComplete;

static
VOID
Es8316MclkDropInterface(
    _In_ PES8316_MCLK_CONTEXT M,
    _In_ BOOLEAN TargetAlive
    )

{
    if (M->InterfaceAcquired)
    {
        if (TargetAlive && M->Interface.InterfaceHeader.InterfaceDereference != NULL)
        {
            M->Interface.InterfaceHeader.InterfaceDereference(M->Interface.InterfaceHeader.Context);
        }
        M->InterfaceAcquired = FALSE;
    }

    RtlZeroMemory(&M->Interface, sizeof(M->Interface));

    if (M->Target != NULL)
    {
        WdfIoTargetClose(M->Target);
        WdfObjectDelete(M->Target);
        M->Target = NULL;
        M->TargetGone = FALSE;
    }

    M->ClockEnabled = FALSE;
}

static
NTSTATUS
Es8316MclkAcquireInterface(
    _In_ PES8316_MCLK_CONTEXT M
    )
{
    NTSTATUS status;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_IO_TARGET_OPEN_PARAMS openParams;

    if (M->InterfaceAcquired)
    {
        return STATUS_SUCCESS;
    }

    if (M->SymbolicLink.Length == 0)
    {
        return STATUS_DEVICE_NOT_CONNECTED;
    }

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = M->FxDevice;

    status = WdfIoTargetCreate(M->FxDevice, &attributes, &M->Target);
    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_ERROR, TRACE_DRIVER, "mclk: WdfIoTargetCreate failed 0x%08lX", status);
        M->Target = NULL;
        return status;
    }

    WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_NAME(&openParams, &M->SymbolicLink, GENERIC_READ | GENERIC_WRITE);
    openParams.ShareAccess = FILE_SHARE_READ | FILE_SHARE_WRITE;

    openParams.EvtIoTargetQueryRemove    = Es8316MclkEvtTargetQueryRemove;
    openParams.EvtIoTargetRemoveCanceled = Es8316MclkEvtTargetRemoveCanceled;
    openParams.EvtIoTargetRemoveComplete = Es8316MclkEvtTargetRemoveComplete;

    status = WdfIoTargetOpen(M->Target, &openParams);
    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_WARNING, TRACE_DRIVER, "mclk: cannot open ADCM interface %wZ (0x%08lX)",
              &M->SymbolicLink, status);
        WdfObjectDelete(M->Target);
        M->Target = NULL;
        return status;
    }

    RtlZeroMemory(&M->Interface, sizeof(M->Interface));

    status = WdfIoTargetQueryForInterface(
        M->Target,
        &GUID_ADCM_AUDIOHWRSC_INTERFACE,
        &M->Interface.InterfaceHeader,
        sizeof(M->Interface),
        ADCM_AUDIOHWRSC_INTERFACE_VERSION,
        NULL);

    if (!NT_SUCCESS(status) || M->Interface.AudioHwRscIoctl == NULL)
    {
        Trace(TRACE_LEVEL_WARNING, TRACE_DRIVER,
              "mclk: AudioHwRsc query-interface failed 0x%08lX (size %u ver %u method %p)",
              status, M->Interface.InterfaceHeader.Size, M->Interface.InterfaceHeader.Version,
              M->Interface.AudioHwRscIoctl);
        if (NT_SUCCESS(status))
        {
            status = STATUS_NOINTERFACE;
        }
        WdfIoTargetClose(M->Target);
        WdfObjectDelete(M->Target);
        M->Target = NULL;
        return status;
    }

    M->InterfaceAcquired = TRUE;

    Trace(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "mclk: ADCM AudioHwRsc interface acquired (%wZ)",
          &M->SymbolicLink);

    return STATUS_SUCCESS;
}

static
NTSTATUS
Es8316ClockRequest(
    _In_ PES8316_MCLK_CONTEXT M,
    _In_ ULONG RequestClockId,
    _In_ ULONG RequestClockHz,
    _In_ BOOLEAN Enable
    )
{
    NTSTATUS status;
    NTSTATUS transportStatus;
    ULONG dspError;
    ULONG clockId;
    ULONG clockHz;
    union
    {
        ADCM_HW_CLK_REQUEST Request;
        ADCM_HW_CLK_RELEASE Release;
    } payload;
    ADCM_HW_RSC_DESCRIPTOR descriptor;
    PDEVICE_CONTEXT ctx;

    ctx = DeviceGetContext(M->FxDevice);
    UNREFERENCED_PARAMETER(ctx);
    clockId = RequestClockId;
    clockHz = RequestClockHz;
    RtlZeroMemory(&payload, sizeof(payload));

    if (Enable)
    {
        payload.Request.ModuleInstanceId = 0;
        payload.Request.ParamId = PARAM_ID_RSC_AUDIO_HW_CLK;
        payload.Request.ParamSize = sizeof(payload.Request) - FIELD_OFFSET(ADCM_HW_CLK_REQUEST, NumClockId);
        payload.Request.NumClockId = 1;
        payload.Request.ClockId = clockId;
        payload.Request.ClockFreqHz = clockHz;
        payload.Request.ClockAttribute = LPASS_CLOCK_ATTRIBUTE_COUPLE_NO;
        payload.Request.ClockRoot = LPASS_CLOCK_ROOT_DEFAULT;

        descriptor.Payload = &payload.Request;
        descriptor.PayloadSize = sizeof(payload.Request);
    }
    else
    {
        payload.Release.ModuleInstanceId = 0;
        payload.Release.ParamId = PARAM_ID_RSC_AUDIO_HW_CLK;
        payload.Release.ParamSize = sizeof(payload.Release) - FIELD_OFFSET(ADCM_HW_CLK_RELEASE, NumClockId);
        payload.Release.NumClockId = 1;
        payload.Release.ClockId = clockId;

        descriptor.Payload = &payload.Release;
        descriptor.PayloadSize = sizeof(payload.Release);
    }
    descriptor.Reserved = 0;

    transportStatus = M->Interface.AudioHwRscIoctl(
        Enable ? ADCM_AUDIOHWRSC_CMD_ENABLE_CLOCK : ADCM_AUDIOHWRSC_CMD_DISABLE_CLOCK,
        &descriptor);

    dspError = Enable ? payload.Request.ErrorCode : payload.Release.ErrorCode;
    M->LastDspError = dspError;
    status = transportStatus;
    if (NT_SUCCESS(transportStatus) && dspError != 0)
    {

        status = STATUS_UNSUCCESSFUL;
    }

    Trace(NT_SUCCESS(status) ? TRACE_LEVEL_INFORMATION : TRACE_LEVEL_WARNING, TRACE_DRIVER,
          "mclk: %s clock 0x%lX @ %lu Hz -> transport 0x%08lX, DSP 0x%08lX, final 0x%08lX",
          Enable ? "ENABLE" : "DISABLE", clockId, clockHz,
          transportStatus, dspError, status);

    return status;
}

NTSTATUS
Es8316MclkEvtTargetQueryRemove(
    _In_ WDFIOTARGET IoTarget
    )
{
    PES8316_MCLK_CONTEXT m;

    m = Es8316MclkFromDevice((WDFDEVICE)WdfIoTargetGetDevice(IoTarget));

    WdfWaitLockAcquire(m->Lock, NULL);
    if (m->Target == IoTarget)
    {
        if (m->ClockEnabled && m->InterfaceAcquired)
        {
            (VOID)Es8316MclkRequest(m, FALSE);
        }
        if (m->InterfaceAcquired && m->Interface.InterfaceHeader.InterfaceDereference != NULL)
        {
            m->Interface.InterfaceHeader.InterfaceDereference(m->Interface.InterfaceHeader.Context);
        }
        m->InterfaceAcquired = FALSE;
        RtlZeroMemory(&m->Interface, sizeof(m->Interface));
        m->ClockEnabled = FALSE;
        m->TargetGone = TRUE;
        Trace(TRACE_LEVEL_WARNING, TRACE_DRIVER,
              "mclk: ADCM is being removed (query-remove) - vote and interface released");
    }
    WdfWaitLockRelease(m->Lock);

    WdfIoTargetCloseForQueryRemove(IoTarget);
    return STATUS_SUCCESS;
}

VOID
Es8316MclkEvtTargetRemoveCanceled(
    _In_ WDFIOTARGET IoTarget
    )
{
    PES8316_MCLK_CONTEXT m;

    m = Es8316MclkFromDevice((WDFDEVICE)WdfIoTargetGetDevice(IoTarget));
    Trace(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "mclk: ADCM removal cancelled - re-acquiring");
    WdfWorkItemEnqueue(m->WorkItem);
}

VOID
Es8316MclkEvtTargetRemoveComplete(
    _In_ WDFIOTARGET IoTarget
    )
{
    PES8316_MCLK_CONTEXT m;

    m = Es8316MclkFromDevice((WDFDEVICE)WdfIoTargetGetDevice(IoTarget));

    WdfWaitLockAcquire(m->Lock, NULL);
    if (m->Target == IoTarget)
    {

        m->InterfaceAcquired = FALSE;
        RtlZeroMemory(&m->Interface, sizeof(m->Interface));
        m->ClockEnabled = FALSE;
        m->TargetGone = TRUE;
        Trace(TRACE_LEVEL_WARNING, TRACE_DRIVER, "mclk: ADCM removed - vote lost");
    }
    WdfWaitLockRelease(m->Lock);

    WdfWorkItemEnqueue(m->WorkItem);
}

static
NTSTATUS
Es8316MclkRequest(
    _In_ PES8316_MCLK_CONTEXT M,
    _In_ BOOLEAN Enable
    )
{
    PDEVICE_CONTEXT ctx = DeviceGetContext(M->FxDevice);

    return Es8316ClockRequest(M, ctx->Config.MclkId, ctx->Config.MclkHz, Enable);
}

static
VOID
Es8316MclkReconcile(
    _In_ PES8316_MCLK_CONTEXT M
    )

{
    NTSTATUS status;
    BOOLEAN retry;

    retry = FALSE;

    if (M->TargetGone && M->Target != NULL)
    {

        Es8316MclkDropInterface(M, FALSE);
    }

    if (M->WantClock)
    {
        if (!M->InterfaceAcquired)
        {
            status = Es8316MclkAcquireInterface(M);
            if (!NT_SUCCESS(status))
            {
                M->LastStatus = status;
                retry = (M->SymbolicLink.Length != 0);
            }
        }

        if (M->InterfaceAcquired && !M->ClockEnabled)
        {
            status = Es8316MclkRequest(M, TRUE);
            M->LastStatus = status;
            if (NT_SUCCESS(status))
            {
                M->ClockEnabled = TRUE;
                M->Retries = 0;

                WdfWaitLockAcquire(DeviceGetContext(M->FxDevice)->CodecLock, NULL);
                if (DeviceGetContext(M->FxDevice)->InD0)
                {
                    Es8316CodecInitialize(DeviceGetContext(M->FxDevice));
                    Es8316JackSetInterruptEnabled(DeviceGetContext(M->FxDevice), TRUE);
                    DeviceGetContext(M->FxDevice)->JackState = Es8316JackUnknown;
                    Es8316JackEvaluate(DeviceGetContext(M->FxDevice), FALSE);
                }
                WdfWaitLockRelease(DeviceGetContext(M->FxDevice)->CodecLock);
            }
            else
            {
                retry = TRUE;
            }
        }
    }
    else
    {
        if (M->ClockEnabled && M->InterfaceAcquired)
        {
            status = Es8316MclkRequest(M, FALSE);
            M->LastStatus = status;
            if (NT_SUCCESS(status))
            {
                M->ClockEnabled = FALSE;
                M->Retries = 0;
            }
            else
            {
                retry = TRUE;
            }
        }
        else
        {
            M->Retries = 0;
        }
    }

    if (retry)
    {
        if (M->Retries < ES8316_MCLK_MAX_RETRIES)
        {
            M->Retries++;
            WdfTimerStart(M->RetryTimer, WDF_REL_TIMEOUT_IN_MS(ES8316_MCLK_RETRY_MS));
        }
        else
        {
            Trace(TRACE_LEVEL_ERROR, TRACE_DRIVER,
                  "mclk: giving up after %lu attempts (last 0x%08lX, DSP 0x%08lX) - unable to %s MCLK_1",
                  M->Retries, M->LastStatus, M->LastDspError,
                  M->WantClock ? "vote" : "release");
        }
    }
}

static
VOID
Es8316MclkEvtWorkItem(
    _In_ WDFWORKITEM WorkItem
    )
{
    PES8316_MCLK_CONTEXT m;

    m = Es8316MclkFromDevice((WDFDEVICE)WdfWorkItemGetParentObject(WorkItem));

    WdfWaitLockAcquire(m->Lock, NULL);

    if (m->ArrivalPending)
    {
        m->ArrivalPending = FALSE;
    }

    Es8316MclkReconcile(m);

    WdfWaitLockRelease(m->Lock);
}

static
VOID
Es8316MclkEvtRetryTimer(
    _In_ WDFTIMER Timer
    )
{
    PES8316_MCLK_CONTEXT m;

    m = Es8316MclkFromDevice((WDFDEVICE)WdfTimerGetParentObject(Timer));
    WdfWorkItemEnqueue(m->WorkItem);
}

static
NTSTATUS
Es8316MclkInterfaceNotification(
    _In_ PVOID NotificationStructure,
    _Inout_opt_ PVOID Context
    )
{
    PDEVICE_INTERFACE_CHANGE_NOTIFICATION notification;
    PES8316_MCLK_CONTEXT m;
    WDFDEVICE device;

    notification = (PDEVICE_INTERFACE_CHANGE_NOTIFICATION)NotificationStructure;
    device = (WDFDEVICE)Context;
    if (device == NULL)
    {
        return STATUS_SUCCESS;
    }

    m = Es8316MclkFromDevice(device);

    if (notification->SymbolicLinkName == NULL)
    {
        return STATUS_SUCCESS;
    }

    WdfWaitLockAcquire(m->Lock, NULL);

    if (IsEqualGUID(&notification->Event, &GUID_DEVICE_INTERFACE_ARRIVAL))
    {

        if (m->SymbolicLink.Length == 0 ||
            RtlCompareUnicodeString(&m->SymbolicLink, notification->SymbolicLinkName, TRUE) != 0)
        {
            Es8316MclkDropInterface(m, FALSE);
            RtlCopyUnicodeString(&m->SymbolicLink, notification->SymbolicLinkName);
        }
        m->ArrivalPending = TRUE;
        m->Retries = 0;
        Trace(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "mclk: ADCM interface arrived");
    }
    else if (IsEqualGUID(&notification->Event, &GUID_DEVICE_INTERFACE_REMOVAL))
    {
        if (m->SymbolicLink.Length != 0 &&
            RtlCompareUnicodeString(&m->SymbolicLink, notification->SymbolicLinkName, TRUE) == 0)
        {

            Trace(TRACE_LEVEL_WARNING, TRACE_DRIVER, "mclk: ADCM interface removed (deferred notice) - forgotten");
            Es8316MclkDropInterface(m, FALSE);
            m->SymbolicLink.Length = 0;
        }
    }

    WdfWaitLockRelease(m->Lock);

    WdfWorkItemEnqueue(m->WorkItem);

    return STATUS_SUCCESS;
}

NTSTATUS
Es8316MclkInitialize(
    _In_ WDFDEVICE Device
    )
{
    PES8316_MCLK_CONTEXT m;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_WORKITEM_CONFIG workItemConfig;
    WDF_TIMER_CONFIG timerConfig;
    NTSTATUS status;

    m = Es8316MclkFromDevice(Device);
    RtlZeroMemory(m, sizeof(*m));
    m->FxDevice = Device;
    RtlInitEmptyUnicodeString(&m->SymbolicLink, m->SymbolicLinkBuffer, sizeof(m->SymbolicLinkBuffer));

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = Device;
    status = WdfWaitLockCreate(&attributes, &m->Lock);
    if (!NT_SUCCESS(status))
    {
        m->Lock = NULL;
        m->LastStatus = status;
        return status;
    }

    WDF_WORKITEM_CONFIG_INIT(&workItemConfig, Es8316MclkEvtWorkItem);
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = Device;
    status = WdfWorkItemCreate(&workItemConfig, &attributes, &m->WorkItem);
    if (!NT_SUCCESS(status))
    {
        m->WorkItem = NULL;
        goto AllocationFailed;
    }

    WDF_TIMER_CONFIG_INIT(&timerConfig, Es8316MclkEvtRetryTimer);
    timerConfig.AutomaticSerialization = FALSE;
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = Device;
    attributes.ExecutionLevel = WdfExecutionLevelPassive;
    status = WdfTimerCreate(&timerConfig, &attributes, &m->RetryTimer);
    if (!NT_SUCCESS(status))
    {
        m->RetryTimer = NULL;
        goto AllocationFailed;
    }

    if (DeviceGetContext(Device)->Config.MclkPolicy == Es8316MclkNever)
    {
        Trace(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "mclk: policy=never, not registering for ADCM");
        return STATUS_SUCCESS;
    }

    status = IoRegisterPlugPlayNotification(
        EventCategoryDeviceInterfaceChange,
        PNPNOTIFY_DEVICE_INTERFACE_INCLUDE_EXISTING_INTERFACES,
        (PVOID)&GUID_ADCM_AUDIOHWRSC_INTERFACE,
        WdfDriverWdmGetDriverObject(WdfDeviceGetDriver(Device)),
        Es8316MclkInterfaceNotification,
        (PVOID)Device,
        &m->NotificationEntry);

    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_ERROR, TRACE_DRIVER, "mclk: IoRegisterPlugPlayNotification failed 0x%08lX", status);
        m->NotificationEntry = NULL;
        m->LastStatus = status;
        return status;
    }

    return STATUS_SUCCESS;

AllocationFailed:
    /* No callbacks can run before notification registration. Leave Lock NULL
       so D0 entry/exit cannot queue an incompletely initialized worker. */
    if (m->WorkItem != NULL)
    {
        WdfObjectDelete(m->WorkItem);
        m->WorkItem = NULL;
    }
    WdfObjectDelete(m->Lock);
    m->Lock = NULL;
    m->LastStatus = status;
    return status;
}

VOID
Es8316MclkSetWanted(
    _In_ WDFDEVICE Device,
    _In_ BOOLEAN Wanted
    )
{
    PES8316_MCLK_CONTEXT m;

    m = Es8316MclkFromDevice(Device);

    if (m->Lock == NULL)
    {
        return;
    }

    WdfWaitLockAcquire(m->Lock, NULL);
    m->WantClock = Wanted;
    if (!Wanted)
    {
        WdfTimerStop(m->RetryTimer, FALSE);
        m->Retries = 0;
    }
    WdfWaitLockRelease(m->Lock);

    if (m->WorkItem != NULL)
    {
        WdfWorkItemEnqueue(m->WorkItem);
        if (!Wanted)
        {

            WdfWorkItemFlush(m->WorkItem);
        }
    }
}

VOID
Es8316MclkUnregisterNotification(
    _In_ WDFDEVICE Device
    )
{
    PES8316_MCLK_CONTEXT m;

    m = Es8316MclkFromDevice(Device);

    if (m->NotificationEntry != NULL)
    {

        IoUnregisterPlugPlayNotificationEx(m->NotificationEntry);
        m->NotificationEntry = NULL;
    }
}

VOID
Es8316MclkCleanup(
    _In_ WDFDEVICE Device
    )
{
    PES8316_MCLK_CONTEXT m;
    NTSTATUS status;

    m = Es8316MclkFromDevice(Device);

    Es8316MclkUnregisterNotification(Device);

    if (m->Lock != NULL)
    {
        WdfWaitLockAcquire(m->Lock, NULL);
        m->WantClock = FALSE;
        if (m->ClockEnabled && m->InterfaceAcquired)
        {
            status = Es8316MclkRequest(m, FALSE);
            m->LastStatus = status;
            if (NT_SUCCESS(status))
            {
                m->ClockEnabled = FALSE;
            }
            else
            {
                Trace(TRACE_LEVEL_WARNING, TRACE_DRIVER,
                      "mclk: final release failed 0x%08lX (DSP 0x%08lX); dropping ADCM session",
                      status, m->LastDspError);
            }
        }
        Es8316MclkDropInterface(m, TRUE);
        WdfWaitLockRelease(m->Lock);
    }

    /* Clear the desired state and drop the interface before draining work.
       Otherwise a pending reconcile can rearm the timer after it was stopped
       (including a failed release retry). With no vote/interface remaining,
       neither queued work nor the final timer callback can schedule a retry. */
    if (m->RetryTimer != NULL)
    {
        WdfTimerStop(m->RetryTimer, TRUE);
    }

    if (m->WorkItem != NULL)
    {
        WdfWorkItemFlush(m->WorkItem);
    }
}
