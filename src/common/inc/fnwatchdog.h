//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include "pooltag.h"

DECLARE_HANDLE(FN_WATCHDOG_HANDLE);

typedef
VOID
FN_WATCHDOG_CALLBACK(
    _In_ VOID *CallbackContext
    );

typedef struct _FN_WATCHDOG {
    FN_WATCHDOG_CALLBACK *Callback;
    VOID *CallbackContext;
    HANDLE ThreadHandle;
    KEVENT CloseEvent;
    LARGE_INTEGER Timeout;
    UINT32 IntervalMs;
} FN_WATCHDOG;

inline
_IRQL_requires_same_
_Function_class_(KSTART_ROUTINE)
VOID
FnWatchdogWorker(
    _In_ VOID *Context
    )
{
    FN_WATCHDOG *Watchdog = Context;
    NTSTATUS Status;

    do {
        Watchdog->Timeout.QuadPart += RTL_MILLISEC_TO_100NANOSEC(Watchdog->IntervalMs);

        Status =
            KeWaitForSingleObject(
                &Watchdog->CloseEvent, Executive, KernelMode, FALSE, &Watchdog->Timeout);

        Watchdog->Callback(Watchdog->CallbackContext);
    } while (Status == STATUS_TIMEOUT);
}

inline
VOID
FnWatchdogDestroy(
    FN_WATCHDOG *Watchdog
    )
{
    if (Watchdog->ThreadHandle != NULL) {
        ObDereferenceObject(Watchdog->ThreadHandle);
    }

    ExFreePoolWithTag(Watchdog, POOLTAG_WATCHDOG);
}

inline
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
FnWatchdogCreate(
    _Out_ FN_WATCHDOG_HANDLE *Handle,
    _In_ FN_WATCHDOG_CALLBACK *Callback,
    _In_ VOID *CallbackContext,
    _In_ UINT32 IntervalMs
    )
{
    NTSTATUS Status;
    FN_WATCHDOG *Watchdog;
    HANDLE ThreadHandle = NULL;

    Watchdog = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Watchdog), POOLTAG_WATCHDOG);
    if (Watchdog == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Watchdog->Callback = Callback;
    Watchdog->CallbackContext = CallbackContext;
    Watchdog->IntervalMs = IntervalMs;
    KeInitializeEvent(&Watchdog->CloseEvent, NotificationEvent, FALSE);
    KeQuerySystemTimePrecise(&Watchdog->Timeout);

    Status =
        PsCreateSystemThread(
            &ThreadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL,
            FnWatchdogWorker, Watchdog);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status =
        ObReferenceObjectByHandle(
            ThreadHandle, THREAD_ALL_ACCESS, *PsThreadType, KernelMode, &Watchdog->ThreadHandle,
            NULL);
    FRE_ASSERT(NT_SUCCESS(Status));

    *Handle = (FN_WATCHDOG_HANDLE)Watchdog;

Exit:

    if (!NT_SUCCESS(Status)) {
        FnWatchdogDestroy(Watchdog);
    }

    if (ThreadHandle != NULL) {
        NT_VERIFY(NT_SUCCESS(ZwClose(ThreadHandle)));
    }

    return Status;
}

inline
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
FnWatchdogClose(
    _In_ FN_WATCHDOG_HANDLE Handle
    )
{
    FN_WATCHDOG *Watchdog = (FN_WATCHDOG *)Handle;

    NT_VERIFY(KeSetEvent(&Watchdog->CloseEvent, IO_NO_INCREMENT, FALSE) == FALSE);
    NT_VERIFY(
        KeWaitForSingleObject(Watchdog->ThreadHandle, Executive, KernelMode, FALSE, NULL) ==
            STATUS_SUCCESS);

    FnWatchdogDestroy(Watchdog);
}
