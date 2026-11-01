/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Licensed under the GNU General Public License, version 2 only.
 * Provided without warranty. See LICENSE for the license text.
 */

#include "Driver.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, Es8316CreateDevice)
#pragma alloc_text (PAGE, Es8316EvtPrepareHardware)
#pragma alloc_text (PAGE, Es8316EvtReleaseHardware)
#endif

#define ES8316_REG_ULONG(ctx, key, name, field, def) \
    Es8316ReadRegistryUlong((key), L##name, &(ctx)->Config.field, (def))

static
VOID
Es8316ReadRegistryUlong(
    _In_ WDFKEY Key,
    _In_ PCWSTR Name,
    _Out_ PULONG Value,
    _In_ ULONG Default
    )
{
    UNICODE_STRING valueName;
    NTSTATUS status;
    ULONG data;

    *Value = Default;

    if (Key == NULL)
    {
        return;
    }

    RtlInitUnicodeString(&valueName, Name);
    status = WdfRegistryQueryULong(Key, &valueName, &data);
    if (NT_SUCCESS(status))
    {
        *Value = data;
    }
}

static
VOID
Es8316ReadConfig(
    _In_ PDEVICE_CONTEXT Ctx
    )
{
    WDFKEY key;
    NTSTATUS status;

    key = NULL;
    status = WdfDeviceOpenRegistryKey(
        Ctx->FxDevice,
        PLUGPLAY_REGKEY_DEVICE,
        KEY_READ,
        WDF_NO_OBJECT_ATTRIBUTES,
        &key);

    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_WARNING, TRACE_REGISTRY,
              "device key unavailable (0x%08lX) - using defaults", status);
        key = NULL;
    }

    ES8316_REG_ULONG(Ctx, key, "MclkHz",              MclkHz,             12288000);
    ES8316_REG_ULONG(Ctx, key, "MclkId",              MclkId,             LPASS_CLOCK_ID_MCLK_1);
    ES8316_REG_ULONG(Ctx, key, "MclkPolicy",          MclkPolicy,         Es8316MclkAlwaysInD0);

    ES8316_REG_ULONG(Ctx, key, "I2sMaster",           I2sMaster,          1);

    ES8316_REG_ULONG(Ctx, key, "BclkDiv",             BclkDiv,            0);
    ES8316_REG_ULONG(Ctx, key, "SampleRate",          SampleRate,         48000);
    ES8316_REG_ULONG(Ctx, key, "WordLength",          WordLength,         16);
    ES8316_REG_ULONG(Ctx, key, "MicInput",            MicInput,           2);
    ES8316_REG_ULONG(Ctx, key, "MicBoost",            MicBoost,           0);
    ES8316_REG_ULONG(Ctx, key, "MicAlc",              MicAlc,             1);
    ES8316_REG_ULONG(Ctx, key, "MicPgaGain",          MicPgaGain,         6);
    ES8316_REG_ULONG(Ctx, key, "HpVolume",            HpVolume,           3);
    ES8316_REG_ULONG(Ctx, key, "HpMixerGain",         HpMixerGain,        11);
    ES8316_REG_ULONG(Ctx, key, "DacVolume",           DacVolume,          0);
    ES8316_REG_ULONG(Ctx, key, "AdcVolume",           AdcVolume,          0);
    ES8316_REG_ULONG(Ctx, key, "JackDetectInverted",  JackDetectInverted, 1);
    ES8316_REG_ULONG(Ctx, key, "JackPollIntervalMs",  JackPollIntervalMs, 1000);
    ES8316_REG_ULONG(Ctx, key, "UseJackInterrupt",    UseJackInterrupt,   0);
    ES8316_REG_ULONG(Ctx, key, "MuteWhenUnplugged",   MuteWhenUnplugged,  0);
    ES8316_REG_ULONG(Ctx, key, "MicAlwaysOn",         MicAlwaysOn,        0);
    ES8316_REG_ULONG(Ctx, key, "KeepPowerInD3",       KeepPowerInD3,      1);
    ES8316_REG_ULONG(Ctx, key, "TraceLevel",          TraceLevel,         TRACE_LEVEL_INFORMATION);

