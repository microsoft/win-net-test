//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

typedef struct _SERVICE_USER_CONTEXT {
    FILE_OBJECT_HEADER Header;
    KEVENT RequestsAvailableEvent;
} SERVICE_USER_CONTEXT;

NTSTATUS
ServiceIrpCreate(
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp,
    _In_ UCHAR Disposition,
    _In_ VOID *InputBuffer,
    _In_ SIZE_T InputBufferLength
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
ServiceInitialize(
    VOID
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
ServiceUninitialize(
    VOID
    );
