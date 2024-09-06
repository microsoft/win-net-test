//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
MpPortCleanup(
    _In_ ADAPTER_CONTEXT *Adapter
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
MpIrpAllocatePort(
    _In_ EXCLUSIVE_USER_CONTEXT *UserContext,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
MpIrpFreePort(
    _In_ EXCLUSIVE_USER_CONTEXT *UserContext,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
MpIrpActivatePort(
    _In_ EXCLUSIVE_USER_CONTEXT *UserContext,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
MpIrpDeactivatePort(
    _In_ EXCLUSIVE_USER_CONTEXT *UserContext,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    );