#define ES8316_WIDE_LITERAL_(text) L##text
#define ES8316_WIDE_LITERAL(text) ES8316_WIDE_LITERAL_(text)
#define ES8316_READ_TUNING(name, def, minimum, maximum) \
    Es8316ReadRegistryUlong(key, ES8316_WIDE_LITERAL(#name), &Ctx->Config.name, def); \
    Ctx->Config.name = Es8316BoundTuningValue(Ctx->Config.name, def, minimum, maximum);
    ES8316_TUNING_FIELDS(ES8316_READ_TUNING)
#undef ES8316_READ_TUNING
#undef ES8316_WIDE_LITERAL
#undef ES8316_WIDE_LITERAL_

    if (key != NULL)
    {
        WdfRegistryClose(key);
    }

    g_Es8316TraceLevel = Ctx->Config.TraceLevel;

    if (Ctx->Config.HpVolume > 3)       Ctx->Config.HpVolume = 3;
    if (Ctx->Config.HpMixerGain > 11)   Ctx->Config.HpMixerGain = 11;
    if (Ctx->Config.DacVolume > 192)    Ctx->Config.DacVolume = 192;
    if (Ctx->Config.AdcVolume > 192)    Ctx->Config.AdcVolume = 192;
    if (Ctx->Config.MicPgaGain > 10)    Ctx->Config.MicPgaGain = 10;
    if (Ctx->Config.MicInput > 2)       Ctx->Config.MicInput = 2;
    if (Ctx->Config.MclkPolicy > Es8316MclkAlwaysInD0) Ctx->Config.MclkPolicy = Es8316MclkAlwaysInD0;

    switch (Ctx->Config.WordLength)
    {
    case 16: case 18: case 20: case 24: case 32:
        break;
    default:
        Ctx->Config.WordLength = 16;
        break;
    }

    Trace(TRACE_LEVEL_INFORMATION, TRACE_REGISTRY,
          "config: mclk=%lu (id 0x%lX policy %lu) fs=%lu bits=%lu mic=%lu pga=%lu hpvol=%lu jdinv=%lu poll=%lums irq=%lu",
          Ctx->Config.MclkHz, Ctx->Config.MclkId, Ctx->Config.MclkPolicy,
          Ctx->Config.SampleRate, Ctx->Config.WordLength,
          Ctx->Config.MicInput, Ctx->Config.MicPgaGain, Ctx->Config.HpVolume,
          Ctx->Config.JackDetectInverted, Ctx->Config.JackPollIntervalMs,
          Ctx->Config.UseJackInterrupt);
    Trace(TRACE_LEVEL_INFORMATION, TRACE_REGISTRY,
          "output timing: hp enable/mute=%lu/%lu ms; dac ramp=%lu",
          Ctx->Config.HpEnableDelayMs, Ctx->Config.HpMuteDelayMs, Ctx->Config.DacRampRate);
}

NTSTATUS
Es8316CreateDevice(
    _Inout_ PWDFDEVICE_INIT DeviceInit
    )
{
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
    WDF_IO_QUEUE_CONFIG queueConfig;
    WDF_WORKITEM_CONFIG workItemConfig;
    WDF_TIMER_CONFIG timerConfig;
    PDEVICE_CONTEXT ctx;
    WDFDEVICE device;
    WDFQUEUE queue;
    NTSTATUS status;

    PAGED_CODE();

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
    pnpPowerCallbacks.EvtDevicePrepareHardware = Es8316EvtPrepareHardware;
    pnpPowerCallbacks.EvtDeviceReleaseHardware = Es8316EvtReleaseHardware;
    pnpPowerCallbacks.EvtDeviceD0Entry = Es8316EvtD0Entry;
    pnpPowerCallbacks.EvtDeviceD0Exit = Es8316EvtD0Exit;
    pnpPowerCallbacks.EvtDeviceSelfManagedIoCleanup = Es8316EvtSelfManagedIoCleanup;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);

    WdfDeviceInitSetIoType(DeviceInit, WdfDeviceIoBuffered);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DEVICE_CONTEXT);
    attributes.SynchronizationScope = WdfSynchronizationScopeNone;
    attributes.EvtCleanupCallback = Es8316EvtDeviceContextCleanup;

    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_ERROR, TRACE_PNP, "WdfDeviceCreate failed 0x%08lX", status);
        return status;
    }

    ctx = DeviceGetContext(device);
    RtlZeroMemory(ctx, sizeof(*ctx));
    ctx->FxDevice = device;
    ctx->JackState = Es8316JackUnknown;

    Es8316ReadConfig(ctx);

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = device;
    status = WdfWaitLockCreate(&attributes, &ctx->CodecLock);
    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_ERROR, TRACE_PNP, "WdfWaitLockCreate failed 0x%08lX", status);
        return status;
    }

    WDF_WORKITEM_CONFIG_INIT(&workItemConfig, Es8316EvtJackWorkItem);
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = device;
    status = WdfWorkItemCreate(&workItemConfig, &attributes, &ctx->JackWorkItem);
    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_ERROR, TRACE_PNP, "WdfWorkItemCreate failed 0x%08lX", status);
        return status;
    }

    WDF_TIMER_CONFIG_INIT(&timerConfig, Es8316EvtJackPollTimer);
    timerConfig.AutomaticSerialization = FALSE;
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = device;
    attributes.ExecutionLevel = WdfExecutionLevelPassive;
    status = WdfTimerCreate(&timerConfig, &attributes, &ctx->JackPollTimer);
    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_ERROR, TRACE_PNP, "WdfTimerCreate failed 0x%08lX", status);
        return status;
    }

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchSequential);
    queueConfig.EvtIoDeviceControl = Es8316EvtIoDeviceControl;
    queueConfig.PowerManaged = WdfTrue;

    status = WdfIoQueueCreate(device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &queue);
    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_ERROR, TRACE_PNP, "WdfIoQueueCreate failed 0x%08lX", status);
        return status;
    }

    status = WdfDeviceCreateDeviceInterface(device, &GUID_DEVINTERFACE_ES8316, NULL);
    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_WARNING, TRACE_PNP,
              "WdfDeviceCreateDeviceInterface failed 0x%08lX (diagnostics unavailable)", status);
        status = STATUS_SUCCESS;
    }

    status = Es8316MclkInitialize(device);
    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_WARNING, TRACE_PNP, "MCLK control unavailable 0x%08lX", status);
        status = STATUS_SUCCESS;
    }

    Trace(TRACE_LEVEL_INFORMATION, TRACE_PNP, "device created");

    return status;
}

