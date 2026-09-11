//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "pnpevent.tmh"

//
// The user mode mirrors in fnlwfioctl.h must track netpnp.h exactly.
//
C_ASSERT((UINT32)FnLwfNetEventSetPower == (UINT32)NetEventSetPower);
C_ASSERT((UINT32)FnLwfNetEventQueryPower == (UINT32)NetEventQueryPower);
C_ASSERT((UINT32)FnLwfNetEventQueryRemoveDevice == (UINT32)NetEventQueryRemoveDevice);
C_ASSERT((UINT32)FnLwfNetEventPnPCapabilities == (UINT32)NetEventPnPCapabilities);
C_ASSERT((UINT32)FnLwfNetEventPause == (UINT32)NetEventPause);
C_ASSERT((UINT32)FnLwfNetEventRestart == (UINT32)NetEventRestart);
C_ASSERT((UINT32)FnLwfNetEventFilterPreDetach == (UINT32)NetEventFilterPreDetach);
C_ASSERT((UINT32)FnLwfNetEventUploadGftFlowEntries == (UINT32)NetEventUploadGftFlowEntries);
C_ASSERT((UINT32)FnLwfNetDeviceStateUnspecified == (UINT32)NetDeviceStateUnspecified);
C_ASSERT((UINT32)FnLwfNetDeviceStateD0 == (UINT32)NetDeviceStateD0);
C_ASSERT((UINT32)FnLwfNetDeviceStateD3 == (UINT32)NetDeviceStateD3);
C_ASSERT(FNLWF_PNP_EVENT_SUCCESS == NDIS_STATUS_SUCCESS);

typedef struct _DEFAULT_PNP_EVENT_ENTRY {
    LIST_ENTRY Link;
    UINT32 NetEvent;
    UINT32 BufferLength;
    UCHAR *Buffer;
} DEFAULT_PNP_EVENT_ENTRY;

//
// A NET_PNP_EVENT the filter returned NDIS_STATUS_PENDING for. The originating
// stack is blocked until this is forwarded and completed.
//
typedef struct _DEFAULT_PNP_EVENT_PENDED {
    LIST_ENTRY Link;
    NET_PNP_EVENT_NOTIFICATION *Notification;
    ULONGLONG Timestamp;
} DEFAULT_PNP_EVENT_PENDED;

typedef struct _DEFAULT_PNP_EVENT {
    DEFAULT_CONTEXT *Default;
    LIST_ENTRY PnpEventFilterLink;
    UINT32 NetEventFilter;
    BOOLEAN PendEvents;
    BOOLEAN QueueEvents;
    LIST_ENTRY PnpEventList;
    LIST_ENTRY PendedList;
} DEFAULT_PNP_EVENT;

static
VOID
PnpEventEntryCleanup(
    _In_ DEFAULT_PNP_EVENT_ENTRY *Entry
    )
{
    if (Entry->Buffer != NULL) {
        ExFreePoolWithTag(Entry->Buffer, POOLTAG_LWF_DEFAULT_PNPEVENT);
    }

    ExFreePoolWithTag(Entry, POOLTAG_LWF_DEFAULT_PNPEVENT);
}

static
_Requires_lock_held_(&PnpEventFile->Default->Filter->Lock)
VOID
PnpEventFlush(
    _In_ DEFAULT_PNP_EVENT *PnpEventFile
    )
{
    while (!IsListEmpty(&PnpEventFile->PnpEventList)) {
        LIST_ENTRY *Link = RemoveHeadList(&PnpEventFile->PnpEventList);
        DEFAULT_PNP_EVENT_ENTRY *Entry =
            CONTAINING_RECORD(Link, DEFAULT_PNP_EVENT_ENTRY, Link);

        PnpEventEntryCleanup(Entry);
    }
}

//
// Forwards a pended event and completes the pend the filter took out for it. A
// synchronous PENDING return means the rest of the stack owns completion.
//
static
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
PnpEventForwardPended(
    _In_ LWF_FILTER *Filter,
    _In_ DEFAULT_PNP_EVENT_PENDED *Pended,
    _In_ NDIS_STATUS CompletionStatus
    )
{
    NDIS_STATUS Status;

    if (CompletionStatus == NDIS_STATUS_SUCCESS) {
        Status = NdisFNetPnPEvent(Filter->NdisFilterHandle, Pended->Notification);
    } else {
        Status = CompletionStatus;
    }

    if (Status != NDIS_STATUS_PENDING) {
        NdisFNetPnPEventComplete(Filter->NdisFilterHandle, Pended->Notification, Status);
    }

    ExFreePoolWithTag(Pended, POOLTAG_LWF_DEFAULT_PNPEVENT);
}

