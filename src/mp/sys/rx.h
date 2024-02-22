//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

typedef struct _SHARED_RX SHARED_RX;
typedef struct _SHARED_CONTEXT SHARED_CONTEXT;

VOID
SharedRxCleanup(
    _In_ SHARED_RX *Rx
    );

SHARED_RX *
SharedRxCreate(
    _In_ SHARED_CONTEXT *Shared
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
SharedIrpRxEnqueue(
    _In_ SHARED_RX *Rx,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
SharedIrpRxFlush(
    _In_ SHARED_RX *Rx,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    );

extern CONST UINT16 SharedRxNblContextSize;