NTSTATUS
Es8316EvtPrepareHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated
    )
{
    PDEVICE_CONTEXT ctx;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR res;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR resRaw;
    ULONG resourceCount;
    ULONG i;
    ULONG gpioCount;
    BOOLEAN haveI2c;
    NTSTATUS status;

    PAGED_CODE();

    ctx = DeviceGetContext(Device);
    gpioCount = 0;
    haveI2c = FALSE;
    ctx->HaveInterruptResource = FALSE;

    resourceCount = WdfCmResourceListGetCount(ResourcesTranslated);

    for (i = 0; i < resourceCount; i++)
    {
        res = WdfCmResourceListGetDescriptor(ResourcesTranslated, i);
        resRaw = WdfCmResourceListGetDescriptor(ResourcesRaw, i);

        switch (res->Type)
        {
        case CmResourceTypeConnection:
            if (res->u.Connection.Class == CM_RESOURCE_CONNECTION_CLASS_SERIAL &&
                res->u.Connection.Type == CM_RESOURCE_CONNECTION_TYPE_SERIAL_I2C)
            {
                if (!haveI2c)
                {
                    ctx->I2c.I2cResHubId.LowPart = res->u.Connection.IdLowPart;
                    ctx->I2c.I2cResHubId.HighPart = res->u.Connection.IdHighPart;
                    haveI2c = TRUE;
                }
            }
            else if (res->u.Connection.Class == CM_RESOURCE_CONNECTION_CLASS_GPIO &&
                     res->u.Connection.Type == CM_RESOURCE_CONNECTION_TYPE_GPIO_IO)
            {

                if (gpioCount == 0)
                {
                    ctx->PowerGpioId.LowPart = res->u.Connection.IdLowPart;
                    ctx->PowerGpioId.HighPart = res->u.Connection.IdHighPart;
                }
                gpioCount++;
            }
            break;

        case CmResourceTypeInterrupt:
            if (!ctx->HaveInterruptResource)
            {
                ctx->InterruptRaw = *resRaw;
                ctx->InterruptTranslated = *res;
                ctx->HaveInterruptResource = TRUE;
            }
            break;

        default:
            break;
        }
    }

    if (!haveI2c)
    {
        Trace(TRACE_LEVEL_ERROR, TRACE_PNP, "no I2C connection resource");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    Trace(TRACE_LEVEL_INFORMATION, TRACE_PNP,
          "resources: i2c=%08lX%08lX gpio=%lu irq=%lu",
          ctx->I2c.I2cResHubId.HighPart, ctx->I2c.I2cResHubId.LowPart,
          gpioCount, (ULONG)ctx->HaveInterruptResource);

    status = SpbTargetInitialize(Device, &ctx->I2c);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    if (gpioCount != 0)
    {
        status = GpioOpenPin(Device, ctx->PowerGpioId, GENERIC_READ | GENERIC_WRITE, &ctx->PowerGpio);
        if (!NT_SUCCESS(status))
        {
            Trace(TRACE_LEVEL_ERROR, TRACE_PNP, "cannot open supply-enable GPIO 0x%08lX", status);
            goto fail;
        }

        status = GpioWritePin(ctx->PowerGpio, TRUE);
        if (!NT_SUCCESS(status))
        {
            Trace(TRACE_LEVEL_ERROR, TRACE_PNP, "cannot assert supply-enable GPIO 0x%08lX", status);
            goto fail;
        }

        {
            LARGE_INTEGER delay;
            delay.QuadPart = -10 * 1000 * 10;
            KeDelayExecutionThread(KernelMode, FALSE, &delay);
        }
    }
    else
    {
        Trace(TRACE_LEVEL_WARNING, TRACE_PNP, "no GpioIo resource - assuming codec supply is on");
    }

    if (ctx->HaveInterruptResource && ctx->Config.UseJackInterrupt)
    {
        WDF_INTERRUPT_CONFIG interruptConfig;

        WDF_INTERRUPT_CONFIG_INIT(&interruptConfig, Es8316EvtJackInterruptIsr, NULL);
        interruptConfig.InterruptRaw = &ctx->InterruptRaw;
        interruptConfig.InterruptTranslated = &ctx->InterruptTranslated;
        interruptConfig.PassiveHandling = TRUE;

        status = WdfInterruptCreate(Device, &interruptConfig, WDF_NO_OBJECT_ATTRIBUTES, &ctx->JackInterrupt);
        if (!NT_SUCCESS(status))
        {

            Trace(TRACE_LEVEL_WARNING, TRACE_PNP,
                  "WdfInterruptCreate failed 0x%08lX - jack detect will poll", status);
            ctx->JackInterrupt = NULL;
            status = STATUS_SUCCESS;
        }
    }

    return STATUS_SUCCESS;

fail:
    GpioClosePin(&ctx->PowerGpio);
    SpbTargetDeinitialize(Device, &ctx->I2c);
    return status;
}

NTSTATUS
Es8316EvtReleaseHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesTranslated
    )
{
    PDEVICE_CONTEXT ctx;

    UNREFERENCED_PARAMETER(ResourcesTranslated);

    PAGED_CODE();

    ctx = DeviceGetContext(Device);

    if (ctx->JackInterrupt != NULL)
    {
        WdfObjectDelete(ctx->JackInterrupt);
        ctx->JackInterrupt = NULL;
    }

    if (ctx->PowerGpio != NULL)
    {
        if (!ctx->Config.KeepPowerInD3)
        {
            GpioWritePin(ctx->PowerGpio, FALSE);
        }
        GpioClosePin(&ctx->PowerGpio);
    }

    SpbTargetDeinitialize(Device, &ctx->I2c);

    return STATUS_SUCCESS;
}

