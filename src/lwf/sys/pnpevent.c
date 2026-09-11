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
// NDIS has no filter-side completion routine for NET_PNP_EVENT, so a filter
// cannot return NDIS_STATUS_PENDING. The handler runs at PASSIVE_LEVEL and
// holds the event by blocking, which keeps the indication alive for the whole
// hold. That lets this reference the caller's notification directly and live on
// the blocked handler's stack, so the indication path allocates nothing.
//
typedef struct _DEFAULT_PNP_EVENT_HELD {
    LIST_ENTRY Link;
    NET_PNP_EVENT_NOTIFICATION *Notification;
    KEVENT ReleaseEvent;
    NDIS_STATUS CompletionStatus;
    ULONGLONG Timestamp;
} DEFAULT_PNP_EVENT_HELD;

typedef struct _DEFAULT_PNP_EVENT {
    DEFAULT_CONTEXT *Default;
    LIST_ENTRY PnpEventFilterLink;
    UINT32 NetEventFilter;
    BOOLEAN HoldEvents;
    LIST_ENTRY HeldList;
} DEFAULT_PNP_EVENT;

//
// Wakes the thread blocked in the handler. The caller must have already removed
// the record from the list, since the stack frame owning it unwinds from here.
//
static
VOID
PnpEventRelease(
    _In_ DEFAULT_PNP_EVENT_HELD *Held,
    _In_ NDIS_STATUS CompletionStatus
    )
{
    Held->CompletionStatus = CompletionStatus;
    KeSetEvent(&Held->ReleaseEvent, IO_NO_INCREMENT, FALSE);
}

VOID
PnpEventCleanup(
    _In_ DEFAULT_PNP_EVENT *PnpEventFile
    )
{
    LWF_FILTER *Filter = PnpEventFile->Default->Filter;
    LIST_ENTRY HeldList;
    KIRQL OldIrql;

    TraceEnter(TRACE_CONTROL, "PnpEventFile=%p", PnpEventFile);

    InitializeListHead(&HeldList);

    KeAcquireSpinLock(&Filter->Lock, &OldIrql);

    if (!IsListEmpty(&PnpEventFile->PnpEventFilterLink)) {
        RemoveEntryList(&PnpEventFile->PnpEventFilterLink);
        InitializeListHead(&PnpEventFile->PnpEventFilterLink);
    }

    //
    // Releasing the file must not strand a held event, or the originating stack
    // never makes progress.
    //
    if (!IsListEmpty(&PnpEventFile->HeldList)) {
        AppendTailList(&HeldList, &PnpEventFile->HeldList);
        RemoveEntryList(&PnpEventFile->HeldList);
        InitializeListHead(&PnpEventFile->HeldList);
    }

    KeReleaseSpinLock(&Filter->Lock, OldIrql);

    while (!IsListEmpty(&HeldList)) {
        LIST_ENTRY *Link = RemoveHeadList(&HeldList);

        PnpEventRelease(
            CONTAINING_RECORD(Link, DEFAULT_PNP_EVENT_HELD, Link), NDIS_STATUS_SUCCESS);
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
    InitializeListHead(&PnpEventFile->HeldList);

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
    PnpEventFile->HoldEvents = !!In->HoldEvents;

    if (IsListEmpty(&PnpEventFile->PnpEventFilterLink)) {
        InsertTailList(&Filter->PnpEventFilterList, &PnpEventFile->PnpEventFilterLink);
    }

    TraceVerbose(
        TRACE_CONTROL,
        "PnpEventFile=%p NetEventFilter=%u HoldEvents=%u",
        PnpEventFile, In->NetEvent, In->HoldEvents);

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
    DEFAULT_PNP_EVENT_HELD *Held;
    NET_PNP_EVENT *NetPnPEvent;
    UINT32 RequiredSize;
    NTSTATUS Status;
    KIRQL OldIrql;

    TraceEnter(TRACE_CONTROL, "PnpEventFile=%p", PnpEventFile);

    KeAcquireSpinLock(&Filter->Lock, &OldIrql);

    if (IsListEmpty(&PnpEventFile->HeldList)) {
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

    //
    // The indication is still owned by the blocked handler, so it is safe to
    // read here.
    //
    Held = CONTAINING_RECORD(PnpEventFile->HeldList.Flink, DEFAULT_PNP_EVENT_HELD, Link);
    NetPnPEvent = &Held->Notification->NetPnPEvent;

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
    // this as liveness and let it hold longer.
    //
    for (LIST_ENTRY *Link = PnpEventFile->HeldList.Flink;
         Link != &PnpEventFile->HeldList;
         Link = Link->Flink) {
        CONTAINING_RECORD(Link, DEFAULT_PNP_EVENT_HELD, Link)->Timestamp =
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
    DEFAULT_PNP_EVENT_HELD *Held = NULL;
    NTSTATUS Status;
    KIRQL OldIrql;

    TraceEnter(TRACE_CONTROL, "PnpEventFile=%p", PnpEventFile);

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(*In)) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    KeAcquireSpinLock(&Filter->Lock, &OldIrql);

    if (!IsListEmpty(&PnpEventFile->HeldList)) {
        LIST_ENTRY *Link = RemoveHeadList(&PnpEventFile->HeldList);
        Held = CONTAINING_RECORD(Link, DEFAULT_PNP_EVENT_HELD, Link);
    }

    KeReleaseSpinLock(&Filter->Lock, OldIrql);

    if (Held == NULL) {
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

    PnpEventRelease(Held, In->CompletionStatus);
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

        for (LIST_ENTRY *Link = PnpEventFile->HeldList.Flink;
             Link != &PnpEventFile->HeldList;
             Link = Link->Flink) {
            DEFAULT_PNP_EVENT_HELD *Held =
                CONTAINING_RECORD(Link, DEFAULT_PNP_EVENT_HELD, Link);

            if (CurrentTime > Held->Timestamp + WatchdogGracePeriod) {
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
    DEFAULT_PNP_EVENT_HELD Held;
    BOOLEAN Holding = FALSE;
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

        if (PnpEventFile->HoldEvents) {
            Held.Notification = NetPnPEventNotification;
            Held.CompletionStatus = NDIS_STATUS_SUCCESS;
            Held.Timestamp = KeQueryUnbiasedInterruptTime();
            KeInitializeEvent(&Held.ReleaseEvent, NotificationEvent, FALSE);
            InsertTailList(&PnpEventFile->HeldList, &Held.Link);
            Holding = TRUE;
        }

        break;
    }

    KeReleaseSpinLock(&Filter->Lock, OldIrql);

    if (Holding) {
        KeWaitForSingleObject(&Held.ReleaseEvent, Executive, KernelMode, FALSE, NULL);

        if (Held.CompletionStatus != NDIS_STATUS_SUCCESS) {
            TraceExitSuccess(TRACE_CONTROL);
            return Held.CompletionStatus;
        }
    }

    TraceExitSuccess(TRACE_CONTROL);

    return NdisFNetPnPEvent(Filter->NdisFilterHandle, NetPnPEventNotification);
}