VOID
PnpEventCleanup(
    _In_ DEFAULT_PNP_EVENT *PnpEventFile
    )
{
    LWF_FILTER *Filter = PnpEventFile->Default->Filter;
    LIST_ENTRY PendedList;
    KIRQL OldIrql;

    TraceEnter(TRACE_CONTROL, "PnpEventFile=%p", PnpEventFile);

    InitializeListHead(&PendedList);

    KeAcquireSpinLock(&Filter->Lock, &OldIrql);

    if (!IsListEmpty(&PnpEventFile->PnpEventFilterLink)) {
        RemoveEntryList(&PnpEventFile->PnpEventFilterLink);
        InitializeListHead(&PnpEventFile->PnpEventFilterLink);
    }

    PnpEventFlush(PnpEventFile);

    //
    // Releasing the file must not strand a pended event, or the originating
    // stack never makes progress.
    //
    if (!IsListEmpty(&PnpEventFile->PendedList)) {
        AppendTailList(&PendedList, &PnpEventFile->PendedList);
        RemoveEntryList(&PnpEventFile->PendedList);
        InitializeListHead(&PnpEventFile->PendedList);
    }

    KeReleaseSpinLock(&Filter->Lock, OldIrql);

    while (!IsListEmpty(&PendedList)) {
        LIST_ENTRY *Link = RemoveHeadList(&PendedList);
        DEFAULT_PNP_EVENT_PENDED *Pended =
            CONTAINING_RECORD(Link, DEFAULT_PNP_EVENT_PENDED, Link);

        PnpEventForwardPended(Filter, Pended, NDIS_STATUS_SUCCESS);
    }

    ExFreePoolWithTag(PnpEventFile, POOLTAG_LWF_DEFAULT_PNPEVENT);

    TraceExitSuccess(TRACE_CONTROL);
}

