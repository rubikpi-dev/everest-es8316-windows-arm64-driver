/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once

typedef struct _ES8316_PERSISTENT_STREAM ES8316_PERSISTENT_STREAM;

typedef enum _ES8316_PERSISTENT_STATE {
    Es8316PersistentDisabled = 0,
    Es8316PersistentIdle,
    Es8316PersistentStarting,
    Es8316PersistentRunning,
    Es8316PersistentWaiting,
    Es8316PersistentStopping,
    Es8316PersistentExited
} ES8316_PERSISTENT_STATE;

typedef enum _ES8316_PERSISTENT_STAGE {
    Es8316StageIdle = 0,
    Es8316StageCodecReady,
    Es8316StageFindInterface,
    Es8316StageTopology,
    Es8316StageEngineDescriptor,
    Es8316StagePinDataflow,
    Es8316StagePinCommunication,
    Es8316StagePinInstances,
    Es8316StagePinModes,
    Es8316StageCreatePin,
    Es8316StageAllocateBuffer,
    Es8316StageAcquire,
    Es8316StagePause,
    Es8316StagePreroll,
    Es8316StageRun,
    Es8316StageReadPosition,
    Es8316StageWritePosition
} ES8316_PERSISTENT_STAGE;

typedef struct _ES8316_PERSISTENT_STATUS {
    ULONG Size;
    ULONG Version;
    ULONG State;
    ULONG Enabled;
    ULONG Wanted;
    ULONG RemovalBlocked;
    NTSTATUS LastStatus;
    ULONG ReconnectCount;
    ULONG BufferBytes;
    ULONG SampleRate;
    ULONGLONG ConsumedBytes;
    ULONGLONG SubmittedBytes;
    /* Version 2 extends the original 56-byte ABI without changing its prefix. */
    ULONGLONG ReturnedBytes;
    ULONG Stage;
    ULONG FailureStage;
    NTSTATUS LowerStatus;
    ULONG PropertyId;
    ULONG PropertyFlags;
    ULONG InputBytes;
    ULONG OutputBytes;
    ULONG ListSize;
    ULONG ListCount;
    ULONG Reserved;
} ES8316_PERSISTENT_STATUS;

/* Initialize/Cleanup are serialized by the device's lifecycle. Resume is
 * asynchronous. Suspend/Cleanup must run at PASSIVE_LEVEL, with no codec or
 * MCLK lock held, before codec power/resources are released. The final join
 * cannot safely abandon an I/O operation whose lower driver ignores cancel. */
NTSTATUS Es8316PersistentInitialize(WDFDEVICE Device, ES8316_PERSISTENT_STREAM **Stream);
VOID Es8316PersistentResume(ES8316_PERSISTENT_STREAM *Stream);
VOID Es8316PersistentSuspend(ES8316_PERSISTENT_STREAM *Stream);
VOID Es8316PersistentCleanup(ES8316_PERSISTENT_STREAM *Stream);
VOID Es8316PersistentGetStatus(ES8316_PERSISTENT_STREAM *Stream, ES8316_PERSISTENT_STATUS *Status);
