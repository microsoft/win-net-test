//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include <fnrtl.h>
#include "pooltag.h"

DECLARE_HANDLE(FN_TIMER_HANDLE);

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
FN_TIMER_CALLBACK(
    _In_ VOID *CallbackContext
    );

typedef struct _FN_TIMER {
    FN_TIMER_CALLBACK *Callback;
    VOID *CallbackContext;
    HANDLE ThreadHandle;
    KEVENT CloseEvent;
    LARGE_INTEGER Timeout;
} FN_TIMER;

inline
_IRQL_requires_same_
_Function_class_(KSTART_ROUTINE)
VOID
FnTimerWorker(
    _In_ VOID *Context
    )
{
    FN_TIMER *Timer = Context;
    NTSTATUS Status;

    while ((Status = FnWaitObject(&Timer->CloseEvent, &Timer->Timeout)) == STATUS_TIMEOUT) {
        Timer->Callback(Timer->CallbackContext);
    }

    ASSERT(Status == STATUS_SUCCESS);
}

inline
VOID
FnTimerDestroy(
    FN_TIMER *Timer
    )
{
    if (Timer->ThreadHandle != NULL) {
        ObDereferenceObject(Timer->ThreadHandle);
    }

    ExFreePoolWithTag(Timer, POOLTAG_TIMER);
}

inline
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
FnTimerCreate(
    _Out_ FN_TIMER_HANDLE *Handle,
    _In_ FN_TIMER_CALLBACK *Callback,
    _In_ VOID *CallbackContext,
    _In_ UINT32 IntervalMs
    )
{
    NTSTATUS Status;
    FN_TIMER *Timer;
    HANDLE ThreadHandle = NULL;

    Timer = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Timer), POOLTAG_TIMER);
    if (Timer == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Timer->Callback = Callback;
    Timer->CallbackContext = CallbackContext;
    Timer->Timeout.QuadPart = -1i64 * RTL_MILLISEC_TO_100NANOSEC(IntervalMs);
    KeInitializeEvent(&Timer->CloseEvent, NotificationEvent, FALSE);

    Status =
        PsCreateSystemThread(
            &ThreadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL,
            FnTimerWorker, Timer);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status =
        ObReferenceObjectByHandle(
            ThreadHandle, THREAD_ALL_ACCESS, *PsThreadType, KernelMode, &Timer->ThreadHandle,
            NULL);
    FRE_ASSERT(NT_SUCCESS(Status));

    *Handle = (FN_TIMER_HANDLE)Timer;

Exit:

    if (!NT_SUCCESS(Status)) {
        FnTimerDestroy(Timer);
    }

    if (ThreadHandle != NULL) {
        NT_VERIFY(NT_SUCCESS(ZwClose(ThreadHandle)));
    }

    return Status;
}

inline
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
FnTimerClose(
    _In_ FN_TIMER_HANDLE Handle
    )
{
    FN_TIMER *Timer = (FN_TIMER *)Handle;

    NT_VERIFY(KeSetEvent(&Timer->CloseEvent, IO_NO_INCREMENT, FALSE) == FALSE);
    NT_VERIFY(FnWaitObject(&Timer->ThreadHandle, NULL) == STATUS_SUCCESS);

    FnTimerDestroy(Timer);
}
