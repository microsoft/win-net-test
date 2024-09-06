//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "port.tmh"

typedef enum _FN_PORT_STATE {
    FnPortStateAllocated,
    FnPortStateActivating,
    FnPortStateActivated,
    FnPortStateDeactivating,
} FN_PORT_STATE;

typedef struct _FN_PORT_ENTRY {
    NDIS_PORT_NUMBER Number;
    LIST_ENTRY Link;
    FN_PORT_STATE State;
} FN_PORT_ENTRY;

static
_Requires_lock_held_(Adapter->PushLock)
FN_PORT_ENTRY *
MpFindPortEntry(
    _In_ ADAPTER_CONTEXT *Adapter,
    _In_ NDIS_PORT_NUMBER PortNumber
    )
{
    LIST_ENTRY *Entry = Adapter->PortList.Flink;

    while (Entry != &Adapter->PortList) {
        FN_PORT_ENTRY *PortEntry = CONTAINING_RECORD(Entry, FN_PORT_ENTRY, Link);
        Entry = Entry->Flink;
        if (PortEntry->Number == PortNumber) {
            return PortEntry;
        }
    }

    return NULL;
}

static
MpFreePortEntry(
    _In_ FN_PORT_ENTRY *PortEntry
    )
{
    ExFreePoolWithTag(PortEntry, POOLTAG_MP_PORT);
}

_Requires_exclusive_lock_held_(Adapter->PushLock)
NTSTATUS
MpFreePort(
    _In_ ADAPTER_CONTEXT *Adapter,
    _In_ FN_PORT_ENTRY *PortEntry
    )
{
    NTSTATUS Status;

    TraceEnter(TRACE_CONTROL, "Adapter=%p PortNumber=%d", Adapter, PortEntry->Number);

    ASSERT(PortEntry->State == FnPortStateAllocated);

    Status = NdisMFreePort(Adapter->MiniportHandle, PortEntry->Number);
    if (Status != NDIS_STATUS_SUCCESS) {
        Status = FnConvertNdisStatusToNtStatus(Status);
        goto Exit;
    }

    RemoveEntryList(&PortEntry->Link);
    MpFreePortEntry(PortEntry);

Exit:

    TraceExitStatus(TRACE_CONTROL);

    return Status;
}

