//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

MINIPORT_RESTART MiniportRestartHandler;
MINIPORT_PAUSE MiniportPauseHandler;
MINIPORT_SEND_NET_BUFFER_LISTS MpSendNetBufferLists;
MINIPORT_CANCEL_SEND MiniportCancelSendHandler;
MINIPORT_RETURN_NET_BUFFER_LISTS MpReturnNetBufferLists;

FILE_CREATE_ROUTINE SharedIrpCreate;

typedef struct _SHARED_RX SHARED_RX;
typedef struct _SHARED_TX SHARED_TX;

typedef struct _ADAPTER_SHARED {
    ADAPTER_CONTEXT *Adapter;
    EX_RUNDOWN_REF NblRundown;
    NDIS_HANDLE NblPool;

    KSPIN_LOCK Lock;
    LIST_ENTRY TxFilterList;
} ADAPTER_SHARED;

typedef struct _SHARED_CONTEXT {
    FILE_OBJECT_HEADER Header;
    KSPIN_LOCK Lock;
    ADAPTER_CONTEXT *Adapter;
    SHARED_RX *Rx;
    SHARED_TX *Tx;
} SHARED_CONTEXT;

ADAPTER_SHARED *
SharedAdapterCreate(
    _In_ ADAPTER_CONTEXT *Adapter
    );

VOID
SharedAdapterCleanup(
    _In_ ADAPTER_SHARED *AdapterShared
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
SharedWatchdogTimeout(
    _In_ ADAPTER_SHARED *AdapterShared
    );
