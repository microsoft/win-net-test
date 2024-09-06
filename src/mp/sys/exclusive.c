//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
ExclusiveWatchdogTimeout(
    _In_ ADAPTER_CONTEXT *Adapter
    )
{
    if (MpOidWatchdogIsExpired(Adapter)) {
        MpWatchdogFailure(Adapter, "OID");
        MpOidClearFilterAndFlush(Adapter);
    }
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
ExclusiveIrpDeviceIoControl(
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    EXCLUSIVE_USER_CONTEXT *UserContext = IrpSp->FileObject->FsContext;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Irp);

    switch (IrpSp->Parameters.DeviceIoControl.IoControlCode) {
    case FNMP_IOCTL_OID_FILTER:
        Status = MpIrpOidSetFilter(UserContext, Irp, IrpSp);
        break;

    case FNMP_IOCTL_OID_GET_REQUEST:
        Status = MpIrpOidGetRequest(UserContext, Irp, IrpSp);
        break;

    case FNMP_IOCTL_OID_COMPLETE_REQUEST:
        Status = MpIrpOidCompleteRequest(UserContext, Irp, IrpSp);
        break;

    case FNMP_IOCTL_MINIPORT_ALLOCATE_PORT:
        Status = MpIrpAllocatePort(UserContext, Irp, IrpSp);
        break;

    case FNMP_IOCTL_MINIPORT_FREE_PORT:
        Status = MpIrpFreePort(UserContext, Irp, IrpSp);
        break;

    case FNMP_IOCTL_MINIPORT_ACTIVATE_PORT:
        Status = MpIrpActivatePort(UserContext, Irp, IrpSp);
        break;

    case FNMP_IOCTL_MINIPORT_DEACTIVATE_PORT:
        Status = MpIrpDeactivatePort(UserContext, Irp, IrpSp);
        break;

    default:
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

Exit:

    return Status;
}

static
VOID
ExclusiveCleanup(
    _In_ EXCLUSIVE_USER_CONTEXT *UserContext
    )
{
    KIRQL OldIrql;

    if (UserContext->Adapter != NULL) {
        MpPortCleanup(UserContext->Adapter);

        if (UserContext->SetOidFilter) {
            MpOidClearFilterAndFlush(UserContext->Adapter);
        }

        KeAcquireSpinLock(&UserContext->Adapter->Lock, &OldIrql);
        if (UserContext->Adapter->UserContext == UserContext) {
            UserContext->Adapter->UserContext = NULL;
        }
        KeReleaseSpinLock(&UserContext->Adapter->Lock, OldIrql);

        MpDereferenceAdapter(UserContext->Adapter);
    }

    ExFreePoolWithTag(UserContext, POOLTAG_MP_EXCLUSIVE);
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
ExclusiveIrpClose(
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    EXCLUSIVE_USER_CONTEXT *UserContext = (EXCLUSIVE_USER_CONTEXT *)IrpSp->FileObject->FsContext;

    UNREFERENCED_PARAMETER(Irp);

    ASSERT(UserContext == UserContext->Adapter->UserContext);

    ExclusiveCleanup(UserContext);

    return STATUS_SUCCESS;
}

static CONST FILE_DISPATCH ExclusiveFileDispatch = {
    .IoControl = ExclusiveIrpDeviceIoControl,
    .Close = ExclusiveIrpClose,
};

NTSTATUS
ExclusiveIrpCreate(
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp,
    _In_ UCHAR Disposition,
    _In_ VOID *InputBuffer,
    _In_ SIZE_T InputBufferLength
    )
{
    NTSTATUS Status;
    EXCLUSIVE_USER_CONTEXT *UserContext = NULL;
    FNMP_OPEN_EXCLUSIVE *OpenExclusive;
    UINT32 IfIndex;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(Irp);
    UNREFERENCED_PARAMETER(Disposition);

    if (InputBufferLength < sizeof(*OpenExclusive)) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }
    OpenExclusive = InputBuffer;
    IfIndex = OpenExclusive->IfIndex;

    UserContext = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*UserContext), POOLTAG_MP_EXCLUSIVE);
    if (UserContext == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    UserContext->Header.ObjectType = FNMP_FILE_TYPE_EXCLUSIVE;
    UserContext->Header.Dispatch = &ExclusiveFileDispatch;

    UserContext->Adapter = MpFindAdapter(IfIndex);
    if (UserContext->Adapter == NULL) {
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

    KeAcquireSpinLock(&UserContext->Adapter->Lock, &OldIrql);

    if (UserContext->Adapter->UserContext != NULL) {
        Status = STATUS_DUPLICATE_OBJECTID;
        KeReleaseSpinLock(&UserContext->Adapter->Lock, OldIrql);
        goto Exit;
    }

    UserContext->Adapter->UserContext = UserContext;

    KeReleaseSpinLock(&UserContext->Adapter->Lock, OldIrql);

    IrpSp->FileObject->FsContext = UserContext;
    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        if (UserContext != NULL) {
            ExclusiveCleanup(UserContext);
        }
    }

    return Status;
}