static
VOID
MpInitializePortCharacteristics(
    _Out_ NDIS_PORT_CHARACTERISTICS *PortCharacteristics
    )
{
    RtlZeroMemory(PortCharacteristics, sizeof(*PortCharacteristics));
    PortCharacteristics->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    PortCharacteristics->Header.Revision = NDIS_PORT_CHARACTERISTICS_REVISION_1;
    PortCharacteristics->Header.Size = NDIS_SIZEOF_PORT_CHARACTERISTICS_REVISION_1;
    PortCharacteristics->Type = NdisPortTypeUndefined;
    PortCharacteristics->Direction = NET_IF_DIRECTION_SENDRECEIVE;
    PortCharacteristics->SendControlState = NdisPortControlStateUncontrolled;
    PortCharacteristics->RcvControlState = NdisPortControlStateUncontrolled;
    PortCharacteristics->SendAuthorizationState = NdisPortAuthorizationUnknown;
    PortCharacteristics->RcvAuthorizationState = NdisPortAuthorizationUnknown;
    PortCharacteristics->XmitLinkSpeed = MpGlobalContext.XmitLinkSpeed;
    PortCharacteristics->RcvLinkSpeed = MpGlobalContext.RecvLinkSpeed;
    PortCharacteristics->MediaConnectState = MediaConnectStateConnected;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
MpIrpAllocatePort(
    _In_ EXCLUSIVE_USER_CONTEXT *UserContext,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    ADAPTER_CONTEXT *Adapter = UserContext->Adapter;
    FN_PORT_ENTRY *PortEntry = NULL;
    NDIS_PORT_CHARACTERISTICS PortCharacteristics;

    TraceEnter(TRACE_CONTROL, "Adapter=%p", Adapter);

    if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(PortEntry->Number)) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    PortEntry = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*PortEntry), POOLTAG_MP_PORT);
    if (PortEntry == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    MpInitializePortCharacteristics(&PortCharacteristics);
    Status = NdisMAllocatePort(Adapter->MiniportHandle, &PortCharacteristics);
    if (Status != NDIS_STATUS_SUCCESS) {
        Status = FnConvertNdisStatusToNtStatus(Status);
        goto Exit;
    }

    PortEntry->Number = PortCharacteristics.PortNumber;
    PortEntry->State = FnPortStateAllocated;

    RtlAcquirePushLockExclusive(&Adapter->PushLock);
    ASSERT(MpFindPortEntry(Adapter, PortEntry->Number) == NULL);
    InsertTailList(&Adapter->PortList, &PortEntry->Link);
    RtlReleasePushLockExclusive(&Adapter->PushLock);

    *(NDIS_PORT_NUMBER *)Irp->AssociatedIrp.SystemBuffer = PortEntry->Number;
    Irp->IoStatus.Information = sizeof(PortEntry->Number);
    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        if (PortEntry != NULL) {
            MpFreePortEntry(PortEntry);
        }
    }

    TraceExitStatus(TRACE_CONTROL);

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
MpIrpFreePort(
    _In_ EXCLUSIVE_USER_CONTEXT *UserContext,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    ADAPTER_CONTEXT *Adapter = UserContext->Adapter;
    const NDIS_PORT_NUMBER *PortNumber = Irp->AssociatedIrp.SystemBuffer;
    FN_PORT_ENTRY *PortEntry;
    BOOLEAN LockHeld = FALSE;

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(PortEntry->Number)) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    TraceEnter(TRACE_CONTROL, "Adapter=%p PortNumber=%d", Adapter, *PortNumber);

    RtlAcquirePushLockExclusive(&Adapter->PushLock);
    LockHeld = TRUE;

    PortEntry = MpFindPortEntry(Adapter, *PortNumber);
    if (PortEntry == NULL) {
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

    if (PortEntry->State != FnPortStateAllocated) {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Exit;
    }

    Status = MpFreePort(Adapter, PortEntry);

    RtlReleasePushLockExclusive(&Adapter->PushLock);
    LockHeld = FALSE;

Exit:

    if (LockHeld) {
        RtlReleasePushLockExclusive(&Adapter->PushLock);
    }

    TraceExitStatus(TRACE_CONTROL);

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
MpIrpActivatePort(
    _In_ EXCLUSIVE_USER_CONTEXT *UserContext,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    ADAPTER_CONTEXT *Adapter = UserContext->Adapter;
    const NDIS_PORT_NUMBER *PortNumber = Irp->AssociatedIrp.SystemBuffer;
    NDIS_PORT Port;
    NET_PNP_EVENT_NOTIFICATION PnpEvent;
    FN_PORT_ENTRY *PortEntry;
    BOOLEAN LockHeld = FALSE;

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(PortEntry->Number)) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    TraceEnter(TRACE_CONTROL, "Adapter=%p PortNumber=%d", Adapter, *PortNumber);

    RtlAcquirePushLockExclusive(&Adapter->PushLock);
    LockHeld = TRUE;

    PortEntry = MpFindPortEntry(Adapter, *PortNumber);
    if (PortEntry == NULL) {
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

    if (PortEntry->State != FnPortStateAllocated) {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Exit;
    }

    PortEntry->State = FnPortStateActivating;

    RtlReleasePushLockExclusive(&Adapter->PushLock);
    LockHeld = FALSE;

    RtlZeroMemory(&Port, sizeof(Port));
    MpInitializePortCharacteristics(&Port.PortCharacteristics);
    Port.PortCharacteristics.PortNumber = PortEntry->Number;
    Port.Next = NULL;

    RtlZeroMemory(&PnpEvent, sizeof(NET_PNP_EVENT_NOTIFICATION));
    PnpEvent.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    PnpEvent.Header.Revision = NET_PNP_EVENT_NOTIFICATION_REVISION_1;
    PnpEvent.Header.Size = NDIS_SIZEOF_NET_PNP_EVENT_NOTIFICATION_REVISION_1;
    PnpEvent.NetPnPEvent.NetEvent = NetEventPortActivation;
    PnpEvent.NetPnPEvent.Buffer = &Port;
    PnpEvent.NetPnPEvent.BufferLength = sizeof(Port);

    Status = NdisMNetPnPEvent(Adapter->MiniportHandle, &PnpEvent);

    RtlAcquirePushLockExclusive(&Adapter->PushLock);
    LockHeld = TRUE;

    if (Status != NDIS_STATUS_SUCCESS) {
        ASSERT(PortEntry->State == FnPortStateActivating);
        PortEntry->State = FnPortStateAllocated;
        Status = FnConvertNdisStatusToNtStatus(Status);
        goto Exit;
    }

    PortEntry->State = FnPortStateActivated;

Exit:

    if (LockHeld) {
        RtlReleasePushLockExclusive(&Adapter->PushLock);
    }

    TraceExitStatus(TRACE_CONTROL);

    return Status;
}

_Requires_exclusive_lock_held_(Adapter->PushLock)
_Releases_lock_(Adapter->PushLock)
_Acquires_exclusive_lock_(Adapter->PushLock)
static
NTSTATUS
MpDeactivatePort(
    _In_ ADAPTER_CONTEXT *Adapter,
    _In_ FN_PORT_ENTRY *PortEntry
    )
{
    NTSTATUS Status;
    NET_PNP_EVENT_NOTIFICATION PnpEvent;

    //
    // N.B. This routine releases and acquires the adapter pushlock.
    //

    TraceEnter(TRACE_CONTROL, "Adapter=%p PortNumber=%d", Adapter, PortEntry->Number);

    ASSERT(PortEntry->State == FnPortStateActivated);

    PortEntry->State = FnPortStateDeactivating;

    RtlReleasePushLockExclusive(&Adapter->PushLock);

    RtlZeroMemory(&PnpEvent, sizeof(NET_PNP_EVENT_NOTIFICATION));
    PnpEvent.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    PnpEvent.Header.Revision = NET_PNP_EVENT_NOTIFICATION_REVISION_1;
    PnpEvent.Header.Size = NDIS_SIZEOF_NET_PNP_EVENT_NOTIFICATION_REVISION_1;
    PnpEvent.PortNumber = PortEntry->Number;
    PnpEvent.NetPnPEvent.NetEvent = NetEventPortDeactivation;
    PnpEvent.NetPnPEvent.Buffer = &PortEntry->Number;
    PnpEvent.NetPnPEvent.BufferLength = sizeof(PortEntry->Number);

    Status = NdisMNetPnPEvent(Adapter->MiniportHandle, &PnpEvent);

    RtlAcquirePushLockExclusive(&Adapter->PushLock);

    if (Status != NDIS_STATUS_SUCCESS) {
        ASSERT(PortEntry->State == FnPortStateDeactivating);
        PortEntry->State = FnPortStateActivated;
        Status = FnConvertNdisStatusToNtStatus(Status);
        goto Exit;
    }

    PortEntry->State = FnPortStateAllocated;

Exit:

    TraceExitStatus(TRACE_CONTROL);

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
MpIrpDeactivatePort(
    _In_ EXCLUSIVE_USER_CONTEXT *UserContext,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    ADAPTER_CONTEXT *Adapter = UserContext->Adapter;
    const NDIS_PORT_NUMBER *PortNumber = Irp->AssociatedIrp.SystemBuffer;
    FN_PORT_ENTRY *PortEntry;
    BOOLEAN LockHeld = FALSE;

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(PortEntry->Number)) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    TraceEnter(TRACE_CONTROL, "Adapter=%p PortNumber=%d", Adapter, *PortNumber);

    RtlAcquirePushLockExclusive(&Adapter->PushLock);
    LockHeld = TRUE;

    PortEntry = MpFindPortEntry(Adapter, *PortNumber);
    if (PortEntry == NULL) {
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

    if (PortEntry->State != FnPortStateActivated) {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Exit;
    }

    Status = MpDeactivatePort(Adapter, PortEntry);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

Exit:

    if (LockHeld) {
        RtlReleasePushLockExclusive(&Adapter->PushLock);
    }

    TraceExitStatus(TRACE_CONTROL);

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
MpPortCleanup(
    _In_ ADAPTER_CONTEXT *Adapter
    )
{
    TraceEnter(TRACE_CONTROL, "Adapter=%p", Adapter);

    RtlAcquirePushLockExclusive(&Adapter->PushLock);

    while (!IsListEmpty(&Adapter->PortList)) {
        FN_PORT_ENTRY *PortEntry = CONTAINING_RECORD(Adapter->PortList.Flink, FN_PORT_ENTRY, Link);

        ASSERT(
            PortEntry->State == FnPortStateAllocated || PortEntry->State == FnPortStateActivated);

        if (PortEntry->State == FnPortStateActivated) {
            FRE_ASSERT(NT_SUCCESS(MpDeactivatePort(Adapter, PortEntry)));
        }

        ASSERT(PortEntry->State == FnPortStateAllocated);

        FRE_ASSERT(NT_SUCCESS(MpFreePort(Adapter, PortEntry)));
    }

    RtlReleasePushLockExclusive(&Adapter->PushLock);

    TraceExitSuccess(TRACE_CONTROL);
}
