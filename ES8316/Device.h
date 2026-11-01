/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Licensed under the GNU General Public License, version 2 only.
 * Provided without warranty. See LICENSE for the license text.
 */

#pragma once

#include "Tuning.h"

EXTERN_C_START

typedef struct _ES8316_CONFIG
{
    ULONG MclkHz;
    ULONG MclkId;
    ULONG MclkPolicy;
    ULONG BclkDiv;
    ULONG I2sMaster;
    ULONG SampleRate;
    ULONG WordLength;
    ULONG MicInput;
    ULONG MicBoost;
    ULONG MicAlc;
    ULONG MicPgaGain;
    ULONG HpVolume;
    ULONG HpMixerGain;
    ULONG DacVolume;
    ULONG AdcVolume;
    ULONG JackDetectInverted;
    ULONG JackPollIntervalMs;
    ULONG UseJackInterrupt;
    ULONG MuteWhenUnplugged;
    ULONG MicAlwaysOn;
    ULONG KeepPowerInD3;
    ULONG TraceLevel;
#define ES8316_DECLARE_TUNING(name, def, minimum, maximum) ULONG name;
    ES8316_TUNING_FIELDS(ES8316_DECLARE_TUNING)
#undef ES8316_DECLARE_TUNING
} ES8316_CONFIG, *PES8316_CONFIG;

typedef struct _DEVICE_CONTEXT
{
    WDFDEVICE FxDevice;

    SPB_CONTEXT I2c;
    LARGE_INTEGER PowerGpioId;
    WDFIOTARGET PowerGpio;
    BOOLEAN HaveInterruptResource;
    CM_PARTIAL_RESOURCE_DESCRIPTOR InterruptRaw;
    CM_PARTIAL_RESOURCE_DESCRIPTOR InterruptTranslated;
    WDFINTERRUPT JackInterrupt;

    WDFWAITLOCK CodecLock;

    WDFWORKITEM JackWorkItem;
    WDFTIMER JackPollTimer;
    ES8316_JACK_STATE JackState;
    BOOLEAN ButtonPressed;
    UCHAR LastGpioFlag;
    ULONG JackEvents;
    ULONG PollCount;
    BOOLEAN InterruptConnected;

    BOOLEAN CodecInitialized;
    BOOLEAN OutputEnabled;
    BOOLEAN MicEnabled;
    BOOLEAN InD0;
    NTSTATUS LastI2cStatus;

    ES8316_CONFIG Config;

    ES8316_MCLK_CONTEXT Mclk;

} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, DeviceGetContext)

NTSTATUS
Es8316CreateDevice(
    _Inout_ PWDFDEVICE_INIT DeviceInit
    );

EVT_WDF_DEVICE_PREPARE_HARDWARE Es8316EvtPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE Es8316EvtReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY Es8316EvtD0Entry;
EVT_WDF_DEVICE_D0_EXIT Es8316EvtD0Exit;
EVT_WDF_INTERRUPT_ISR Es8316EvtJackInterruptIsr;
EVT_WDF_WORKITEM Es8316EvtJackWorkItem;
EVT_WDF_TIMER Es8316EvtJackPollTimer;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL Es8316EvtIoDeviceControl;
EVT_WDF_DEVICE_CONTEXT_CLEANUP Es8316EvtDeviceContextCleanup;
EVT_WDF_DEVICE_SELF_MANAGED_IO_CLEANUP Es8316EvtSelfManagedIoCleanup;

EXTERN_C_END
