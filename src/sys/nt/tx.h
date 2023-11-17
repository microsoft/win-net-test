//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

typedef struct _SHARED_TX SHARED_TX;

VOID
SharedTxCleanup(
    _In_ SHARED_TX *Tx
    );

SHARED_TX *
SharedTxCreate(
    _In_ SHARED_CONTEXT *Shared
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
SharedIrpTxFilter(
    _In_ SHARED_TX *Tx,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
SharedIrpTxGetFrame(
    _In_ SHARED_TX *Tx,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
SharedIrpTxDequeueFrame(
    _In_ SHARED_TX *Tx,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
SharedIrpTxFlush(
    _In_ SHARED_TX *Tx,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    );