NTSTATUS
Es8316EvtD0Entry(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE PreviousState
    )
{
    PDEVICE_CONTEXT ctx;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(PreviousState);

    ctx = DeviceGetContext(Device);

    Trace(TRACE_LEVEL_INFORMATION, TRACE_POWER, "D0 entry (from D%d)", PreviousState - WdfPowerDeviceD0);

    WdfWaitLockAcquire(ctx->CodecLock, NULL);

    ctx->JackEvents = 0;
    ctx->PollCount = 0;
    ctx->JackState = Es8316JackUnknown;
    ctx->ButtonPressed = FALSE;

    status = Es8316CodecInitialize(ctx);
    if (NT_SUCCESS(status))
    {
        Es8316JackSetInterruptEnabled(ctx, TRUE);
        Es8316JackEvaluate(ctx, FALSE);
    }

    ctx->InD0 = TRUE;

    WdfWaitLockRelease(ctx->CodecLock);

    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_ERROR, TRACE_POWER,
              "codec initialisation failed 0x%08lX - device stays started, retrying from the poll timer", status);
    }

    Es8316MclkSetWanted(Device, ctx->Config.MclkPolicy != Es8316MclkNever);

    if (ctx->Config.JackPollIntervalMs != 0)
    {
        WdfTimerStart(ctx->JackPollTimer, WDF_REL_TIMEOUT_IN_MS(ctx->Config.JackPollIntervalMs));
    }

    return STATUS_SUCCESS;
}

