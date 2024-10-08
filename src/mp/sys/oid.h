//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

extern CONST NDIS_OID MpSupportedOidArray[];
extern CONST UINT32 MpSupportedOidArraySize;

MINIPORT_CANCEL_OID_REQUEST MiniportCancelRequestHandler;
MINIPORT_OID_REQUEST MiniportRequestHandler;
MINIPORT_DIRECT_OID_REQUEST MiniportDirectRequestHandler;
MINIPORT_CANCEL_DIRECT_OID_REQUEST MiniportCancelDirectRequestHandler;

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
MpIrpOidSetFilter(
    _In_ EXCLUSIVE_USER_CONTEXT *UserContext,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
MpIrpOidGetRequest(
    _In_ EXCLUSIVE_USER_CONTEXT *UserContext,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
MpIrpOidCompleteRequest(
    _In_ EXCLUSIVE_USER_CONTEXT *UserContext,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
MpOidClearFilterAndFlush(
    _In_ ADAPTER_CONTEXT *Adapter
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
BOOLEAN
MpOidWatchdogIsExpired(
    _In_ ADAPTER_CONTEXT *Adapter
    );
