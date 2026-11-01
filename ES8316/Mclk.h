/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Licensed under the GNU General Public License, version 2 only.
 * Provided without warranty. See LICENSE for the license text.
 */

#pragma once

EXTERN_C_START

DEFINE_GUID(GUID_ADCM_AUDIOHWRSC_INTERFACE,
    0x5be1e2d0, 0x4323, 0x4d19, 0x81, 0xfe, 0x5c, 0x41, 0x85, 0xae, 0x7c, 0x05);

#define ADCM_AUDIOHWRSC_INTERFACE_VERSION   1

#define ADCM_AUDIOHWRSC_CMD_ENABLE_HW_CORE  3
#define ADCM_AUDIOHWRSC_CMD_DISABLE_HW_CORE 4
#define ADCM_AUDIOHWRSC_CMD_ENABLE_CLOCK    5
#define ADCM_AUDIOHWRSC_CMD_DISABLE_CLOCK   6

#define PARAM_ID_RSC_AUDIO_HW_CLK           0x0800102C
#define LPASS_CLOCK_ID_MCLK_1               0x00000300
#define LPASS_CLOCK_ATTRIBUTE_COUPLE_NO     1
#define LPASS_CLOCK_ROOT_DEFAULT            0

#pragma pack(push, 4)

typedef struct _ADCM_HW_CLK_REQUEST
{
    ULONG ModuleInstanceId;
    ULONG ParamId;
    ULONG ParamSize;
    ULONG ErrorCode;
    ULONG NumClockId;
    ULONG ClockId;
    ULONG ClockFreqHz;
    ULONG ClockAttribute;
    ULONG ClockRoot;
} ADCM_HW_CLK_REQUEST, *PADCM_HW_CLK_REQUEST;

typedef struct _ADCM_HW_CLK_RELEASE
{
    ULONG ModuleInstanceId;
    ULONG ParamId;
    ULONG ParamSize;
    ULONG ErrorCode;
    ULONG NumClockId;
    ULONG ClockId;
} ADCM_HW_CLK_RELEASE, *PADCM_HW_CLK_RELEASE;

C_ASSERT(sizeof(ADCM_HW_CLK_REQUEST) == 0x24);
C_ASSERT(sizeof(ADCM_HW_CLK_RELEASE) == 0x18);

#pragma pack(pop)

typedef struct _ADCM_HW_RSC_DESCRIPTOR
{
    PVOID Payload;
    ULONG PayloadSize;
    ULONG Reserved;
} ADCM_HW_RSC_DESCRIPTOR, *PADCM_HW_RSC_DESCRIPTOR;

typedef
NTSTATUS
(*PADCM_AUDIOHWRSC_IOCTL)(
    _In_ ULONG Command,
    _In_ PADCM_HW_RSC_DESCRIPTOR Request
    );

typedef struct _ADCM_AUDIOHWRSC_INTERFACE
{
    INTERFACE InterfaceHeader;
    PADCM_AUDIOHWRSC_IOCTL AudioHwRscIoctl;
} ADCM_AUDIOHWRSC_INTERFACE, *PADCM_AUDIOHWRSC_INTERFACE;

C_ASSERT(sizeof(ADCM_AUDIOHWRSC_INTERFACE) == 0x28);

typedef enum _ES8316_MCLK_POLICY
{
    Es8316MclkNever = 0,
    Es8316MclkAlwaysInD0 = 1,
} ES8316_MCLK_POLICY;

typedef struct _ES8316_MCLK_CONTEXT
{
    WDFDEVICE FxDevice;
    PVOID NotificationEntry;
    WDFWORKITEM WorkItem;
    WDFTIMER RetryTimer;
    WDFWAITLOCK Lock;

    UNICODE_STRING SymbolicLink;
    WCHAR SymbolicLinkBuffer[256];

    WDFIOTARGET Target;
    ADCM_AUDIOHWRSC_INTERFACE Interface;
    BOOLEAN InterfaceAcquired;
    BOOLEAN TargetGone;
    BOOLEAN ClockEnabled;
    BOOLEAN WantClock;
    BOOLEAN ArrivalPending;
    ULONG Retries;
    NTSTATUS LastStatus;
    ULONG LastDspError;
} ES8316_MCLK_CONTEXT, *PES8316_MCLK_CONTEXT;

NTSTATUS
Es8316MclkInitialize(
    _In_ WDFDEVICE Device
    );

VOID
Es8316MclkCleanup(
    _In_ WDFDEVICE Device
    );

VOID
Es8316MclkUnregisterNotification(
    _In_ WDFDEVICE Device
    );

VOID
Es8316MclkSetWanted(
    _In_ WDFDEVICE Device,
    _In_ BOOLEAN Wanted
    );

EXTERN_C_END
