//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

#include <fniotypes.h>

#ifndef FNLWFAPI
#define FNLWFAPI __declspec(dllimport)
#endif

FNLWFAPI
HRESULT
FnLwfOpenDefault(
    _In_ UINT32 IfIndex,
    _Out_ HANDLE *Handle
    );

FNLWFAPI
HRESULT
FnLwfTxEnqueue(
    _In_ HANDLE Handle,
    _In_ DATA_FRAME *Frame
    );

FNLWFAPI
HRESULT
FnLwfTxFlush(
    _In_ HANDLE Handle,
    _In_opt_ DATA_FLUSH_OPTIONS *Options
    );

FNLWFAPI
HRESULT
FnLwfRxFilter(
    _In_ HANDLE Handle,
    _In_ const VOID *Pattern,
    _In_ const VOID *Mask,
    _In_ UINT32 Length
    );

FNLWFAPI
HRESULT
FnLwfRxGetFrame(
    _In_ HANDLE Handle,
    _In_ UINT32 FrameIndex,
    _Inout_ UINT32 *FrameBufferLength,
    _Out_opt_ DATA_FRAME *Frame
    );

FNLWFAPI
HRESULT
FnLwfRxDequeueFrame(
    _In_ HANDLE Handle,
    _In_ UINT32 FrameIndex
    );

FNLWFAPI
HRESULT
FnLwfRxFlush(
    _In_ HANDLE Handle
    );

FNLWFAPI
HRESULT
FnLwfOidSubmitRequest(
    _In_ HANDLE Handle,
    _In_ OID_KEY Key,
    _Inout_ UINT32 *InformationBufferLength,
    _Inout_opt_ VOID *InformationBuffer
    );

FNLWFAPI
HRESULT
FnLwfStatusSetFilter(
    _In_ HANDLE Handle,
    _In_ NDIS_STATUS StatusCode,
    _In_ BOOLEAN BlockIndications,
    _In_ BOOLEAN QueueIndications
    );

FNLWFAPI
HRESULT
FnLwfStatusGetIndication(
    _In_ HANDLE Handle,
    _Inout_ UINT32 *StatusBufferLength,
    _Out_writes_bytes_opt_(*StatusBufferLength) VOID *StatusBuffer
    );

FNLWFAPI
HRESULT
FnLwfDatapathGetState(
    _In_ HANDLE Handle,
    BOOLEAN *IsDatapathActive
    );

EXTERN_C_END
