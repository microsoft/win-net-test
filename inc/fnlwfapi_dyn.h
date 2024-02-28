//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

#include <fniotypes.h>

typedef
HRESULT
FNLWF_OPEN_DEFAULT(
    _In_ UINT32 IfIndex,
    _Out_ HANDLE *Handle
    );

typedef
HRESULT
FNLWF_TX_ENQUEUE(
    _In_ HANDLE Handle,
    _In_ DATA_FRAME *Frame
    );

typedef
HRESULT
FNLWF_TX_FLUSH(
    _In_ HANDLE Handle,
    _In_opt_ DATA_FLUSH_OPTIONS *Options
    );

typedef
HRESULT
FNLWF_RX_FILTER(
    _In_ HANDLE Handle,
    _In_ const VOID *Pattern,
    _In_ const VOID *Mask,
    _In_ UINT32 Length
    );

typedef
HRESULT
FNLWF_RX_GET_FRAME(
    _In_ HANDLE Handle,
    _In_ UINT32 FrameIndex,
    _Inout_ UINT32 *FrameBufferLength,
    _Out_opt_ DATA_FRAME *Frame
    );

typedef
HRESULT
FNLWF_RX_DEQUEUE_FRAME(
    _In_ HANDLE Handle,
    _In_ UINT32 FrameIndex
    );

typedef
HRESULT
FNLWF_RX_FLUSH(
    _In_ HANDLE Handle
    );

typedef
HRESULT
FNLWF_OID_SUBMIT_REQUEST(
    _In_ HANDLE Handle,
    _In_ OID_KEY Key,
    _Inout_ UINT32 *InformationBufferLength,
    _Inout_opt_ VOID *InformationBuffer
    );

typedef
HRESULT
FNLWF_STATUS_SET_FILTER(
    _In_ HANDLE Handle,
    _In_ NDIS_STATUS StatusCode,
    _In_ BOOLEAN BlockIndications,
    _In_ BOOLEAN QueueIndications
    );

typedef
HRESULT
FNLWF_STATUS_GET_INDICATION(
    _In_ HANDLE Handle,
    _Inout_ UINT32 *StatusBufferLength,
    _Out_writes_bytes_opt_(*StatusBufferLength) VOID *StatusBuffer
    );

typedef
HRESULT
FNLWF_DATAPATH_GET_STATE(
    _In_ HANDLE Handle,
    BOOLEAN *IsDatapathActive
    );

typedef struct _FNLWF_LOAD_CONTEXT *FNLWF_LOAD_API_CONTEXT;

extern FNLWF_LOAD_API_CONTEXT FnLwfLoadApiContext;

#define LOAD_FN(TYPE, Name) \
    static TYPE *Fn; \
    if (Fn == NULL) { \
        Fn = (TYPE *)GetProcAddress((HMODULE)FnLwfLoadApiContext, Name); \
    } \
    if (Fn == NULL) { \
        return E_NOINTERFACE; \
    }

inline
HRESULT
FnLwfOpenDefault(
    _In_ UINT32 IfIndex,
    _Out_ HANDLE *Handle
    )
{
    LOAD_FN(FNLWF_OPEN_DEFAULT, __FUNCTION__);
    return Fn(IfIndex, Handle);
}

inline
HRESULT
FnLwfTxEnqueue(
    _In_ HANDLE Handle,
    _In_ DATA_FRAME *Frame
    )
{
    LOAD_FN(FNLWF_TX_ENQUEUE, __FUNCTION__);
    return Fn(Handle, Frame);
}

inline
HRESULT
FnLwfTxFlush(
    _In_ HANDLE Handle,
    _In_opt_ DATA_FLUSH_OPTIONS *Options
    )
{
    LOAD_FN(FNLWF_TX_FLUSH, __FUNCTION__);
     return Fn(Handle, Options);
}

inline
HRESULT
FnLwfRxFilter(
    _In_ HANDLE Handle,
    _In_ const VOID *Pattern,
    _In_ const VOID *Mask,
    _In_ UINT32 Length
    )
{
    LOAD_FN(FNLWF_RX_FILTER, __FUNCTION__);
    return Fn(Handle, Pattern, Mask, Length);
}

inline
HRESULT
FnLwfRxGetFrame(
    _In_ HANDLE Handle,
    _In_ UINT32 FrameIndex,
    _Inout_ UINT32 *FrameBufferLength,
    _Out_opt_ DATA_FRAME *Frame
    )
{
    LOAD_FN(FNLWF_RX_GET_FRAME, __FUNCTION__);
    return Fn(Handle, FrameIndex, FrameBufferLength, Frame);
}

inline
HRESULT
FnLwfRxDequeueFrame(
    _In_ HANDLE Handle,
    _In_ UINT32 FrameIndex
    )
{
    LOAD_FN(FNLWF_RX_DEQUEUE_FRAME, __FUNCTION__);
    return Fn(Handle, FrameIndex);
}

inline
HRESULT
FnLwfRxFlush(
    _In_ HANDLE Handle
    )
{
    LOAD_FN(FNLWF_RX_FLUSH, __FUNCTION__);
    return Fn(Handle);
}

inline
HRESULT
FnLwfOidSubmitRequest(
    _In_ HANDLE Handle,
    _In_ OID_KEY Key,
    _Inout_ UINT32 *InformationBufferLength,
    _Inout_opt_ VOID *InformationBuffer
    )
{
    LOAD_FN(FNLWF_OID_SUBMIT_REQUEST, __FUNCTION__);
    return Fn(Handle, Key, InformationBufferLength, InformationBuffer);
}

inline
HRESULT
FnLwfStatusSetFilter(
    _In_ HANDLE Handle,
    _In_ NDIS_STATUS StatusCode,
    _In_ BOOLEAN BlockIndications,
    _In_ BOOLEAN QueueIndications
    )
{
    LOAD_FN(FNLWF_STATUS_SET_FILTER, __FUNCTION__);
    return Fn(Handle, StatusCode, BlockIndications, QueueIndications);
}

inline
HRESULT
FnLwfStatusGetIndication(
    _In_ HANDLE Handle,
    _Inout_ UINT32 *StatusBufferLength,
    _Out_writes_bytes_opt_(*StatusBufferLength) VOID *StatusBuffer
    )
{
    LOAD_FN(FNLWF_STATUS_GET_INDICATION, __FUNCTION__);
    return Fn(Handle, StatusBufferLength, StatusBuffer);
}

inline
HRESULT
FnLwfDatapathGetState(
    _In_ HANDLE Handle,
    BOOLEAN *IsDatapathActive
    )
{
    LOAD_FN(FNLWF_DATAPATH_GET_STATE, __FUNCTION__);
    return Fn(Handle, IsDatapathActive);
}

inline
HRESULT
FnLwfLoadApi(
    )
{
    HRESULT Result;
    HMODULE ModuleHandle;

    ModuleHandle = LoadLibraryA("fnlwfapi.dll");
    if (ModuleHandle == NULL) {
        Result = E_NOINTERFACE;
    } else {
        FnLwfLoadApiContext = (FNLWF_LOAD_API_CONTEXT)ModuleHandle;
        Result = S_OK;
    }

    return Result;
}

inline
VOID
FnLwfUnloadApi(
    )
{
    HMODULE ModuleHandle = (HMODULE)FnLwfLoadApiContext;

    FreeLibrary(ModuleHandle);
}

EXTERN_C_END
