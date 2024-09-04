//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

FILTER_OID_REQUEST FilterOidRequest;
FILTER_OID_REQUEST_COMPLETE FilterOidRequestComplete;
FILTER_DIRECT_OID_REQUEST FilterDirectOidRequest;
FILTER_DIRECT_OID_REQUEST_COMPLETE FilterDirectOidRequestComplete;

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
OidIrpSubmitRequest(
    _In_ DEFAULT_CONTEXT *Default,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    );
