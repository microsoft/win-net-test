//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

#include "requestqueue.tmh"

typedef struct _RQ_CLIENT {
    CLIENT_REQUEST_COMPLETE_FN *RequestComplete;
    EX_RUNDOWN_REF Rundown;
    BOOLEAN IsRegistered;
} RQ_CLIENT;

typedef struct _RQ_SERVICE {
    SERVICE_REQUEST_AVAILABLE_FN *RequestAvailable;
    EX_RUNDOWN_REF Rundown;
    BOOLEAN IsRegistered;
} RQ_SERVICE;

static KSPIN_LOCK Lock;
static LIST_ENTRY Queue;
static RQ_CLIENT Client;
static RQ_SERVICE Service;

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
RqClientPushRequest(
    _In_ UINT64 Id,
    _In_ VOID *Context,
    _In_z_ CHAR *Command
    )
{
    NTSTATUS Status;
    KIRQL OldIrql;
    ISR_REQUEST *Request;
    BOOLEAN WasListEmpty = FALSE;

    Request = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Request), POOLTAG_ISR_REQUEST);
    if (Request == NULL) {
        TraceError(TRACE_CONTROL, "Failed to allocate request");
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Request->Id = Id;
    Request->ClientContext = Context;
    strcpy_s(Request->Command, sizeof(Request->Command), Command);

    KeAcquireSpinLock(&Lock, &OldIrql);

    if (!Service.IsRegistered || !ExAcquireRundownProtection(&Service.Rundown)) {
        TraceError(TRACE_CONTROL, "Failed to acquire ref on service");
        Status = STATUS_DEVICE_NOT_READY;
    } else {
        WasListEmpty = IsListEmpty(&Queue);
        InsertTailList(&Queue, &Request->Link);
        Status = STATUS_SUCCESS;
    }

    KeReleaseSpinLock(&Lock, OldIrql);

    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    if (WasListEmpty) {
        Service.RequestAvailable();
    }

    ExReleaseRundownProtection(&Service.Rundown);

Exit:

    if (!NT_SUCCESS(Status)) {
        if (Request != NULL) {
            ExFreePoolWithTag(Request, POOLTAG_ISR_REQUEST);
        }
    }

    return Status;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
RqServicePopRequest(
    _Out_ ISR_REQUEST **Request
    )
{
    NTSTATUS Status;
    KIRQL OldIrql;

    *Request = NULL;

    KeAcquireSpinLock(&Lock, &OldIrql);

    if (!IsListEmpty(&Queue)) {
        LIST_ENTRY *Entry = RemoveHeadList(&Queue);
        *Request = CONTAINING_RECORD(Entry, ISR_REQUEST, Link);
        Status = STATUS_SUCCESS;
    } else {
        Status = STATUS_NOT_FOUND;
    }

    KeReleaseSpinLock(&Lock, OldIrql);

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
RqServiceCompleteRequest(
    _In_ ISR_REQUEST *Request,
    _In_ INT Result
    )
{
    NTSTATUS Status;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Lock, &OldIrql);

    if (!Client.IsRegistered || !ExAcquireRundownProtection(&Client.Rundown)) {
        TraceError(TRACE_CONTROL, "Failed to acquire ref on client");
        Status = STATUS_DEVICE_NOT_READY;
    } else {
        Status = STATUS_SUCCESS;
    }

    KeReleaseSpinLock(&Lock, OldIrql);

    if (NT_SUCCESS(Status)) {
        Client.RequestComplete(Request->Id, Request->ClientContext, Result);
        ExReleaseRundownProtection(&Client.Rundown);
    }

    ExFreePoolWithTag(Request, POOLTAG_ISR_REQUEST);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
RqClientRegister(
    _In_ CLIENT_REQUEST_COMPLETE_FN *ClientRequestComplete
    )
{
    KIRQL OldIrql;

    ExInitializeRundownProtection(&Client.Rundown);

    KeAcquireSpinLock(&Lock, &OldIrql);
    Client.RequestComplete = ClientRequestComplete;
    Client.IsRegistered = TRUE;
    KeReleaseSpinLock(&Lock, OldIrql);

    return STATUS_SUCCESS;
}

//
// Upon return, no client callbacks will be invoked.
//
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
RqClientDeregister(
    VOID
    )
{
    KIRQL OldIrql;

    ExWaitForRundownProtectionRelease(&Client.Rundown);

    KeAcquireSpinLock(&Lock, &OldIrql);
    Client.IsRegistered = FALSE;
    KeReleaseSpinLock(&Lock, OldIrql);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
RqServiceRegister(
    _In_ SERVICE_REQUEST_AVAILABLE_FN *ServiceRequestAvailable
    )
{
    KIRQL OldIrql;

    ExInitializeRundownProtection(&Service.Rundown);

    KeAcquireSpinLock(&Lock, &OldIrql);
    Service.RequestAvailable = ServiceRequestAvailable;
    Service.IsRegistered = TRUE;
    KeReleaseSpinLock(&Lock, OldIrql);

    return STATUS_SUCCESS;
}

//
// Upon return, no client callbacks will be invoked.
//
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
RqServiceDeregister(
    VOID
    )
{
    KIRQL OldIrql;

    ExWaitForRundownProtectionRelease(&Service.Rundown);

    KeAcquireSpinLock(&Lock, &OldIrql);
    Service.IsRegistered = FALSE;
    KeReleaseSpinLock(&Lock, OldIrql);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
RqInitialize(
    VOID
    )
{
    InitializeListHead(&Queue);
    KeInitializeSpinLock(&Lock);

    Client.IsRegistered = FALSE;
    Service.IsRegistered = FALSE;

    return STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
RqUninitialize(
    VOID
    )
{
    NT_ASSERT(!Client.IsRegistered);
    NT_ASSERT(!Service.IsRegistered);
    NT_ASSERT(IsListEmpty(&Queue));

    // while (IsListEmpty(&Queue)) {
    //     LIST_ENTRY *Entry = RemoveHeadList(&Queue);
    //     ExFreePoolWithTag(Entry, POOLTAG_ISR_REQUEST);
    // }

    return;
}
