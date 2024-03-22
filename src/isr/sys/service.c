//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

#include "service.tmh"

static KSPIN_LOCK Lock;
static IRP *PendingIrp;
static ISR_REQUEST *PendingRequest;
static SERVICE_USER_CONTEXT *ServiceUserContext;
static DRIVER_CANCEL ServiceCancelGet;

static
_Use_decl_annotations_
VOID
ServiceCancelGet(
    DEVICE_OBJECT *DeviceObject,
    IRP *Irp
    )
{
    SERVICE_USER_CONTEXT *UserContext;
    IO_STACK_LOCATION *IrpSp;

    UNREFERENCED_PARAMETER(DeviceObject);

    IoReleaseCancelSpinLock(DISPATCH_LEVEL);

    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    UserContext = IrpSp->FileObject->FsContext;

    TraceInfo(TRACE_CONTROL, "UserContext=%p", UserContext);

    KeAcquireSpinLockAtDpcLevel(&Lock);

    NT_ASSERT(Irp == PendingIrp);
    if (Irp == PendingIrp) {
        PendingIrp = NULL;
    }

    KeReleaseSpinLock(&Lock, Irp->CancelIrql);

    Irp->IoStatus.Status = STATUS_CANCELLED;

    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

static
VOID
ServiceInitializeGetIrpOutput(
    _In_ IRP *Irp,
    _In_ const ISR_REQUEST *Request
    )
{
    ISR_GET_OUTPUT *Output = Irp->AssociatedIrp.SystemBuffer;

    RtlZeroMemory(Output, sizeof(*Output));
    Output->Id = Request->Id;
    strcpy_s(Output->Command, sizeof(Request->Command), Request->Command);
    Irp->IoStatus.Information = sizeof(*Output);
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
ServiceRequestAvailable(
    VOID
    )
{
    NTSTATUS Status;
    KIRQL OldIrql;
    IRP *Irp = NULL;
    ISR_REQUEST *Request;

    KeAcquireSpinLock(&Lock, &OldIrql);

    if (PendingIrp != NULL) {
        Status = RqServicePopRequest(&Request);
        NT_ASSERT(NT_SUCCESS(Status));
        if (NT_SUCCESS(Status)) {
            Irp = PendingIrp;
            PendingIrp = NULL;

            ServiceInitializeGetIrpOutput(Irp, Request);
            PendingRequest = Request;

            if (IoSetCancelRoutine(Irp, NULL) == NULL) {
                Irp = NULL;
            }
        }
    }

    KeReleaseSpinLock(&Lock, OldIrql);

    if (Irp != NULL) {
        TraceInfo(TRACE_CONTROL, "Completing service get Irp=%p", Irp);

        Irp->IoStatus.Status = STATUS_SUCCESS;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
ServiceIrpGet(
    SERVICE_USER_CONTEXT *UserContext,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    KIRQL OldIrql = 0;
    SIZE_T OutputBufferLength;
    ISR_GET_OUTPUT *Output;
    ISR_REQUEST *Request;
    BOOLEAN LockAcquired = FALSE;

    TraceEnter(TRACE_CONTROL, "UserContext=%p", UserContext);

    OutputBufferLength = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;

    if (OutputBufferLength < sizeof(*Output)) {
        TraceError(
            TRACE_CONTROL, "Invalid input OututBufferLength=%Iu",
            OutputBufferLength);
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    Output = Irp->AssociatedIrp.SystemBuffer;

    KeAcquireSpinLock(&Lock, &OldIrql);
    LockAcquired = TRUE;

    if (PendingIrp != NULL) {
        TraceError(TRACE_CONTROL, "Parallel requests not supported");
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    if (PendingRequest != NULL) {
        TraceError(
            TRACE_CONTROL, "Getting request while one has not been completed not supported");
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    Status = RqServicePopRequest(&Request);
    if (NT_SUCCESS(Status)) {
        ServiceInitializeGetIrpOutput(Irp, Request);
        PendingRequest = Request;
        Status = STATUS_SUCCESS;
        TraceInfo(
            TRACE_CONTROL, "Pending service get UserContext=%p Irp=%p",
            UserContext, Irp);
    } else if (Status == STATUS_NOT_FOUND) {
        //
        // Pend the IRP.
        //
        IoMarkIrpPending(Irp);
        PendingIrp = Irp;
        Status = STATUS_PENDING;

        //
        // Set up a cancel routine.
        //
        IoSetCancelRoutine(Irp, ServiceCancelGet);
        if (Irp->Cancel) {
            if (IoSetCancelRoutine(Irp, NULL) != NULL) {
                //
                // The cancellation routine will not run; cancel the IRP here
                // and bail.
                //
                PendingIrp = NULL;
                Status = STATUS_CANCELLED;
            } else {
                //
                // The cancellation routine will run; bail.
                //
            }
            goto Exit;
        }
    } else {
        TraceError(
            TRACE_CONTROL, "RqGetRequest failed Status=%!STATUS!", Status);
        goto Exit;
    }

Exit:

    if (LockAcquired) {
        KeReleaseSpinLock(&Lock, OldIrql);
    }

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
    NTSTATUS Status;
    KIRQL OldIrql;
    ISR_POST_INPUT *Input;
    SIZE_T InputBufferLength;
    ISR_REQUEST *Request = NULL;

    TraceEnter(TRACE_CONTROL, "UserContext=%p", UserContext);

    InputBufferLength = IrpSp->Parameters.DeviceIoControl.InputBufferLength;

    if (InputBufferLength < sizeof(*Input)) {
        TraceError(
            TRACE_CONTROL, "Invalid input InputBufferLength=%Iu",
            InputBufferLength);
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    Input = Irp->AssociatedIrp.SystemBuffer;

    KeAcquireSpinLock(&Lock, &OldIrql);

    if (Input->Id == PendingRequest->Id) {
        Request = PendingRequest;
        PendingRequest = NULL;
    }

    KeReleaseSpinLock(&Lock, OldIrql);

    if (Request == NULL) {
        TraceError(
            TRACE_CONTROL, "Request not found Id=%llu",
            Input->Id);
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

    RqServiceCompleteRequest(Request, Input->Result);
    Status = STATUS_SUCCESS;

    TraceInfo(
        TRACE_CONTROL, "Completing service post Irp=%p, Id=%llu Result=%d",
        Irp, Input->Id, Input->Result);

Exit:

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
    NTSTATUS Status;
    SERVICE_USER_CONTEXT *UserContext = IrpSp->FileObject->FsContext;

    switch (IrpSp->Parameters.DeviceIoControl.IoControlCode) {
    case ISR_IOCTL_INVOKE_SYSTEM_GET:
        Status = ServiceIrpGet(UserContext, Irp, IrpSp);
        break;

    case ISR_IOCTL_INVOKE_SYSTEM_POST:
        Status = ServiceIrpPost(UserContext, Irp, IrpSp);
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
    SERVICE_USER_CONTEXT *UserContext =
        (SERVICE_USER_CONTEXT *)IrpSp->FileObject->FsContext;

    UNREFERENCED_PARAMETER(Irp);

    RqServiceDeregister();

    InterlockedCompareExchangePointer(
        (PVOID volatile *)&ServiceUserContext, NULL, UserContext);

    TraceInfo(TRACE_CONTROL, "Service closed UserContext=%p", UserContext);

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

    UserContext =
        ExAllocatePoolZero(
            NonPagedPoolNx, sizeof(*UserContext), POOLTAG_ISR_SERVICE);
    if (UserContext == NULL) {
        TraceError(TRACE_CONTROL, "Failed to allocate user context");
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    if (InterlockedCompareExchangePointer(
            (PVOID volatile *)&ServiceUserContext, UserContext, NULL) != NULL) {
        TraceError(TRACE_CONTROL, "Multiple clients not supported");
        Status = STATUS_TOO_MANY_SESSIONS;
        goto Exit;
    }

    Status = RqServiceRegister(ServiceRequestAvailable);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    UserContext->Header.ObjectType = ISR_FILE_TYPE_SERVICE;
    UserContext->Header.Dispatch = &ServiceFileDispatch;

    IrpSp->FileObject->FsContext = UserContext;
    Status = STATUS_SUCCESS;

    TraceInfo(TRACE_CONTROL, "Service created UserContext=%p", UserContext);

Exit:

    if (!NT_SUCCESS(Status)) {
        if (UserContext != NULL) {
            InterlockedCompareExchangePointer(
                (PVOID volatile *)&ServiceUserContext, NULL, UserContext);
            ServiceCleanup(UserContext);
        }
    }

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
ServiceInitialize(
    VOID
    )
{
    KeInitializeSpinLock(&Lock);
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
ServiceUninitialize(
    VOID
    )
{
    NT_ASSERT(PendingIrp == NULL);
    NT_ASSERT(PendingRequest == NULL);
}
