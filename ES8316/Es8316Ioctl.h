/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Licensed under the GNU General Public License, version 2 only.
 * Provided without warranty. See LICENSE for the license text.
 */

#pragma once

DEFINE_GUID(GUID_DEVINTERFACE_ES8316,
    0x5e2b8c1a, 0x9f6d, 0x4c3b, 0x8e, 0x7a, 0x2d, 0x10, 0x83, 0x16, 0x00, 0x01);

#define ES8316_IOCTL_INDEX_BASE 0x800

#define IOCTL_ES8316_READ_REGISTER \
    CTL_CODE(FILE_DEVICE_UNKNOWN, ES8316_IOCTL_INDEX_BASE + 0, METHOD_BUFFERED, FILE_READ_ACCESS)

#define IOCTL_ES8316_WRITE_REGISTER \
    CTL_CODE(FILE_DEVICE_UNKNOWN, ES8316_IOCTL_INDEX_BASE + 1, METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define IOCTL_ES8316_DUMP_REGISTERS \
    CTL_CODE(FILE_DEVICE_UNKNOWN, ES8316_IOCTL_INDEX_BASE + 2, METHOD_BUFFERED, FILE_READ_ACCESS)

#define IOCTL_ES8316_GET_STATUS \
    CTL_CODE(FILE_DEVICE_UNKNOWN, ES8316_IOCTL_INDEX_BASE + 3, METHOD_BUFFERED, FILE_READ_ACCESS)

#define IOCTL_ES8316_SET_OUTPUT \
    CTL_CODE(FILE_DEVICE_UNKNOWN, ES8316_IOCTL_INDEX_BASE + 4, METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define IOCTL_ES8316_REINITIALIZE \
    CTL_CODE(FILE_DEVICE_UNKNOWN, ES8316_IOCTL_INDEX_BASE + 5, METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define IOCTL_ES8316_GET_PERSISTENT_STATUS \
    CTL_CODE(FILE_DEVICE_UNKNOWN, ES8316_IOCTL_INDEX_BASE + 6, METHOD_BUFFERED, FILE_READ_ACCESS)

#pragma pack(push, 1)

/* Separate ABI: the original codec status and controls remain unchanged. */
typedef struct _ES8316_PERSISTENT_INFO
{
    ULONG Size;
    ULONG Version;
    ULONG State; /* 0 disabled, 1 idle, 2 starting, 3 running, 4 waiting,
                    5 stopping, 6 exited */
    ULONG Enabled;
    ULONG Wanted;
    ULONG RemovalBlocked;
    ULONG LastStatus;
    ULONG ReconnectCount;
    ULONG BufferBytes;
    ULONG SampleRate;
    ULONGLONG ConsumedBytes;
    ULONGLONG SubmittedBytes;
    ULONGLONG ReturnedBytes;
    ULONG Stage;
    ULONG FailureStage;
    ULONG LowerStatus;
    ULONG PropertyId;
    ULONG PropertyFlags;
    ULONG InputBytes;
    ULONG OutputBytes;
    ULONG ListSize;
    ULONG ListCount;
    ULONG Reserved;
} ES8316_PERSISTENT_INFO, *PES8316_PERSISTENT_INFO;

#define ES8316_PERSISTENT_INFO_VERSION_1_SIZE ((ULONG)FIELD_OFFSET(ES8316_PERSISTENT_INFO, ReturnedBytes))

typedef struct _ES8316_REGISTER_IO
{
    UCHAR Register;
    UCHAR Value;
} ES8316_REGISTER_IO, *PES8316_REGISTER_IO;

typedef struct _ES8316_REGISTER_DUMP
{
    UCHAR Count;
    UCHAR Values[0x54];
} ES8316_REGISTER_DUMP, *PES8316_REGISTER_DUMP;

typedef enum _ES8316_JACK_STATE
{
    Es8316JackUnknown = 0,
    Es8316JackRemoved = 1,
    Es8316JackHeadphone = 2,
    Es8316JackHeadset = 3,
} ES8316_JACK_STATE;

typedef struct _ES8316_STATUS
{
    ULONG Version;
    ULONG JackState;
    ULONG ButtonPressed;
    ULONG GpioFlag;
    ULONG CodecInitialized;
    ULONG InterruptConnected;
    ULONG JackEvents;
    ULONG PollCount;
    ULONG LastI2cStatus;
    ULONG MclkHz;
    ULONG SampleRate;
    ULONG WordLength;
    ULONG OutputEnabled;
    ULONG MicEnabled;
    ULONG MclkPolicy;
    ULONG MclkInterface;
    ULONG MclkVoted;
    ULONG MclkLastStatus;
    ULONG MclkRetries;
    ULONG MclkLastDspError;
} ES8316_STATUS, *PES8316_STATUS;

#define ES8316_STATUS_VERSION 3
#define ES8316_STATUS_VERSION_2_SIZE FIELD_OFFSET(ES8316_STATUS, MclkLastDspError)

typedef struct _ES8316_SET_OUTPUT
{
    ULONG HeadphoneEnable;
    ULONG MicEnable;
    ULONG HpVolume;
    ULONG DacVolume;
} ES8316_SET_OUTPUT, *PES8316_SET_OUTPUT;

#pragma pack(pop)
