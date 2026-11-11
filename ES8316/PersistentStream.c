/* SPDX-License-Identifier: GPL-2.0-only
 * Owned silent KS offload client. This does not intercept another client's
 * stream or suppress STOP, close, suspend, removal or DSP restart.
 *
 * All handle creation, mapped-buffer access and closing occurs on one System
 * thread. Public KS properties are used; no private ADCM ABI is assumed.
 * WaveSpeaker's LP/default offload graph has two calibrated stream instances;
 * consuming one leaves the independent LL/default host graph available.
 */
#include <ntifs.h>
#include "Driver.h"
#include <windef.h>
#define NOBITMAP
#include <mmreg.h>
#undef NOBITMAP
#include <ks.h>
#include <ksmedia.h>
#include <devpkey.h>
#include <wdmguid.h>
#include "PersistentStream.h"
#include "RingFeed.h"

#define PS_TAG 'sP8E'
#define PS_PIN 1UL
#define PS_RATE 48000UL
#define PS_BUFFER_BYTES (PS_RATE * 4UL) /* one second, two DSP submissions */
#define PS_POLL_MS 20UL
#define PS_RETRY_MS 2000UL
#define PS_TIMEOUT_MS 2000UL
#define PS_STALL_100NS (3ULL * 10000000ULL)

struct _ES8316_PERSISTENT_STREAM {
    WDFDEVICE Device;
    KSPIN_LOCK Lock;
    KEVENT Wake;
    KEVENT Idle;
    HANDLE ThreadHandle;
    PETHREAD WorkerThread;
    BOOLEAN Wanted;
    BOOLEAN Exit;
    BOOLEAN Blocked;
    BOOLEAN Removed;
    BOOLEAN RetireTarget;
    BOOLEAN RetiringTarget;
    BOOLEAN ForceClose;
    BOOLEAN InterfaceAbsent;
    PIRP ActiveIrp;
    PVOID InterfaceNotification;
    PVOID TargetNotification;
    HANDLE FilterHandle;
    HANDLE PinHandle;
    PFILE_OBJECT FilterFile;
    PFILE_OBJECT PinFile;
    KSSTATE PinState;
    ULONGLONG SelectedGeneration;
    USHORT SelectedLinkLength;
    WCHAR SelectedLink[1024];
    ULONG EngineNode;
    ES8316_RING_FEED Ring;
    ULONGLONG LastProgress;
    ES8316_PERSISTENT_STATUS Status;
};

static BOOLEAN PsWanted(ES8316_PERSISTENT_STREAM *s)
{
    KIRQL irql;
    BOOLEAN result;
    KeAcquireSpinLock(&s->Lock, &irql);
    result = s->Wanted && !s->Exit && !s->Blocked && !s->ForceClose && !s->InterfaceAbsent;
    KeReleaseSpinLock(&s->Lock, irql);
    return result;
}

static BOOLEAN PsBeginWork(ES8316_PERSISTENT_STREAM *s)
{
    KIRQL irql;
    BOOLEAN allowed;
    /* Serialize the quiescence event with Suspend's Wanted=FALSE. No handle
     * creation or property request may begin using a stale readiness check. */
    KeAcquireSpinLock(&s->Lock, &irql);
    allowed = s->Wanted && !s->Exit && !s->Blocked && !s->ForceClose && !s->InterfaceAbsent;
    if (allowed) KeClearEvent(&s->Idle);
    KeReleaseSpinLock(&s->Lock, irql);
    return allowed;
}

static VOID PsStatus(ES8316_PERSISTENT_STREAM *s, ULONG state, NTSTATUS status)
{
    KIRQL irql;
    BOOLEAN changed;
    KeAcquireSpinLock(&s->Lock, &irql);
    changed = s->Status.State != state || s->Status.LastStatus != status;
    s->Status.State = state;
    s->Status.LastStatus = status;
    if (!NT_SUCCESS(status)) s->Status.FailureStage = s->Status.Stage;
    KeReleaseSpinLock(&s->Lock, irql);
    if (changed) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
            "ES8316Persistent: state=%lu status=0x%08lx\n", state, status);
    }
}

static VOID PsSetStage(ES8316_PERSISTENT_STREAM *s, ULONG stage)
{
    KIRQL irql;
    KeAcquireSpinLock(&s->Lock, &irql);
    s->Status.Stage = stage;
    s->Status.LowerStatus = STATUS_PENDING;
    s->Status.ReturnedBytes = 0;
    s->Status.PropertyId = 0;
    s->Status.PropertyFlags = 0;
    s->Status.InputBytes = 0;
    s->Status.OutputBytes = 0;
    s->Status.ListSize = 0;
    s->Status.ListCount = 0;
    KeReleaseSpinLock(&s->Lock, irql);
}

static VOID PsRecordReply(ES8316_PERSISTENT_STREAM *s, const VOID *property,
    ULONG inputBytes, ULONG outputBytes, NTSTATUS status, ULONG_PTR returned)
{
    KIRQL irql;
    const KSPROPERTY *request = property;
    KeAcquireSpinLock(&s->Lock, &irql);
    s->Status.LowerStatus = status;
    s->Status.InputBytes = inputBytes;
    s->Status.OutputBytes = outputBytes;
    s->Status.ReturnedBytes = returned;
    if (request != NULL && inputBytes >= sizeof(*request)) {
        s->Status.PropertyId = request->Id;
        s->Status.PropertyFlags = request->Flags;
    }
    KeReleaseSpinLock(&s->Lock, irql);
}

static VOID PsRecordList(ES8316_PERSISTENT_STREAM *s, const KSMULTIPLE_ITEM *list)
{
    KIRQL irql;
    KeAcquireSpinLock(&s->Lock, &irql);
    s->Status.ListSize = list->Size;
    s->Status.ListCount = list->Count;
    KeReleaseSpinLock(&s->Lock, irql);
}

/* The first completion retains the IRP, so cancellation cannot race automatic
 * free. No callback takes Lock. The caller clears ActiveIrp under Lock before
 * resuming normal I/O-manager completion and waiting for its separate event.
 */
