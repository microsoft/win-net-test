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

//
// A pended NET_PNP_EVENT. NDIS has no filter-side completion routine, so a
// filter cannot return NDIS_STATUS_PENDING; the pend is implemented by blocking
// the calling thread, which the handler may do at PASSIVE_LEVEL. Blocking keeps
// the indication alive for the whole pend, so this can reference the caller's
// notification directly and live on the blocked handler's stack, leaving the
// indication path free of allocations.
//
typedef struct _DEFAULT_PNP_EVENT_PENDED {
    LIST_ENTRY Link;
    NET_PNP_EVENT_NOTIFICATION *Notification;
    KEVENT ReleaseEvent;
    NDIS_STATUS CompletionStatus;
    ULONGLONG Timestamp;
} DEFAULT_PNP_EVENT_PENDED;

typedef struct _DEFAULT_PNP_EVENT {
    DEFAULT_CONTEXT *Default;
    LIST_ENTRY PnpEventFilterLink;
    UINT32 NetEventFilter;
    LIST_ENTRY PendedList;
} DEFAULT_PNP_EVENT;

//
// Completes a pend. The caller must have already removed the record from the
// list, since the stack frame owning it unwinds from here.
//
static
VOID
PnpEventCompletePended(
    _In_ DEFAULT_PNP_EVENT_PENDED *Pended,
    _In_ NDIS_STATUS CompletionStatus
    )
{
    Pended->CompletionStatus = CompletionStatus;
    KeSetEvent(&Pended->ReleaseEvent, IO_NO_INCREMENT, FALSE);
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

        PnpEventCompletePended(
            CONTAINING_RECORD(Link, DEFAULT_PNP_EVENT_PENDED, Link), NDIS_STATUS_SUCCESS);
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

    if (IsListEmpty(&PnpEventFile->PnpEventFilterLink)) {
        InsertTailList(&Filter->PnpEventFilterList, &PnpEventFile->PnpEventFilterLink);
    }

    TraceVerbose(
        TRACE_CONTROL,
        "PnpEventFile=%p NetEventFilter=%u",
        PnpEventFile, In->NetEvent);

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
    DEFAULT_PNP_EVENT_PENDED *Pended;
    NET_PNP_EVENT *NetPnPEvent;
    UINT32 RequiredSize;
    NTSTATUS Status;
    KIRQL OldIrql;

    TraceEnter(TRACE_CONTROL, "PnpEventFile=%p", PnpEventFile);

    KeAcquireSpinLock(&Filter->Lock, &OldIrql);

    if (IsListEmpty(&PnpEventFile->PendedList)) {
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

    //
    // The indication is still owned by the pended handler, so it is safe to
    // read here.
    //
    Pended = CONTAINING_RECORD(PnpEventFile->PendedList.Flink, DEFAULT_PNP_EVENT_PENDED, Link);
    NetPnPEvent = &Pended->Notification->NetPnPEvent;

    Status = RtlUInt32Add(sizeof(*Out), NetPnPEvent->BufferLength, &RequiredSize);
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
    Out->NetEvent = NetPnPEvent->NetEvent;
    Out->BufferLength = NetPnPEvent->BufferLength;

    if (NetPnPEvent->BufferLength > 0) {
        RtlCopyMemory(Out + 1, NetPnPEvent->Buffer, NetPnPEvent->BufferLength);
    }

    *BytesReturned = RequiredSize;
    Status = STATUS_SUCCESS;

    //
    // A test that is actively inspecting events is making progress, so treat
    // this as liveness and let it pend longer.
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

    PnpEventCompletePended(Pended, In->CompletionStatus);
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
FilterNetPnPEvent(
    _In_ NDIS_HANDLE FilterModuleContext,
    _In_ NET_PNP_EVENT_NOTIFICATION *NetPnPEventNotification
    )
{
    LWF_FILTER *Filter = (LWF_FILTER *)FilterModuleContext;
    NET_PNP_EVENT *NetPnPEvent = &NetPnPEventNotification->NetPnPEvent;
    DEFAULT_PNP_EVENT_PENDED Pended;
    BOOLEAN Pending = FALSE;
    LIST_ENTRY *Link;
    KIRQL OldIrql;

    TraceEnter(
        TRACE_CONTROL,
        "Filter=%p NetEvent=%u BufferLength=%u",
        Filter, NetPnPEvent->NetEvent, NetPnPEvent->BufferLength);

    KeAcquireSpinLock(&Filter->Lock, &OldIrql);

    Link = Filter->PnpEventFilterList.Flink;

    while (Link != &Filter->PnpEventFilterList) {
        DEFAULT_PNP_EVENT *PnpEventFile =
            CONTAINING_RECORD(Link, DEFAULT_PNP_EVENT, PnpEventFilterLink);
        Link = Link->Flink;

        if (PnpEventFile->NetEventFilter != (UINT32)NetPnPEvent->NetEvent) {
            continue;
        }

        Pended.Notification = NetPnPEventNotification;
        Pended.CompletionStatus = NDIS_STATUS_SUCCESS;
        Pended.Timestamp = KeQueryUnbiasedInterruptTime();
        KeInitializeEvent(&Pended.ReleaseEvent, NotificationEvent, FALSE);
        InsertTailList(&PnpEventFile->PendedList, &Pended.Link);
        Pending = TRUE;
        break;
    }

    KeReleaseSpinLock(&Filter->Lock, OldIrql);

    if (Pending) {
        KeWaitForSingleObject(&Pended.ReleaseEvent, Executive, KernelMode, FALSE, NULL);

        if (Pended.CompletionStatus != NDIS_STATUS_SUCCESS) {
            TraceExitSuccess(TRACE_CONTROL);
            return Pended.CompletionStatus;
        }
    }

    TraceExitSuccess(TRACE_CONTROL);

    return NdisFNetPnPEvent(Filter->NdisFilterHandle, NetPnPEventNotification);
}
