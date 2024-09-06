//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "port.tmh"

typedef enum _FN_PORT_STATE {
    FnPortStateAllocated,
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

    RtlZeroMemory(&PortCharacteristics, sizeof(PortCharacteristics));
    PortCharacteristics.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    PortCharacteristics.Header.Revision = NDIS_PORT_CHARACTERISTICS_REVISION_1;
    PortCharacteristics.Header.Size = NDIS_SIZEOF_PORT_CHARACTERISTICS_REVISION_1;
    PortCharacteristics.Type = NdisPortTypeUndefined;
    PortCharacteristics.Direction = NET_IF_DIRECTION_SENDRECEIVE;
    PortCharacteristics.SendControlState = NdisPortControlStateUncontrolled;
    PortCharacteristics.RcvControlState = NdisPortControlStateUncontrolled;
    PortCharacteristics.SendAuthorizationState = NdisPortAuthorizationUnknown;
    PortCharacteristics.RcvAuthorizationState = NdisPortAuthorizationUnknown;
    PortCharacteristics.XmitLinkSpeed = MpGlobalContext.XmitLinkSpeed;
    PortCharacteristics.RcvLinkSpeed = MpGlobalContext.RecvLinkSpeed;
    PortCharacteristics.MediaConnectState = MediaConnectStateConnected;

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

    if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(PortEntry->Number)) {
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
VOID
MpPortCleanup(
    _In_ ADAPTER_CONTEXT *Adapter
    )
{
    TraceEnter(TRACE_CONTROL, "Adapter=%p", Adapter);

    RtlAcquirePushLockExclusive(&Adapter->PushLock);

    while (!IsListEmpty(&Adapter->PortList)) {
        FN_PORT_ENTRY *PortEntry = CONTAINING_RECORD(Adapter->PortList.Flink, FN_PORT_ENTRY, Link);

        ASSERT(PortEntry->State == FnPortStateAllocated);

        FRE_ASSERT(MpFreePort(Adapter, PortEntry) == NDIS_STATUS_SUCCESS);
    }

    RtlReleasePushLockExclusive(&Adapter->PushLock);

    TraceExitSuccess(TRACE_CONTROL);
}