NTSTATUS
Es8316EvtD0Exit(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE TargetState
    )
{
    PDEVICE_CONTEXT ctx;

    ctx = DeviceGetContext(Device);

    Trace(TRACE_LEVEL_INFORMATION, TRACE_POWER, "D0 exit (to D%d)", TargetState - WdfPowerDeviceD0);

    WdfTimerStop(ctx->JackPollTimer, TRUE);
    WdfWorkItemFlush(ctx->JackWorkItem);

    WdfWaitLockAcquire(ctx->CodecLock, NULL);
    ctx->InD0 = FALSE;
    if (ctx->CodecInitialized)
    {
        Es8316JackSetInterruptEnabled(ctx, FALSE);
        Es8316CodecShutdown(ctx);
    }
    WdfWaitLockRelease(ctx->CodecLock);

    Es8316MclkSetWanted(Device, FALSE);

    return STATUS_SUCCESS;
}

BOOLEAN
Es8316EvtJackInterruptIsr(
    _In_ WDFINTERRUPT Interrupt,
    _In_ ULONG MessageID
    )
{
    PDEVICE_CONTEXT ctx;

    UNREFERENCED_PARAMETER(MessageID);

    ctx = DeviceGetContext(WdfInterruptGetDevice(Interrupt));

    ctx->InterruptConnected = TRUE;
    WdfWorkItemEnqueue(ctx->JackWorkItem);

    return TRUE;
}

VOID
Es8316EvtJackWorkItem(
    _In_ WDFWORKITEM WorkItem
    )
{
    PDEVICE_CONTEXT ctx;

    ctx = DeviceGetContext(WdfWorkItemGetParentObject(WorkItem));

    WdfWaitLockAcquire(ctx->CodecLock, NULL);
    if (ctx->InD0)
    {
        Es8316JackEvaluate(ctx, TRUE);
    }
    WdfWaitLockRelease(ctx->CodecLock);
}

