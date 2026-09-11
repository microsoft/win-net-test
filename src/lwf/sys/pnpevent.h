//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

FILTER_NET_PNP_EVENT FilterNetPnPEvent;

typedef struct _DEFAULT_PNP_EVENT DEFAULT_PNP_EVENT;

VOID
PnpEventCleanup(
    _In_ DEFAULT_PNP_EVENT *PnpEventFile
    );

DEFAULT_PNP_EVENT *
PnpEventCreate(
    _In_ DEFAULT_CONTEXT *Default
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
PnpEventIrpFilter(
    _In_ DEFAULT_PNP_EVENT *PnpEventFile,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
PnpEventIrpGetNotification(
    _In_ DEFAULT_PNP_EVENT *PnpEventFile,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
PnpEventIrpComplete(
    _In_ DEFAULT_PNP_EVENT *PnpEventFile,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    );