static NTSTATUS PsIoComplete(PDEVICE_OBJECT device, PIRP irp, PVOID context)
{
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(irp);
    KeSetEvent((PKEVENT)context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static VOID PsCancel(ES8316_PERSISTENT_STREAM *s)
{
    KIRQL irql;
    KeAcquireSpinLock(&s->Lock, &irql);
    if (s->ActiveIrp != NULL) IoCancelIrp(s->ActiveIrp);
    KeReleaseSpinLock(&s->Lock, irql);
}

static NTSTATUS PsProperty(ES8316_PERSISTENT_STREAM *s, PFILE_OBJECT file,
    PVOID property, ULONG propertyBytes, PVOID value, ULONG valueBytes,
    BOOLEAN closing, PULONG_PTR transferred)
{
    PDEVICE_OBJECT target = IoGetRelatedDeviceObject(file);
    PIRP irp;
    PIO_STACK_LOCATION stack;
    KEVENT done;
    KEVENT finalized;
    IO_STATUS_BLOCK iosb;
    LARGE_INTEGER timeout;
    KIRQL irql;
    NTSTATUS status;
    BOOLEAN timeoutOccurred = FALSE;
    if (transferred != NULL) *transferred = 0;
    KeInitializeEvent(&done, NotificationEvent, FALSE);
    KeInitializeEvent(&finalized, NotificationEvent, FALSE);
    iosb.Status = STATUS_NOT_SUPPORTED;
    iosb.Information = 0;
    /* KS can convert this METHOD_NEITHER request to buffered I/O internally,
     * including for KernelMode callers. A thread-associated built IRP lets
     * normal completion copy SystemBuffer to value and release that buffer.
     * IoFreeIrp would skip both operations and leave a successful GET empty. */
    irp = IoBuildDeviceIoControlRequest(IOCTL_KS_PROPERTY, target,
        property, propertyBytes, value, valueBytes, FALSE, &finalized, &iosb);
    if (irp == NULL) return STATUS_INSUFFICIENT_RESOURCES;
    irp->RequestorMode = KernelMode;
    irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    irp->IoStatus.Information = 0;
    stack = IoGetNextIrpStackLocation(irp);
    /* The owner thread holds this file reference through final completion.
     * Leave OriginalFileObject as initialized by the builder: assigning a
     * borrowed reference there would allow an unbalanced completion release. */
    stack->FileObject = file;
    KeAcquireSpinLock(&s->Lock, &irql);
    if (!closing && (!s->Wanted || s->Exit || s->Blocked || s->ForceClose || s->InterfaceAbsent)) {
        KeReleaseSpinLock(&s->Lock, irql);
        /* It was never dispatched and has no catch-completion routine. It is
         * still a thread-associated built IRP and requires normal completion. */
        irp->IoStatus.Status = STATUS_CANCELLED;
        irp->IoStatus.Information = 0;
        IoCompleteRequest(irp, IO_NO_INCREMENT);
        KeWaitForSingleObject(&finalized, Executive, KernelMode, FALSE, NULL);
        if (!closing) PsRecordReply(s, property, propertyBytes, valueBytes, iosb.Status, iosb.Information);
        return iosb.Status;
    }
    IoSetCompletionRoutine(irp, PsIoComplete, &done, TRUE, TRUE, TRUE);
    s->ActiveIrp = irp;
    /* Cancel may precede IoCallDriver. Lower receives the already-set Cancel
     * flag; timeout cancellation is repeated after dispatch if necessary. */
    KeReleaseSpinLock(&s->Lock, irql);
    (VOID)IoCallDriver(target, irp);
    timeout.QuadPart = -((LONGLONG)PS_TIMEOUT_MS * 10000);
    status = KeWaitForSingleObject(&done, Executive, KernelMode, FALSE, &timeout);
    if (status == STATUS_TIMEOUT) {
        timeoutOccurred = TRUE;
        PsCancel(s);
        /* Retain the IRP until the lower stack finishes, even after cancel.
         * A broken lower cancellation implementation can extend this wait. */
        KeWaitForSingleObject(&done, Executive, KernelMode, FALSE, NULL);
    }
    KeAcquireSpinLock(&s->Lock, &irql);
    s->ActiveIrp = NULL;
    KeReleaseSpinLock(&s->Lock, irql);
    /* The caught completion has not yet copied back KS's buffered result.
     * After resuming completion the IRP can be freed immediately; never touch
     * it again. Keep both events, IOSB and caller buffers alive until finalized. */
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    KeWaitForSingleObject(&finalized, Executive, KernelMode, FALSE, NULL);
    if (!closing) PsRecordReply(s, property, propertyBytes, valueBytes, iosb.Status, iosb.Information);
    status = timeoutOccurred ? STATUS_IO_TIMEOUT : iosb.Status;
    if (transferred != NULL) *transferred = iosb.Information;
    return status;
}

static NTSTATUS PsGetPin(ES8316_PERSISTENT_STREAM *s, const GUID *set, ULONG id,
    PVOID value, ULONG bytes, PULONG_PTR returned)
{
    KSP_PIN property = { 0 };
    property.Property.Set = *set;
    property.Property.Id = id;
    property.Property.Flags = KSPROPERTY_TYPE_GET;
    property.PinId = PS_PIN;
    return PsProperty(s, s->FilterFile, &property, sizeof(property), value, bytes, FALSE, returned);
}

static NTSTATUS PsNode(ES8316_PERSISTENT_STREAM *s, PFILE_OBJECT file,
    const GUID *set, ULONG id, ULONG verb, PVOID value, ULONG bytes)
{
    KSP_NODE property = { 0 };
    ULONG_PTR returned = 0;
    NTSTATUS status;
    property.Property.Set = *set;
    property.Property.Id = id;
    property.Property.Flags = verb | KSPROPERTY_TYPE_TOPOLOGY;
    property.NodeId = s->EngineNode;
    status = PsProperty(s, file, &property, sizeof(property), value, bytes, FALSE, &returned);
    if (NT_SUCCESS(status) && verb == KSPROPERTY_TYPE_GET && returned < bytes)
        return STATUS_INFO_LENGTH_MISMATCH;
    return status;
}

static NTSTATUS PsSetState(ES8316_PERSISTENT_STREAM *s, KSSTATE state, BOOLEAN closing)
{
    KSPROPERTY property;
    NTSTATUS status;
    property.Set = KSPROPSETID_Connection;
    property.Id = KSPROPERTY_CONNECTION_STATE;
    property.Flags = KSPROPERTY_TYPE_SET;
    status = PsProperty(s, s->PinFile, &property, sizeof(property), &state, sizeof(state), closing, NULL);
    if (NT_SUCCESS(status)) s->PinState = state;
    /* Cancellation after timeout can race a successful lower state change.
     * Conservatively unwind from the higher attempted state in that case. */
    else if (status == STATUS_IO_TIMEOUT && state > s->PinState) s->PinState = state;
    return status;
}

static NTSTATUS PsCodecStatus(ES8316_PERSISTENT_STREAM *s)
{
    PDEVICE_CONTEXT ctx = DeviceGetContext(s->Device);
    NTSTATUS status;
    ULONG enabled;
    KIRQL irql;
    /* MCLK reconciliation uses Mclk.Lock -> CodecLock. Take the same order
     * for one coherent readiness snapshot, then release both before KS I/O. */
    if (ctx->Mclk.Lock != NULL) WdfWaitLockAcquire(ctx->Mclk.Lock, NULL);
    WdfWaitLockAcquire(ctx->CodecLock, NULL);
    enabled = ctx->Config.PersistentStreamEnabled;
    if (!enabled) status = STATUS_NOT_SUPPORTED;
    else if (ctx->Config.SampleRate != PS_RATE || ctx->Config.WordLength != 16 ||
        ctx->Config.I2sMaster != 1 || ctx->Config.MclkPolicy != Es8316MclkAlwaysInD0)
        status = STATUS_DEVICE_CONFIGURATION_ERROR;
    else if (ctx->Mclk.Lock == NULL || !ctx->InD0 || !ctx->CodecInitialized ||
        !ctx->Mclk.ClockEnabled || ctx->Mclk.TargetGone) status = STATUS_DEVICE_NOT_READY;
    else status = STATUS_SUCCESS;
    WdfWaitLockRelease(ctx->CodecLock);
    if (ctx->Mclk.Lock != NULL) WdfWaitLockRelease(ctx->Mclk.Lock);
    KeAcquireSpinLock(&s->Lock, &irql);
    s->Status.Enabled = enabled;
    KeReleaseSpinLock(&s->Lock, irql);
    return status;
}

static NTSTATUS PsTargetChange(PVOID notification, PVOID context)
{
    PTARGET_DEVICE_REMOVAL_NOTIFICATION n = notification;
    GUID event = n->Event; /* notification memory need only be valid at PASSIVE */
    ES8316_PERSISTENT_STREAM *s = context;
    KIRQL irql;
    LARGE_INTEGER timeout;
    NTSTATUS status;
    if (IsEqualGUID(&event, &GUID_TARGET_DEVICE_REMOVE_CANCELLED)) {
        KeAcquireSpinLock(&s->Lock, &irql);
        s->Blocked = FALSE;
        s->RetireTarget = TRUE;
        s->ForceClose = TRUE;
        KeReleaseSpinLock(&s->Lock, irql);
        KeSetEvent(&s->Wake, IO_NO_INCREMENT, FALSE);
        return STATUS_SUCCESS;
    }
    if (!IsEqualGUID(&event, &GUID_TARGET_DEVICE_QUERY_REMOVE) &&
        !IsEqualGUID(&event, &GUID_TARGET_DEVICE_REMOVE_COMPLETE)) return STATUS_SUCCESS;
    if (IsEqualGUID(&event, &GUID_TARGET_DEVICE_QUERY_REMOVE) &&
        PsGetCurrentThread() == s->WorkerThread) return STATUS_UNSUCCESSFUL;
    KeAcquireSpinLock(&s->Lock, &irql);
    if (IsEqualGUID(&event, &GUID_TARGET_DEVICE_QUERY_REMOVE) &&
        (s->RetiringTarget || s->Exit)) {
        /* A successful query requires keeping the registration for its
         * CANCELLED/COMPLETE event. Retirement has already claimed it. */
        KeReleaseSpinLock(&s->Lock, irql);
        return STATUS_UNSUCCESSFUL;
    }
    s->Blocked = TRUE;
    s->ForceClose = TRUE;
    if (IsEqualGUID(&event, &GUID_TARGET_DEVICE_REMOVE_COMPLETE)) {
        s->Removed = TRUE;
        s->RetireTarget = TRUE;
    }
    KeReleaseSpinLock(&s->Lock, irql);
    PsCancel(s);
    KeSetEvent(&s->Wake, IO_NO_INCREMENT, FALSE);
    if (IsEqualGUID(&event, &GUID_TARGET_DEVICE_REMOVE_COMPLETE)) return STATUS_SUCCESS;
    /* A query remove succeeds only once handles AND referenced file objects
     * have been released. Never wait for this same thread from a callback. */
    timeout.QuadPart = -((LONGLONG)PS_TIMEOUT_MS * 10000);
    status = KeWaitForSingleObject(&s->Idle, Executive, KernelMode, FALSE, &timeout);
    if (status != STATUS_SUCCESS) {
        /* Do not depend on REMOVE_CANCELLED being delivered to a vetoing
         * callback. ForceClose still prevents using the old pin after veto. */
        KeAcquireSpinLock(&s->Lock, &irql);
        if (!s->Removed) s->Blocked = FALSE;
        s->RetireTarget = TRUE;
        KeReleaseSpinLock(&s->Lock, irql);
        KeSetEvent(&s->Wake, IO_NO_INCREMENT, FALSE);
    }
    return status == STATUS_SUCCESS ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

static NTSTATUS PsInterfaceChange(PVOID notification, PVOID context)
{
    PDEVICE_INTERFACE_CHANGE_NOTIFICATION n = notification;
    GUID event = n->Event;
    ES8316_PERSISTENT_STREAM *s = context;
    KIRQL irql;
    if (IsEqualGUID(&event, &GUID_DEVICE_INTERFACE_ARRIVAL) ||
        IsEqualGUID(&event, &GUID_DEVICE_INTERFACE_REMOVAL)) {
        WCHAR selected[1024];
        UNICODE_STRING previous;
        BOOLEAN matching;
        ULONGLONG generation;
        KeAcquireSpinLock(&s->Lock, &irql);
        generation = s->SelectedGeneration;
        previous.Length = s->SelectedLinkLength;
        previous.MaximumLength = sizeof(selected);
        previous.Buffer = selected;
        RtlCopyMemory(selected, s->SelectedLink, s->SelectedLinkLength);
        KeReleaseSpinLock(&s->Lock, irql);
        matching = previous.Length != 0 && RtlEqualUnicodeString(&previous, n->SymbolicLinkName, TRUE);
        KeAcquireSpinLock(&s->Lock, &irql);
        matching = matching && s->SelectedGeneration == generation &&
            s->SelectedLinkLength == previous.Length &&
            RtlCompareMemory(s->SelectedLink, selected, previous.Length) == previous.Length;
        /* Arrivals only wake the owner. They never clear removal state or
         * authorize a target. The owner also retries discovery periodically,
         * so arrival/removal callback ordering cannot strand a replacement. */
        if (matching && IsEqualGUID(&event, &GUID_DEVICE_INTERFACE_REMOVAL)) {
            s->InterfaceAbsent = TRUE;
            s->ForceClose = TRUE;
        }
        KeReleaseSpinLock(&s->Lock, irql);
        if (matching && IsEqualGUID(&event, &GUID_DEVICE_INTERFACE_REMOVAL)) PsCancel(s);
        KeSetEvent(&s->Wake, IO_NO_INCREMENT, FALSE);
    }
    return STATUS_SUCCESS;
}

static VOID PsClose(ES8316_PERSISTENT_STREAM *s)
{
    if (s->PinFile != NULL) {
        KSSTATE prior = s->PinState;
        if (prior >= KSSTATE_RUN) (VOID)PsSetState(s, KSSTATE_PAUSE, TRUE);
        if (prior >= KSSTATE_PAUSE) (VOID)PsSetState(s, KSSTATE_ACQUIRE, TRUE);
        if (prior >= KSSTATE_ACQUIRE) (VOID)PsSetState(s, KSSTATE_STOP, TRUE);
        ObDereferenceObject(s->PinFile);
        s->PinFile = NULL;
    }
    if (s->PinHandle != NULL) {
        ZwClose(s->PinHandle); /* also frees the WaveRT mapping on owner thread */
        s->PinHandle = NULL;
    }
    s->PinState = KSSTATE_STOP;
    if (s->FilterFile != NULL) {
        ObDereferenceObject(s->FilterFile);
        s->FilterFile = NULL;
    }
    if (s->FilterHandle != NULL) {
        ZwClose(s->FilterHandle);
        s->FilterHandle = NULL;
    }
    /* Target callback can now return. Unregister only after signaling Idle;
     * IoUnregisterPlugPlayNotificationEx waits for in-flight callbacks. */
    KeSetEvent(&s->Idle, IO_NO_INCREMENT, FALSE);
}

static VOID PsRetireNotification(ES8316_PERSISTENT_STREAM *s, BOOLEAN force)
{
    KIRQL irql;
    BOOLEAN retire;
    PVOID notification = NULL;
    KeAcquireSpinLock(&s->Lock, &irql);
    /* Query-remove and retirement arbitrate under one lock. A prior retire
     * request cannot override an outstanding successful query. */
    retire = force || !s->Blocked || s->Removed;
    if (retire && s->TargetNotification != NULL) {
        s->RetiringTarget = TRUE;
        notification = s->TargetNotification;
    }
    s->RetireTarget = FALSE;
    KeReleaseSpinLock(&s->Lock, irql);
    if (notification != NULL) {
        IoUnregisterPlugPlayNotificationEx(notification);
        KeAcquireSpinLock(&s->Lock, &irql);
        s->TargetNotification = NULL;
        s->RetiringTarget = FALSE;
        KeReleaseSpinLock(&s->Lock, irql);
    }
}

/* Called only after pin/filter handles and file references have been closed. */
static VOID PsAcknowledgeClosed(ES8316_PERSISTENT_STREAM *s)
{
    KIRQL irql;
    KeAcquireSpinLock(&s->Lock, &irql);
    s->ForceClose = FALSE;
    KeReleaseSpinLock(&s->Lock, irql);
}

/* Owner only, after PsClose/PsRetireNotification. Do not require an arrival:
 * it can precede the old target's terminal callback or be coalesced. This only
 * permits PsFindFilter to enumerate and perform its WaveSpeaker/hardware-ID
 * checks; it does not adopt any interface named by a notification. */
static VOID PsPrepareRediscovery(ES8316_PERSISTENT_STREAM *s)
{
    KIRQL irql;
    KeAcquireSpinLock(&s->Lock, &irql);
    if ((s->Removed || s->InterfaceAbsent) &&
        (!s->Blocked || s->Removed) && s->TargetNotification == NULL &&
        s->PinHandle == NULL && s->PinFile == NULL &&
        s->FilterHandle == NULL && s->FilterFile == NULL) {
        /* Invalidate callback snapshots of the retired selection. Callbacks
         * racing this transition must not mutate a subsequently selected pin. */
        ++s->SelectedGeneration;
        s->SelectedLinkLength = 0;
        s->Removed = FALSE;
        s->Blocked = FALSE;
        s->InterfaceAbsent = FALSE;
        s->ForceClose = FALSE;
    }
    KeReleaseSpinLock(&s->Lock, irql);
}

static BOOLEAN PsHardwareMatches(PFILE_OBJECT file)
{
    PDEVICE_OBJECT pdo = IoGetDeviceAttachmentBaseRef(IoGetRelatedDeviceObject(file));
    WCHAR ids[512];
    ULONG bytes = 0, offset = 0, chars;
    BOOLEAN matches = FALSE;
    NTSTATUS status;
    if (pdo == NULL) return FALSE;
    RtlZeroMemory(ids, sizeof(ids));
    status = IoGetDeviceProperty(pdo, DevicePropertyHardwareID, sizeof(ids), ids, &bytes);
    ObDereferenceObject(pdo);
    if (!NT_SUCCESS(status) || bytes < 2 * sizeof(WCHAR) || bytes > sizeof(ids) || bytes % sizeof(WCHAR)) return FALSE;
    chars = bytes / sizeof(WCHAR);
    while (offset < chars && ids[offset] != L'\0') {
        ULONG end = offset;
        UNICODE_STRING current, expected;
        while (end < chars && ids[end] != L'\0') ++end;
        if (end == chars) return FALSE;
        current.Buffer = &ids[offset];
        current.Length = (USHORT)((end - offset) * sizeof(WCHAR));
        current.MaximumLength = current.Length;
        RtlInitUnicodeString(&expected, L"AUCD\\VEN_QCOM&DEV_0E29&SUBSYS_IOT06490");
        matches = RtlEqualUnicodeString(&current, &expected, TRUE);
        RtlInitUnicodeString(&expected, L"AUCD\\VEN_QCOM&DEV_0E29&SUBSYS_RUBI6490");
        matches = matches || RtlEqualUnicodeString(&current, &expected, TRUE);
        if (matches) return TRUE;
        offset = end + 1;
    }
    return FALSE;
}

static NTSTATUS PsFindFilter(ES8316_PERSISTENT_STREAM *s)
{
    PWSTR names = NULL, name;
    NTSTATUS status;
    status = IoGetDeviceInterfaces(&KSCATEGORY_RENDER, NULL, 0, &names);
    if (!NT_SUCCESS(status)) return status;
    status = STATUS_NOT_FOUND;
    for (name = names; *name != L'\0' && PsWanted(s); name += wcslen(name) + 1) {
        UNICODE_STRING link, reference, expected;
        WCHAR ref[64];
        DEVPROPTYPE type;
        ULONG required = 0;
        OBJECT_ATTRIBUTES attributes;
        IO_STATUS_BLOCK iosb;
        NTSTATUS candidate;
        RtlInitUnicodeString(&link, name);
        RtlZeroMemory(ref, sizeof(ref));
        candidate = IoGetDeviceInterfacePropertyData(&link, &DEVPKEY_DeviceInterface_ReferenceString,
            LOCALE_NEUTRAL, 0, sizeof(ref), ref, &required, &type);
        if (!NT_SUCCESS(candidate) || type != DEVPROP_TYPE_STRING ||
            required < sizeof(WCHAR) || required > sizeof(ref) || required % sizeof(WCHAR) ||
            ref[required / sizeof(WCHAR) - 1] != L'\0') continue;
        RtlInitUnicodeString(&reference, ref);
        RtlInitUnicodeString(&expected, L"WaveSpeaker");
        if (!RtlEqualUnicodeString(&reference, &expected, TRUE)) continue;
        InitializeObjectAttributes(&attributes, &link, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
        candidate = ZwCreateFile(&s->FilterHandle, GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE,
            &attributes, &iosb, NULL, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
            FILE_OPEN, 0, NULL, 0);
        if (!NT_SUCCESS(candidate)) { status = candidate; continue; }
        candidate = ObReferenceObjectByHandle(s->FilterHandle, 0, *IoFileObjectType,
            KernelMode, (PVOID *)&s->FilterFile, NULL);
        if (!NT_SUCCESS(candidate) || !PsHardwareMatches(s->FilterFile)) {
            if (s->FilterFile != NULL) { ObDereferenceObject(s->FilterFile); s->FilterFile = NULL; }
            ZwClose(s->FilterHandle); s->FilterHandle = NULL;
            status = NT_SUCCESS(candidate) ? STATUS_OBJECT_TYPE_MISMATCH : candidate;
            continue;
        }
        if (link.Length >= sizeof(s->SelectedLink)) {
            ObDereferenceObject(s->FilterFile); s->FilterFile = NULL;
            ZwClose(s->FilterHandle); s->FilterHandle = NULL;
            status = STATUS_NAME_TOO_LONG;
            continue;
        }
        {
            KIRQL irql;
            WCHAR selected[1024];
            RtlCopyMemory(selected, link.Buffer, link.Length);
            KeAcquireSpinLock(&s->Lock, &irql);
            RtlCopyMemory(s->SelectedLink, selected, link.Length);
            s->SelectedLinkLength = link.Length;
            ++s->SelectedGeneration;
            KeReleaseSpinLock(&s->Lock, irql);
        }
        candidate = IoRegisterPlugPlayNotification(EventCategoryTargetDeviceChange, 0,
            s->FilterFile, WdfDriverWdmGetDriverObject(WdfDeviceGetDriver(s->Device)),
            PsTargetChange, s, &s->TargetNotification);
        status = candidate;
        if (NT_SUCCESS(candidate)) break;
        ObDereferenceObject(s->FilterFile); s->FilterFile = NULL;
        ZwClose(s->FilterHandle); s->FilterHandle = NULL;
    }
    ExFreePool(names);
    return status;
}

static NTSTATUS PsValidateFilter(ES8316_PERSISTENT_STREAM *s)
{
    struct { KSMULTIPLE_ITEM List; GUID Ids[32]; } list;
    KSPROPERTY property;
    ULONG_PTR returned = 0;
    ULONG i;
    KSPIN_DATAFLOW flow;
    KSPIN_COMMUNICATION communication;
    KSPIN_CINSTANCES instances;
    KSAUDIOENGINE_DESCRIPTOR engine;
    BOOLEAN found = FALSE;
    NTSTATUS status;
    PsSetStage(s, Es8316StageTopology);
    property.Set = KSPROPSETID_Topology;
    property.Id = KSPROPERTY_TOPOLOGY_NODES;
    property.Flags = KSPROPERTY_TYPE_GET;
    RtlZeroMemory(&list, sizeof(list));
    status = PsProperty(s, s->FilterFile, &property, sizeof(property), &list, sizeof(list), FALSE, &returned);
    PsRecordList(s, &list.List);
    if (!NT_SUCCESS(status)) return status;
    if (returned < sizeof(KSMULTIPLE_ITEM) || list.List.Count > ARRAYSIZE(list.Ids) ||
        list.List.Size > returned || list.List.Size < sizeof(KSMULTIPLE_ITEM) + list.List.Count * sizeof(GUID))
        return STATUS_INFO_LENGTH_MISMATCH;
    for (i = 0; i < list.List.Count; ++i) {
        if (IsEqualGUID(&list.Ids[i], &KSNODETYPE_AUDIO_ENGINE)) {
            s->EngineNode = i;
            PsSetStage(s, Es8316StageEngineDescriptor);
            status = PsNode(s, s->FilterFile, &KSPROPSETID_AudioEngine,
                KSPROPERTY_AUDIOENGINE_DESCRIPTOR, KSPROPERTY_TYPE_GET, &engine, sizeof(engine));
            if (NT_SUCCESS(status) && engine.nHostPinId == 0 && engine.nOffloadPinId == PS_PIN && engine.nLoopbackPinId == 2) {
                found = TRUE; break;
            }
        }
    }
    if (!found) return STATUS_NOT_SUPPORTED;
    PsSetStage(s, Es8316StagePinDataflow);
    status = PsGetPin(s, &KSPROPSETID_Pin, KSPROPERTY_PIN_DATAFLOW, &flow, sizeof(flow), &returned);
    if (!NT_SUCCESS(status)) return status;
    if (returned < sizeof(flow) || flow != KSPIN_DATAFLOW_IN) return STATUS_NOT_SUPPORTED;
    PsSetStage(s, Es8316StagePinCommunication);
    status = PsGetPin(s, &KSPROPSETID_Pin, KSPROPERTY_PIN_COMMUNICATION, &communication, sizeof(communication), &returned);
    if (!NT_SUCCESS(status)) return status;
    if (returned < sizeof(communication) || communication != KSPIN_COMMUNICATION_SINK) return STATUS_NOT_SUPPORTED;
    PsSetStage(s, Es8316StagePinInstances);
    status = PsGetPin(s, &KSPROPSETID_Pin, KSPROPERTY_PIN_CINSTANCES, &instances, sizeof(instances), &returned);
    if (!NT_SUCCESS(status)) return status;
    if (returned < sizeof(instances) || instances.PossibleCount <= instances.CurrentCount) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(&list, sizeof(list));
    PsSetStage(s, Es8316StagePinModes);
    status = PsGetPin(s, &KSPROPSETID_AudioSignalProcessing, KSPROPERTY_AUDIOSIGNALPROCESSING_MODES, &list, sizeof(list), &returned);
    PsRecordList(s, &list.List);
    if (!NT_SUCCESS(status)) return status;
    if (returned < sizeof(KSMULTIPLE_ITEM) || list.List.Count > ARRAYSIZE(list.Ids) ||
        list.List.Size > returned || list.List.Size < sizeof(KSMULTIPLE_ITEM) + list.List.Count * sizeof(GUID))
        return STATUS_INFO_LENGTH_MISMATCH;
    for (i = 0; i < list.List.Count; ++i)
        if (IsEqualGUID(&list.Ids[i], &AUDIO_SIGNALPROCESSINGMODE_DEFAULT)) return STATUS_SUCCESS;
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS PsOpenStream(ES8316_PERSISTENT_STREAM *s)
{
    const ULONG formatBytes = sizeof(KSDATAFORMAT_WAVEFORMATEXTENSIBLE);
    const ULONG alignedFormatBytes = (sizeof(KSDATAFORMAT_WAVEFORMATEXTENSIBLE) + 7UL) & ~7UL;
    ULONG connectBytes = sizeof(KSPIN_CONNECT) + alignedFormatBytes + sizeof(KSMULTIPLE_ITEM) + sizeof(KSATTRIBUTE_AUDIOSIGNALPROCESSING_MODE);
    PKSPIN_CONNECT connect;
    PKSDATAFORMAT_WAVEFORMATEXTENSIBLE format;
    PKSMULTIPLE_ITEM attributes;
    KSATTRIBUTE_AUDIOSIGNALPROCESSING_MODE *mode;
    KSRTAUDIO_BUFFER_PROPERTY_WITH_NOTIFICATION request = { 0 };
    KSRTAUDIO_BUFFER buffer = { 0 };
    ULONG_PTR returned = 0;
    NTSTATUS status;
    ULONG initialPosition;
    KIRQL irql;
    PsSetStage(s, Es8316StageFindInterface);
    status = PsFindFilter(s);
    PsRecordReply(s, NULL, 0, 0, status, 0);
    if (!NT_SUCCESS(status)) return status;
    status = PsValidateFilter(s);
    if (!NT_SUCCESS(status)) return status;
    if (!PsWanted(s)) return STATUS_CANCELLED;
    connect = ExAllocatePool2(POOL_FLAG_NON_PAGED, connectBytes, PS_TAG);
    if (connect == NULL) return STATUS_INSUFFICIENT_RESOURCES;
    connect->Interface.Set = KSINTERFACESETID_Standard;
    connect->Interface.Id = KSINTERFACE_STANDARD_LOOPED_STREAMING;
    connect->Medium.Set = KSMEDIUMSETID_Standard;
    connect->Medium.Id = KSMEDIUM_TYPE_ANYINSTANCE;
    connect->PinId = PS_PIN;
    connect->Priority.PriorityClass = KSPRIORITY_NORMAL;
    /* Both priority fields must be nonzero; zero subclass is rejected by KS. */
    connect->Priority.PrioritySubClass = 1;
    format = (PKSDATAFORMAT_WAVEFORMATEXTENSIBLE)(connect + 1);
    format->DataFormat.FormatSize = formatBytes;
    format->DataFormat.Flags = KSDATAFORMAT_ATTRIBUTES;
    format->DataFormat.SampleSize = 4;
    format->DataFormat.MajorFormat = KSDATAFORMAT_TYPE_AUDIO;
    format->DataFormat.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    format->DataFormat.Specifier = KSDATAFORMAT_SPECIFIER_WAVEFORMATEX;
    format->WaveFormatExt.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    format->WaveFormatExt.Format.nChannels = 2;
    format->WaveFormatExt.Format.nSamplesPerSec = PS_RATE;
    format->WaveFormatExt.Format.nAvgBytesPerSec = PS_RATE * 4;
    format->WaveFormatExt.Format.nBlockAlign = 4;
    format->WaveFormatExt.Format.wBitsPerSample = 16;
    format->WaveFormatExt.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    format->WaveFormatExt.Samples.wValidBitsPerSample = 16;
    format->WaveFormatExt.dwChannelMask = KSAUDIO_SPEAKER_STEREO;
    format->WaveFormatExt.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    attributes = (PKSMULTIPLE_ITEM)((PUCHAR)format + alignedFormatBytes);
    attributes->Size = sizeof(*attributes) + sizeof(*mode);
    attributes->Count = 1;
    mode = (KSATTRIBUTE_AUDIOSIGNALPROCESSING_MODE *)(attributes + 1);
    mode->AttributeHeader.Size = sizeof(*mode);
    mode->AttributeHeader.Attribute = KSATTRIBUTEID_AUDIOSIGNALPROCESSING_MODE;
    mode->SignalProcessingMode = AUDIO_SIGNALPROCESSINGMODE_DEFAULT;
    PsSetStage(s, Es8316StageCreatePin);
    status = KsCreatePin(s->FilterHandle, connect, GENERIC_WRITE, &s->PinHandle);
    PsRecordReply(s, NULL, connectBytes, 0, status, 0);
    ExFreePoolWithTag(connect, PS_TAG);
    if (!NT_SUCCESS(status)) return status;
    status = ObReferenceObjectByHandle(s->PinHandle, 0, *IoFileObjectType,
        KernelMode, (PVOID *)&s->PinFile, NULL);
    if (!NT_SUCCESS(status)) return status;
    if (!PsWanted(s)) return STATUS_CANCELLED;
    PsSetStage(s, Es8316StageAllocateBuffer);
    request.Property.Set = KSPROPSETID_RtAudio;
    request.Property.Id = KSPROPERTY_RTAUDIO_BUFFER_WITH_NOTIFICATION;
    request.Property.Flags = KSPROPERTY_TYPE_GET;
    request.RequestedBufferSize = PS_BUFFER_BYTES;
    request.NotificationCount = 2;
    status = PsProperty(s, s->PinFile, &request, sizeof(request), &buffer, sizeof(buffer), FALSE, &returned);
    if (!NT_SUCCESS(status)) return status;
    if (returned < sizeof(buffer) || buffer.BufferAddress == NULL ||
        buffer.ActualBufferSize < PS_BUFFER_BYTES || buffer.ActualBufferSize > 4 * PS_BUFFER_BYTES ||
        buffer.ActualBufferSize % 64 != 0) return STATUS_INVALID_BUFFER_SIZE;
    /* Mapping belongs to the current System process. Never hand it to a WDF
     * work item or a callback in a different process. Pin close owns unmap. */
    __try { RtlZeroMemory(buffer.BufferAddress, buffer.ActualBufferSize); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return GetExceptionCode(); }
    KeMemoryBarrier();
    /* Qualcomm ACQUIRE opens the graph with the already-allocated DSP buffer
     * handle. Allocate while STOP; ACQUIRE before allocation is rejected. */
    PsSetStage(s, Es8316StageAcquire);
    status = PsSetState(s, KSSTATE_ACQUIRE, FALSE);
    if (!NT_SUCCESS(status)) return status;
    PsSetStage(s, Es8316StagePause);
    status = PsSetState(s, KSSTATE_PAUSE, FALSE);
    if (!NT_SUCCESS(status)) return status;
    initialPosition = buffer.ActualBufferSize;
    PsSetStage(s, Es8316StagePreroll);
    status = PsNode(s, s->PinFile, &KSPROPSETID_Audio, KSPROPERTY_AUDIO_WAVERT_CURRENT_WRITE_POSITION,
        KSPROPERTY_TYPE_SET, &initialPosition, sizeof(initialPosition));
    if (!NT_SUCCESS(status)) return status;
    if (!Es8316RingFeedInitialize(&s->Ring, buffer.ActualBufferSize)) return STATUS_INVALID_BUFFER_SIZE;
    PsSetStage(s, Es8316StageRun);
    status = PsSetState(s, KSSTATE_RUN, FALSE);
    if (!NT_SUCCESS(status)) return status;
    s->LastProgress = KeQueryInterruptTime();
    KeAcquireSpinLock(&s->Lock, &irql);
    s->Status.BufferBytes = buffer.ActualBufferSize;
    s->Status.ConsumedBytes = 0;
    s->Status.SubmittedBytes = s->Ring.Submitted;
    ++s->Status.ReconnectCount;
    KeReleaseSpinLock(&s->Lock, irql);
    return STATUS_SUCCESS;
}

static NTSTATUS PsFeed(ES8316_PERSISTENT_STREAM *s)
{
    ULONGLONG consumed = 0, submitted = 0;
    ULONG position = 0;
    int plan;
    KIRQL irql;
    NTSTATUS status;
    PsSetStage(s, Es8316StageReadPosition);
    status = PsNode(s, s->PinFile, &KSPROPSETID_Audio,
        KSPROPERTY_AUDIO_LINEAR_BUFFER_POSITION, KSPROPERTY_TYPE_GET, &consumed, sizeof(consumed));
    if (!NT_SUCCESS(status)) return status;
    plan = Es8316RingFeedPlan(&s->Ring, consumed, &position, &submitted);
    if (plan < 0) return STATUS_DATA_ERROR;
    if (plan == 0) return KeQueryInterruptTime() - s->LastProgress > PS_STALL_100NS ? STATUS_IO_TIMEOUT : STATUS_SUCCESS;
    KeMemoryBarrier();
    PsSetStage(s, Es8316StageWritePosition);
    status = PsNode(s, s->PinFile, &KSPROPSETID_Audio, KSPROPERTY_AUDIO_WAVERT_CURRENT_WRITE_POSITION,
        KSPROPERTY_TYPE_SET, &position, sizeof(position));
    if (!NT_SUCCESS(status)) return status;
    if (!Es8316RingFeedCommit(&s->Ring, consumed, submitted)) return STATUS_DATA_ERROR;
    s->LastProgress = KeQueryInterruptTime();
    KeAcquireSpinLock(&s->Lock, &irql);
    s->Status.ConsumedBytes = s->Ring.Consumed;
    s->Status.SubmittedBytes = s->Ring.Submitted;
    KeReleaseSpinLock(&s->Lock, irql);
    return STATUS_SUCCESS;
}

static VOID PsWorker(PVOID context)
{
    ES8316_PERSISTENT_STREAM *s = context;
    NTSTATUS status = STATUS_SUCCESS;
    ULONG waitMs;
    BOOLEAN exit;
    NTSTATUS codecStatus;
    KIRQL irql;
    LARGE_INTEGER timeout;
    s->WorkerThread = PsGetCurrentThread();
    for (;;) {
        KeClearEvent(&s->Wake);
        KeAcquireSpinLock(&s->Lock, &irql);
        exit = s->Exit;
        KeReleaseSpinLock(&s->Lock, irql);
        if (exit) break;
        waitMs = PS_RETRY_MS;
        codecStatus = PsWanted(s) ? PsCodecStatus(s) : STATUS_SUCCESS;
        if (!NT_SUCCESS(codecStatus)) {
            PsSetStage(s, Es8316StageCodecReady);
            PsRecordReply(s, NULL, 0, 0, codecStatus, 0);
        }
        if (!PsWanted(s) || !NT_SUCCESS(codecStatus) || !PsBeginWork(s)) {
            if (s->PinHandle != NULL || s->FilterHandle != NULL) {
                PsStatus(s, Es8316PersistentStopping, STATUS_SUCCESS);
                PsClose(s);
            } else KeSetEvent(&s->Idle, IO_NO_INCREMENT, FALSE);
            PsAcknowledgeClosed(s);
            PsRetireNotification(s, FALSE);
            PsPrepareRediscovery(s);
            PsStatus(s, codecStatus == STATUS_NOT_SUPPORTED ? Es8316PersistentDisabled :
                (!NT_SUCCESS(codecStatus) ? Es8316PersistentWaiting : Es8316PersistentIdle),
                codecStatus == STATUS_NOT_SUPPORTED ? STATUS_SUCCESS : codecStatus);
        } else {
            if (s->PinHandle == NULL) {
                PsRetireNotification(s, FALSE);
                PsStatus(s, Es8316PersistentStarting, STATUS_SUCCESS);
                status = PsOpenStream(s);
                if (NT_SUCCESS(status) && !PsWanted(s)) status = STATUS_CANCELLED;
            } else status = PsFeed(s);
            if (NT_SUCCESS(status)) {
                PsStatus(s, Es8316PersistentRunning, STATUS_SUCCESS);
                waitMs = PS_POLL_MS;
            } else {
                PsClose(s);
                PsRetireNotification(s, FALSE);
                PsStatus(s, Es8316PersistentWaiting, status);
            }
        }
        timeout.QuadPart = -((LONGLONG)waitMs * 10000);
        KeWaitForSingleObject(&s->Wake, Executive, KernelMode, FALSE, &timeout);
    }
    PsStatus(s, Es8316PersistentStopping, STATUS_SUCCESS);
    PsClose(s);
    PsRetireNotification(s, TRUE);
    PsStatus(s, Es8316PersistentExited, STATUS_SUCCESS);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

NTSTATUS Es8316PersistentInitialize(WDFDEVICE device, ES8316_PERSISTENT_STREAM **stream)
{
    ES8316_PERSISTENT_STREAM *s;
    OBJECT_ATTRIBUTES attributes;
    NTSTATUS status;
    *stream = NULL;
    s = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*s), PS_TAG);
    if (s == NULL) return STATUS_INSUFFICIENT_RESOURCES;
    s->Device = device;
    KeInitializeSpinLock(&s->Lock);
    KeInitializeEvent(&s->Wake, NotificationEvent, FALSE);
    KeInitializeEvent(&s->Idle, NotificationEvent, TRUE);
    s->Status.Size = sizeof(s->Status);
    s->Status.Version = 2;
    s->Status.State = Es8316PersistentIdle;
    s->Status.SampleRate = PS_RATE;
    status = IoRegisterPlugPlayNotification(EventCategoryDeviceInterfaceChange, 0,
        (PVOID)&KSCATEGORY_RENDER, WdfDriverWdmGetDriverObject(WdfDeviceGetDriver(device)),
        PsInterfaceChange, s, &s->InterfaceNotification);
    if (!NT_SUCCESS(status)) { ExFreePoolWithTag(s, PS_TAG); return status; }
    InitializeObjectAttributes(&attributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    status = PsCreateSystemThread(&s->ThreadHandle, SYNCHRONIZE, &attributes, NULL, NULL, PsWorker, s);
    if (!NT_SUCCESS(status)) {
        IoUnregisterPlugPlayNotificationEx(s->InterfaceNotification);
        ExFreePoolWithTag(s, PS_TAG);
        return status;
    }
    *stream = s;
    return STATUS_SUCCESS;
}

VOID Es8316PersistentResume(ES8316_PERSISTENT_STREAM *s)
{
    KIRQL irql;
    if (s == NULL) return;
    KeAcquireSpinLock(&s->Lock, &irql);
    s->Wanted = TRUE;
    KeClearEvent(&s->Idle);
    KeReleaseSpinLock(&s->Lock, irql);
    KeSetEvent(&s->Wake, IO_NO_INCREMENT, FALSE);
}

VOID Es8316PersistentSuspend(ES8316_PERSISTENT_STREAM *s)
{
    KIRQL irql;
    if (s == NULL) return;
    KeAcquireSpinLock(&s->Lock, &irql);
    s->Wanted = FALSE;
    KeReleaseSpinLock(&s->Lock, irql);
    PsCancel(s);
    KeSetEvent(&s->Wake, IO_NO_INCREMENT, FALSE);
    KeWaitForSingleObject(&s->Idle, Executive, KernelMode, FALSE, NULL);
}

VOID Es8316PersistentCleanup(ES8316_PERSISTENT_STREAM *s)
{
    KIRQL irql;
    if (s == NULL) return;
    IoUnregisterPlugPlayNotificationEx(s->InterfaceNotification);
    KeAcquireSpinLock(&s->Lock, &irql);
    s->Exit = TRUE;
    s->Wanted = FALSE;
    KeReleaseSpinLock(&s->Lock, irql);
    PsCancel(s);
    KeSetEvent(&s->Wake, IO_NO_INCREMENT, FALSE);
    ZwWaitForSingleObject(s->ThreadHandle, FALSE, NULL);
    ZwClose(s->ThreadHandle);
    ExFreePoolWithTag(s, PS_TAG);
}

VOID Es8316PersistentGetStatus(ES8316_PERSISTENT_STREAM *s, ES8316_PERSISTENT_STATUS *status)
{
    KIRQL irql;
    RtlZeroMemory(status, sizeof(*status));
    status->Size = sizeof(*status);
    status->Version = 2;
    if (s == NULL) return;
    KeAcquireSpinLock(&s->Lock, &irql);
    *status = s->Status;
    status->Wanted = s->Wanted;
    status->RemovalBlocked = s->Blocked || s->InterfaceAbsent;
    KeReleaseSpinLock(&s->Lock, irql);
}