DEFAULT_PNP_EVENT *
PnpEventCreate(
    _In_ DEFAULT_CONTEXT *Default
    )
{
    DEFAULT_PNP_EVENT *PnpEventFile;

    TraceEnter(TRACE_CONTROL, "Default=%p", Default);

    PnpEventFile =
        ExAllocatePoolZero(
            NonPagedPoolNx, sizeof(*PnpEventFile), POOLTAG_LWF_DEFAULT_PNPEVENT);
    if (PnpEventFile == NULL) {
        goto Exit;
    }

    PnpEventFile->Default = Default;
    InitializeListHead(&PnpEventFile->PnpEventFilterLink);
    InitializeListHead(&PnpEventFile->PnpEventList);
    InitializeListHead(&PnpEventFile->PendedList);

    TraceVerbose(TRACE_CONTROL, "Default=%p PnpEventFile=%p", Default, PnpEventFile);

Exit:

    TraceExitSuccess(TRACE_CONTROL);

    return PnpEventFile;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
PnpEventIrpFilter(
    _In_ DEFAULT_PNP_EVENT *PnpEventFile,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    const PNP_EVENT_FILTER_IN *In = Irp->AssociatedIrp.SystemBuffer;
    LWF_FILTER *Filter = PnpEventFile->Default->Filter;
    NTSTATUS Status;
    KIRQL OldIrql;

    TraceEnter(TRACE_CONTROL, "PnpEventFile=%p", PnpEventFile);

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(*In)) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    KeAcquireSpinLock(&Filter->Lock, &OldIrql);

    PnpEventFile->NetEventFilter = In->NetEvent;
    PnpEventFile->PendEvents = !!In->PendEvents;
    PnpEventFile->QueueEvents = !!In->QueueEvents;
    PnpEventFlush(PnpEventFile);

    if (IsListEmpty(&PnpEventFile->PnpEventFilterLink)) {
        InsertTailList(&Filter->PnpEventFilterList, &PnpEventFile->PnpEventFilterLink);
    }

    TraceVerbose(
        TRACE_CONTROL,
        "PnpEventFile=%p NetEventFilter=%u PendEvents=%u QueueEvents=%u",
        PnpEventFile, In->NetEvent, In->PendEvents, In->QueueEvents);

    KeReleaseSpinLock(&Filter->Lock, OldIrql);

    Status = STATUS_SUCCESS;

Exit:

    TraceExitStatus(TRACE_CONTROL);

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
PnpEventIrpGet(
    _In_ DEFAULT_PNP_EVENT *PnpEventFile,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    LWF_FILTER *Filter = PnpEventFile->Default->Filter;
    UINT32 OutputBufferLength = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;
    SIZE_T *BytesReturned = &Irp->IoStatus.Information;
    PNP_EVENT_NOTIFICATION_OUT *Out;
    DEFAULT_PNP_EVENT_ENTRY *Entry;
    UINT32 RequiredSize;
    NTSTATUS Status;
    KIRQL OldIrql;

    TraceEnter(TRACE_CONTROL, "PnpEventFile=%p", PnpEventFile);

    KeAcquireSpinLock(&Filter->Lock, &OldIrql);

    if (IsListEmpty(&PnpEventFile->PnpEventList)) {
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

    Entry =
        CONTAINING_RECORD(PnpEventFile->PnpEventList.Flink, DEFAULT_PNP_EVENT_ENTRY, Link);

    Status = RtlUInt32Add(sizeof(*Out), Entry->BufferLength, &RequiredSize);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    if ((OutputBufferLength == 0) && (Irp->Flags & IRP_INPUT_OPERATION) == 0) {
        *BytesReturned = RequiredSize;
        Status = STATUS_BUFFER_OVERFLOW;
        goto Exit;
    }

    if (OutputBufferLength < RequiredSize) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    Out = Irp->AssociatedIrp.SystemBuffer;
    Out->NetEvent = Entry->NetEvent;
    Out->BufferLength = Entry->BufferLength;

    if (Entry->BufferLength > 0) {
        RtlCopyMemory(Out + 1, Entry->Buffer, Entry->BufferLength);
    }

    *BytesReturned = RequiredSize;
    RemoveEntryList(&Entry->Link);
    PnpEventEntryCleanup(Entry);
    Status = STATUS_SUCCESS;

    //
    // A test that is actively draining events is making progress, so treat this
    // as liveness and let it hold its pends longer.
    //
    for (LIST_ENTRY *Link = PnpEventFile->PendedList.Flink;
         Link != &PnpEventFile->PendedList;
         Link = Link->Flink) {
        CONTAINING_RECORD(Link, DEFAULT_PNP_EVENT_PENDED, Link)->Timestamp =
            KeQueryUnbiasedInterruptTime();
    }

Exit:

    KeReleaseSpinLock(&Filter->Lock, OldIrql);

    TraceExitStatus(TRACE_CONTROL);

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
PnpEventIrpComplete(
    _In_ DEFAULT_PNP_EVENT *PnpEventFile,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    const PNP_EVENT_COMPLETE_IN *In = Irp->AssociatedIrp.SystemBuffer;
    LWF_FILTER *Filter = PnpEventFile->Default->Filter;
    DEFAULT_PNP_EVENT_PENDED *Pended = NULL;
    NTSTATUS Status;
    KIRQL OldIrql;

    TraceEnter(TRACE_CONTROL, "PnpEventFile=%p", PnpEventFile);

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(*In)) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    KeAcquireSpinLock(&Filter->Lock, &OldIrql);

    if (!IsListEmpty(&PnpEventFile->PendedList)) {
        LIST_ENTRY *Link = RemoveHeadList(&PnpEventFile->PendedList);
        Pended = CONTAINING_RECORD(Link, DEFAULT_PNP_EVENT_PENDED, Link);
    }

    KeReleaseSpinLock(&Filter->Lock, OldIrql);

    if (Pended == NULL) {
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

    PnpEventForwardPended(Filter, Pended, In->CompletionStatus);
    Status = STATUS_SUCCESS;

Exit:

    TraceExitStatus(TRACE_CONTROL);

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
PnpEventWatchdogTimeout(
    _In_ LWF_FILTER *Filter
    )
{
    static const ULONGLONG WatchdogGracePeriod = RTL_SEC_TO_100NANOSEC(5);
    ULONGLONG CurrentTime = KeQueryUnbiasedInterruptTime();
    BOOLEAN Expired = FALSE;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Filter->Lock, &OldIrql);

    for (LIST_ENTRY *FileLink = Filter->PnpEventFilterList.Flink;
         FileLink != &Filter->PnpEventFilterList && !Expired;
         FileLink = FileLink->Flink) {
        DEFAULT_PNP_EVENT *PnpEventFile =
            CONTAINING_RECORD(FileLink, DEFAULT_PNP_EVENT, PnpEventFilterLink);

        for (LIST_ENTRY *Link = PnpEventFile->PendedList.Flink;
             Link != &PnpEventFile->PendedList;
             Link = Link->Flink) {
            DEFAULT_PNP_EVENT_PENDED *Pended =
                CONTAINING_RECORD(Link, DEFAULT_PNP_EVENT_PENDED, Link);

            if (CurrentTime > Pended->Timestamp + WatchdogGracePeriod) {
                Expired = TRUE;
                break;
            }
        }
    }

    KeReleaseSpinLock(&Filter->Lock, OldIrql);

    if (Expired) {
        FilterWatchdogFailure(Filter, "PnP event");
    }
}

_IRQL_requires_max_(PASSIVE_LEVEL)
_Function_class_(FILTER_NET_PNP_EVENT)
NDIS_STATUS
FilterNetPnPEvent(    _In_ NDIS_HANDLE FilterModuleContext,
    _In_ NET_PNP_EVENT_NOTIFICATION *NetPnPEventNotification
    )
{
    LWF_FILTER *Filter = (LWF_FILTER *)FilterModuleContext;
    NET_PNP_EVENT *NetPnPEvent = &NetPnPEventNotification->NetPnPEvent;
    DEFAULT_PNP_EVENT_ENTRY *Entry = NULL;
    DEFAULT_PNP_EVENT_PENDED *Pended = NULL;
    LIST_ENTRY *Link;
    BOOLEAN Pend = FALSE;
    KIRQL OldIrql;

    TraceEnter(
        TRACE_CONTROL,
        "Filter=%p NetEvent=%u BufferLength=%u",
        Filter, NetPnPEvent->NetEvent, NetPnPEvent->BufferLength);

    //
    // Allocate before taking the lock, since the queue and pend paths cannot
    // fail cleanly once the event has been claimed.
    //
    Entry = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Entry), POOLTAG_LWF_DEFAULT_PNPEVENT);
    Pended = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Pended), POOLTAG_LWF_DEFAULT_PNPEVENT);

    if (Entry != NULL && NetPnPEvent->BufferLength > 0) {
        Entry->Buffer =
            ExAllocatePoolZero(
                NonPagedPoolNx, NetPnPEvent->BufferLength, POOLTAG_LWF_DEFAULT_PNPEVENT);

        if (Entry->Buffer == NULL) {
            PnpEventEntryCleanup(Entry);
            Entry = NULL;
        }
    }

    KeAcquireSpinLock(&Filter->Lock, &OldIrql);

    Link = Filter->PnpEventFilterList.Flink;

    while (Link != &Filter->PnpEventFilterList) {
        DEFAULT_PNP_EVENT *PnpEventFile =
            CONTAINING_RECORD(Link, DEFAULT_PNP_EVENT, PnpEventFilterLink);
        Link = Link->Flink;

        if (PnpEventFile->NetEventFilter != (UINT32)NetPnPEvent->NetEvent) {
            continue;
        }

        if (PnpEventFile->QueueEvents && Entry != NULL) {
            Entry->NetEvent = NetPnPEvent->NetEvent;
            Entry->BufferLength = NetPnPEvent->BufferLength;

            if (NetPnPEvent->BufferLength > 0) {
                RtlCopyMemory(Entry->Buffer, NetPnPEvent->Buffer, NetPnPEvent->BufferLength);
            }

            InsertTailList(&PnpEventFile->PnpEventList, &Entry->Link);
            Entry = NULL;
        }

        if (PnpEventFile->PendEvents && Pended != NULL) {
            Pended->Notification = NetPnPEventNotification;
            Pended->Timestamp = KeQueryUnbiasedInterruptTime();
            InsertTailList(&PnpEventFile->PendedList, &Pended->Link);
            Pended = NULL;
            Pend = TRUE;
        }

        break;
    }

    KeReleaseSpinLock(&Filter->Lock, OldIrql);

    if (Entry != NULL) {
        PnpEventEntryCleanup(Entry);
    }

    if (Pended != NULL) {
        ExFreePoolWithTag(Pended, POOLTAG_LWF_DEFAULT_PNPEVENT);
    }

    TraceExitSuccess(TRACE_CONTROL);

    if (Pend) {
        return NDIS_STATUS_PENDING;
    }

    return NdisFNetPnPEvent(Filter->NdisFilterHandle, NetPnPEventNotification);
}
