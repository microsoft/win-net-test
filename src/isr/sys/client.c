//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

#include "client.tmh"

static CLIENT_USER_CONTEXT *ClientUserContext;
static UINT64 UniqueId;

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
ClientRequestComplete(
    _In_ UINT64 Id,
    _In_ VOID *Context,
    _In_ INT Result
    )
{
    IRP *Irp = (IRP *)Context;
    INT *Output = (INT *)Irp->AssociatedIrp.SystemBuffer;

    UNREFERENCED_PARAMETER(Id);

    TraceInfo(TRACE_CONTROL, "Completing client request Irp=%p", Irp);

    *Output = Result;
    Irp->IoStatus.Information = sizeof(*Output);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
ClientIrpSubmit(
    CLIENT_USER_CONTEXT *UserContext,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    SIZE_T InputBufferLength;
    SIZE_T OutputBufferLength;

    TraceEnter(TRACE_CONTROL, "UserContext=%p Irp=%p", UserContext, Irp);

    InputBufferLength = IrpSp->Parameters.DeviceIoControl.InputBufferLength;
    OutputBufferLength = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;

    if (InputBufferLength > ISR_MAX_COMMAND_LENGTH ||
        OutputBufferLength < sizeof(INT)) {
        TraceError(
            TRACE_CONTROL, "Invalid input InputBufferLength=%Iu OututBufferLength=%Iu",
            InputBufferLength, OutputBufferLength);
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    if (((CHAR *)Irp->AssociatedIrp.SystemBuffer)[InputBufferLength - 1] != '\0') {
        TraceError(TRACE_CONTROL, "Invalid input NULL termination");
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    Status =
        RqClientPushRequest(
            InterlockedIncrement64((LONG64 *)&UniqueId), Irp,
            Irp->AssociatedIrp.SystemBuffer);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    //
    // TODO: Fix race condition where request is completed before we mark it pending.
    //
    IoMarkIrpPending(Irp);
    Status = STATUS_PENDING;

    TraceInfo(
        TRACE_CONTROL, "Pending client request UserContext=%p Irp=%p",
        UserContext, Irp);

Exit:

    TraceExitStatus(TRACE_CONTROL);

    return Status;
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
ClientIrpDeviceIoControl(
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    CLIENT_USER_CONTEXT *UserContext = IrpSp->FileObject->FsContext;

    switch (IrpSp->Parameters.DeviceIoControl.IoControlCode) {
    case ISR_IOCTL_INVOKE_SYSTEM_SUBMIT:
        Status = ClientIrpSubmit(UserContext, Irp, IrpSp);
        break;

    default:
        TraceError(
            TRACE_CONTROL, "Invalid IOCTL Code=%u",
            IrpSp->Parameters.DeviceIoControl.IoControlCode);
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

Exit:

    return Status;
}

static
VOID
ClientCleanup(
    _In_ CLIENT_USER_CONTEXT *UserContext
    )
{
    ExFreePoolWithTag(UserContext, POOLTAG_ISR_CLIENT);
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
ClientIrpClose(
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    CLIENT_USER_CONTEXT *UserContext = IrpSp->FileObject->FsContext;

    UNREFERENCED_PARAMETER(Irp);

    RqClientDeregister();

    InterlockedCompareExchangePointer(
        (PVOID volatile *)&ClientUserContext, NULL, UserContext);

    TraceInfo(TRACE_CONTROL, "Client closed UserContext=%p", UserContext);

    ClientCleanup(UserContext);

    return STATUS_SUCCESS;
}

static CONST FILE_DISPATCH ClientFileDispatch = {
    .IoControl = ClientIrpDeviceIoControl,
    .Close = ClientIrpClose,
};

NTSTATUS
ClientIrpCreate(
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp,
    _In_ UCHAR Disposition,
    _In_ VOID *InputBuffer,
    _In_ SIZE_T InputBufferLength
    )
{
    NTSTATUS Status;
    CLIENT_USER_CONTEXT *UserContext = NULL;

    UNREFERENCED_PARAMETER(Irp);
    UNREFERENCED_PARAMETER(Disposition);
    UNREFERENCED_PARAMETER(InputBuffer);
    UNREFERENCED_PARAMETER(InputBufferLength);

    UserContext =
        ExAllocatePoolZero(
            NonPagedPoolNx, sizeof(*UserContext), POOLTAG_ISR_CLIENT);
    if (UserContext == NULL) {
        TraceError(TRACE_CONTROL, "Failed to allocate user context");
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    if (InterlockedCompareExchangePointer(
            (PVOID volatile *)&ClientUserContext, UserContext, NULL) != NULL) {
        TraceError(TRACE_CONTROL, "Multiple clients not supported");
        Status = STATUS_TOO_MANY_SESSIONS;
        goto Exit;
    }

    Status = RqClientRegister(ClientRequestComplete);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    UserContext->Header.ObjectType = ISR_FILE_TYPE_CLIENT;
    UserContext->Header.Dispatch = &ClientFileDispatch;

    IrpSp->FileObject->FsContext = UserContext;
    Status = STATUS_SUCCESS;

    TraceInfo(TRACE_CONTROL, "Client created UserContext=%p", UserContext);

Exit:

    if (!NT_SUCCESS(Status)) {
        if (UserContext != NULL) {
            InterlockedCompareExchangePointer(
                (PVOID volatile *)&ClientUserContext, NULL, UserContext);
            ClientCleanup(UserContext);
        }
    }

    return Status;
}
