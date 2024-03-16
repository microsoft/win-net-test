//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

#include "service.tmh"

static
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
ServiceIrpGet(
    SERVICE_USER_CONTEXT *UserContext,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status = STATUS_NOT_IMPLEMENTED;

    UNREFERENCED_PARAMETER(Irp);
    UNREFERENCED_PARAMETER(IrpSp);

    TraceEnter(TRACE_CONTROL, "UserContext=%p", UserContext);

    TraceExitStatus(TRACE_CONTROL);

    return Status;
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
ServiceIrpPost(
    SERVICE_USER_CONTEXT *UserContext,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status = STATUS_NOT_IMPLEMENTED;

    UNREFERENCED_PARAMETER(Irp);
    UNREFERENCED_PARAMETER(IrpSp);

    TraceEnter(TRACE_CONTROL, "UserContext=%p", UserContext);

    TraceExitStatus(TRACE_CONTROL);

    return Status;
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
ServiceIrpDeviceIoControl(
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    SERVICE_USER_CONTEXT *UserContext = IrpSp->FileObject->FsContext;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Irp);

    switch (IrpSp->Parameters.DeviceIoControl.IoControlCode) {
    case ISR_IOCTL_INVOKE_SYSTEM_GET:
        Status = ServiceIrpGet(UserContext, Irp, IrpSp);
        break;

    case ISR_IOCTL_INVOKE_SYSTEM_POST:
        Status = ServiceIrpPost(UserContext, Irp, IrpSp);
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
ServiceCleanup(
    _In_ SERVICE_USER_CONTEXT *UserContext
    )
{
    ExFreePoolWithTag(UserContext, POOLTAG_ISR_SERVICE);
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
ServiceIrpClose(
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    SERVICE_USER_CONTEXT *UserContext = (SERVICE_USER_CONTEXT *)IrpSp->FileObject->FsContext;

    UNREFERENCED_PARAMETER(Irp);

    ServiceCleanup(UserContext);

    return STATUS_SUCCESS;
}

static CONST FILE_DISPATCH ServiceFileDispatch = {
    .IoControl = ServiceIrpDeviceIoControl,
    .Close = ServiceIrpClose,
};

NTSTATUS
ServiceIrpCreate(
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp,
    _In_ UCHAR Disposition,
    _In_ VOID *InputBuffer,
    _In_ SIZE_T InputBufferLength
    )
{
    NTSTATUS Status;
    SERVICE_USER_CONTEXT *UserContext = NULL;

    UNREFERENCED_PARAMETER(Irp);
    UNREFERENCED_PARAMETER(Disposition);
    UNREFERENCED_PARAMETER(InputBuffer);
    UNREFERENCED_PARAMETER(InputBufferLength);

    UserContext = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*UserContext), POOLTAG_ISR_SERVICE);
    if (UserContext == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    UserContext->Header.ObjectType = ISR_FILE_TYPE_SERVICE;
    UserContext->Header.Dispatch = &ServiceFileDispatch;

    IrpSp->FileObject->FsContext = UserContext;
    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        if (UserContext != NULL) {
            ServiceCleanup(UserContext);
        }
    }

    return Status;
}