VOID
Es8316EvtJackPollTimer(
    _In_ WDFTIMER Timer
    )
{
    PDEVICE_CONTEXT ctx;
    BOOLEAN rearm;

    ctx = DeviceGetContext(WdfTimerGetParentObject(Timer));
    rearm = FALSE;

    WdfWaitLockAcquire(ctx->CodecLock, NULL);
    if (ctx->InD0)
    {
        ctx->PollCount++;
        if (!ctx->CodecInitialized)
        {

            if (NT_SUCCESS(Es8316CodecInitialize(ctx)))
            {
                Trace(TRACE_LEVEL_INFORMATION, TRACE_POWER, "codec initialised on poll %lu", ctx->PollCount);
                Es8316JackSetInterruptEnabled(ctx, TRUE);
                ctx->JackState = Es8316JackUnknown;
            }
        }
        if (ctx->CodecInitialized)
        {
            Es8316JackEvaluate(ctx, FALSE);
        }
        rearm = TRUE;
    }
    WdfWaitLockRelease(ctx->CodecLock);

    if (rearm && ctx->Config.JackPollIntervalMs != 0)
    {
        WdfTimerStart(Timer, WDF_REL_TIMEOUT_IN_MS(ctx->Config.JackPollIntervalMs));
    }
}

VOID
Es8316EvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
    )
{
    PDEVICE_CONTEXT ctx;
    NTSTATUS status;
    size_t information;
    PVOID inBuf;
    PVOID outBuf;
    size_t inLen;
    size_t outLen;

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    ctx = DeviceGetContext(WdfIoQueueGetDevice(Queue));
    information = 0;
    inBuf = NULL;
    outBuf = NULL;
    inLen = 0;
    outLen = 0;

    if (!NT_SUCCESS(WdfRequestRetrieveInputBuffer(Request, 0, &inBuf, &inLen)))
    {
        inBuf = NULL;
        inLen = 0;
    }
    if (!NT_SUCCESS(WdfRequestRetrieveOutputBuffer(Request, 0, &outBuf, &outLen)))
    {
        outBuf = NULL;
        outLen = 0;
    }

    WdfWaitLockAcquire(ctx->CodecLock, NULL);

    if (!ctx->InD0)
    {
        status = STATUS_DEVICE_NOT_READY;
        goto done;
    }

    switch (IoControlCode)
    {
    case IOCTL_ES8316_READ_REGISTER:
    {
        PES8316_REGISTER_IO io;

        if (inLen < sizeof(ES8316_REGISTER_IO) || outLen < sizeof(ES8316_REGISTER_IO))
        {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        io = (PES8316_REGISTER_IO)outBuf;
        io->Register = ((PES8316_REGISTER_IO)inBuf)->Register;
        if (io->Register > ES8316_MAX_REGISTER)
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }
        status = Es8316RegRead(ctx, io->Register, &io->Value);
        information = sizeof(ES8316_REGISTER_IO);
        break;
    }

    case IOCTL_ES8316_WRITE_REGISTER:
    {
        PES8316_REGISTER_IO io;

        if (inLen < sizeof(ES8316_REGISTER_IO))
        {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        io = (PES8316_REGISTER_IO)inBuf;
        if (io->Register > ES8316_MAX_REGISTER)
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }
        status = Es8316RegWrite(ctx, io->Register, io->Value);
        break;
    }

    case IOCTL_ES8316_DUMP_REGISTERS:
    {
        PES8316_REGISTER_DUMP dump;
        UCHAR reg;

        if (outLen < sizeof(ES8316_REGISTER_DUMP))
        {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        dump = (PES8316_REGISTER_DUMP)outBuf;
        RtlZeroMemory(dump, sizeof(*dump));
        status = STATUS_SUCCESS;
        for (reg = 0; reg <= ES8316_MAX_REGISTER; reg++)
        {
            status = Es8316RegRead(ctx, reg, &dump->Values[reg]);
            if (!NT_SUCCESS(status))
            {
                break;
            }
        }
        dump->Count = ES8316_MAX_REGISTER + 1;
        information = sizeof(ES8316_REGISTER_DUMP);
        break;
    }

    case IOCTL_ES8316_GET_STATUS:
    {
        PES8316_STATUS st;
        size_t statusSize;

        if (outLen < ES8316_STATUS_VERSION_2_SIZE)
        {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        statusSize = outLen >= sizeof(ES8316_STATUS) ?
            sizeof(ES8316_STATUS) : ES8316_STATUS_VERSION_2_SIZE;
        st = (PES8316_STATUS)outBuf;
        RtlZeroMemory(st, statusSize);
        st->Version = statusSize == sizeof(ES8316_STATUS) ? ES8316_STATUS_VERSION : 2;
        st->JackState = (ULONG)ctx->JackState;
        st->ButtonPressed = ctx->ButtonPressed;
        st->GpioFlag = ctx->LastGpioFlag;
        st->CodecInitialized = ctx->CodecInitialized;
        st->InterruptConnected = ctx->InterruptConnected;
        st->JackEvents = ctx->JackEvents;
        st->PollCount = ctx->PollCount;
        st->LastI2cStatus = (ULONG)ctx->LastI2cStatus;
        st->MclkHz = ctx->Config.MclkHz;
        st->SampleRate = ctx->Config.SampleRate;
        st->WordLength = ctx->Config.WordLength;
        st->OutputEnabled = ctx->OutputEnabled;
        st->MicEnabled = ctx->MicEnabled;
        st->MclkPolicy = ctx->Config.MclkPolicy;
        st->MclkInterface = ctx->Mclk.InterfaceAcquired;
        st->MclkVoted = ctx->Mclk.ClockEnabled;
        st->MclkLastStatus = (ULONG)ctx->Mclk.LastStatus;
        st->MclkRetries = ctx->Mclk.Retries;
        if (statusSize == sizeof(ES8316_STATUS))
        {
            st->MclkLastDspError = ctx->Mclk.LastDspError;
        }
        information = statusSize;
        status = STATUS_SUCCESS;
        break;
    }

    case IOCTL_ES8316_SET_OUTPUT:
    {
        PES8316_SET_OUTPUT so;

        if (inLen < sizeof(ES8316_SET_OUTPUT))
        {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        so = (PES8316_SET_OUTPUT)inBuf;
        status = Es8316CodecSetVolumes(ctx, so->HpVolume, so->DacVolume);
        if (NT_SUCCESS(status))
        {
            status = Es8316CodecSetOutputEnabled(ctx, so->HeadphoneEnable ? TRUE : FALSE);
        }
        if (NT_SUCCESS(status))
        {
            status = Es8316CodecSetMicEnabled(ctx, so->MicEnable ? TRUE : FALSE);
        }
        break;
    }

    case IOCTL_ES8316_REINITIALIZE:
        status = Es8316CodecInitialize(ctx);
        if (NT_SUCCESS(status))
        {
            Es8316JackSetInterruptEnabled(ctx, TRUE);
            ctx->JackState = Es8316JackUnknown;
            Es8316JackEvaluate(ctx, FALSE);
        }
        break;

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

done:
    WdfWaitLockRelease(ctx->CodecLock);

    WdfRequestCompleteWithInformation(Request, status, information);
}

VOID
Es8316EvtSelfManagedIoCleanup(
    _In_ WDFDEVICE Device
    )
{

    Es8316MclkCleanup(Device);
}

VOID
Es8316EvtDeviceContextCleanup(
    _In_ WDFOBJECT Device
    )
{

    Es8316MclkUnregisterNotification((WDFDEVICE)Device);
}
