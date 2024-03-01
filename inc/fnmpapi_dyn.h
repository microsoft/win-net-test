//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

#include <fniotypes.h>

typedef
HRESULT
FNMP_OPEN_SHARED(
    _In_ UINT32 IfIndex,
    _Out_ HANDLE *Handle
    );

typedef
HRESULT
FNMP_OPEN_EXCLUSIVE(
    _In_ UINT32 IfIndex,
    _Out_ HANDLE *Handle
    );

typedef
HRESULT
FNMP_RX_ENQUEUE(
    _In_ HANDLE Handle,
    _In_ DATA_FRAME *Frame
    );

typedef
HRESULT
FNMP_RX_FLUSH(
    _In_ HANDLE Handle,
    _In_opt_ DATA_FLUSH_OPTIONS *Options
    );

typedef
HRESULT
FNMP_TX_FILTER(
    _In_ HANDLE Handle,
    _In_ const VOID *Pattern,
    _In_ const VOID *Mask,
    _In_ UINT32 Length
    );

typedef
HRESULT
FNMP_TX_GET_FRAME(
    _In_ HANDLE Handle,
    _In_ UINT32 FrameIndex,
    _In_ UINT32 FrameSubIndex,
    _Inout_ UINT32 *FrameBufferLength,
    _Out_opt_ DATA_FRAME *Frame
    );

typedef
HRESULT
FNMP_TX_DEQUEUE_FRAME(
    _In_ HANDLE Handle,
    _In_ UINT32 FrameIndex
    );

typedef
HRESULT
FNMP_TX_FLUSH(
    _In_ HANDLE Handle
    );

typedef
HRESULT
FNMP_GET_LAST_MINIPORT_TIMESTAMP(
    _In_ HANDLE Handle,
    _Out_ LARGE_INTEGER *Timestamp
    );

typedef
HRESULT
FNMP_SET_MTU(
    _In_ HANDLE Handle,
    _In_ UINT32 Mtu
    );

typedef
HRESULT
FNMP_OID_FILTER(
    _In_ HANDLE Handle,
    _In_ const OID_KEY *Keys,
    _In_ UINT32 KeyCount
    );

typedef
HRESULT
FNMP_OID_GET_REQUEST(
    _In_ HANDLE Handle,
    _In_ OID_KEY Key,
    _Inout_ UINT32 *InformationBufferLength,
    _Out_opt_ VOID *InformationBuffer
    );

typedef
HRESULT
FNMP_OID_COMPLETE_REQUEST(
    _In_ HANDLE Handle,
    _In_ OID_KEY Key,
    _In_ NDIS_STATUS Status,
    _In_opt_ const VOID *InformationBuffer,
    _In_ UINT32 InformationBufferLength
    );

typedef
HRESULT
FNMP_UPDATE_TASK_OFFLOAD(
    _In_ HANDLE Handle,
    _In_ FN_OFFLOAD_TYPE OffloadType,
    _In_opt_ const NDIS_OFFLOAD_PARAMETERS *OffloadParameters,
    _In_ UINT32 OffloadParametersLength
    );

typedef struct _FNMP_LOAD_CONTEXT *FNMP_LOAD_API_CONTEXT;

extern FNMP_LOAD_API_CONTEXT FnMpLoadApiContext;

#define FNMP_LOAD_FN(TYPE, Name) \
    static TYPE *Fn; \
    if (Fn == NULL) { \
        Fn = (TYPE *)GetProcAddress((HMODULE)FnMpLoadApiContext, Name); \
    } \
    if (Fn == NULL) { \
        return E_NOINTERFACE; \
    }

inline
HRESULT
FnMpOpenShared(
    _In_ UINT32 IfIndex,
    _Out_ HANDLE *Handle
    )
{
    FNMP_LOAD_FN(FNMP_OPEN_SHARED, __FUNCTION__);
    return Fn(IfIndex, Handle);
}

inline
HRESULT
FnMpOpenExclusive(
    _In_ UINT32 IfIndex,
    _Out_ HANDLE *Handle
    )
{
    FNMP_LOAD_FN(FNMP_OPEN_EXCLUSIVE, __FUNCTION__);
    return Fn(IfIndex, Handle);
}

inline
HRESULT
FnMpRxEnqueue(
    _In_ HANDLE Handle,
    _In_ DATA_FRAME *Frame
    )
{
    FNMP_LOAD_FN(FNMP_RX_ENQUEUE, __FUNCTION__);
    return Fn(Handle, Frame);
}

inline
HRESULT
FnMpRxFlush(
    _In_ HANDLE Handle,
    _In_opt_ DATA_FLUSH_OPTIONS *Options
    )
{
    FNMP_LOAD_FN(FNMP_RX_FLUSH, __FUNCTION__);
    return Fn(Handle, Options);
}

inline
HRESULT
FnMpTxFilter(
    _In_ HANDLE Handle,
    _In_ const VOID *Pattern,
    _In_ const VOID *Mask,
    _In_ UINT32 Length
    )
{
    FNMP_LOAD_FN(FNMP_TX_FILTER, __FUNCTION__);
    return Fn(Handle, Pattern, Mask, Length);
}

inline
HRESULT
FnMpTxGetFrame(
    _In_ HANDLE Handle,
    _In_ UINT32 FrameIndex,
    _In_ UINT32 FrameSubIndex,
    _Inout_ UINT32 *FrameBufferLength,
    _Out_opt_ DATA_FRAME *Frame
    )
{
    FNMP_LOAD_FN(FNMP_TX_GET_FRAME, __FUNCTION__);
    return Fn(Handle, FrameIndex, FrameSubIndex, FrameBufferLength, Frame);
}

inline
HRESULT
FnMpTxDequeueFrame(
    _In_ HANDLE Handle,
    _In_ UINT32 FrameIndex
    )
{
    FNMP_LOAD_FN(FNMP_TX_DEQUEUE_FRAME, __FUNCTION__);
    return Fn(Handle, FrameIndex);
}

inline
HRESULT
FnMpTxFlush(
    _In_ HANDLE Handle
    )
{
    FNMP_LOAD_FN(FNMP_TX_FLUSH, __FUNCTION__);
    return Fn(Handle);
}

inline
HRESULT
FnMpGetLastMiniportPauseTimestamp(
    _In_ HANDLE Handle,
    _Out_ LARGE_INTEGER *Timestamp
    )
{
    FNMP_LOAD_FN(FNMP_GET_LAST_MINIPORT_TIMESTAMP, __FUNCTION__);
    return Fn(Handle, Timestamp);
}

inline
HRESULT
FnMpSetMtu(
    _In_ HANDLE Handle,
    _In_ UINT32 Mtu
    )
{
    FNMP_LOAD_FN(FNMP_SET_MTU, __FUNCTION__);
    return Fn(Handle, Mtu);
}

inline
HRESULT
FnMpOidFilter(
    _In_ HANDLE Handle,
    _In_ const OID_KEY *Keys,
    _In_ UINT32 KeyCount
    )
{
    FNMP_LOAD_FN(FNMP_OID_FILTER, __FUNCTION__);
    return Fn(Handle, Keys, KeyCount);
}

inline
HRESULT
FnMpOidGetRequest(
    _In_ HANDLE Handle,
    _In_ OID_KEY Key,
    _Inout_ UINT32 *InformationBufferLength,
    _Out_opt_ VOID *InformationBuffer
    )
{
    FNMP_LOAD_FN(FNMP_OID_GET_REQUEST, __FUNCTION__);
    return Fn(Handle, Key, InformationBufferLength, InformationBuffer);
}

inline
HRESULT
FnMpOidCompleteRequest(
    _In_ HANDLE Handle,
    _In_ OID_KEY Key,
    _In_ NDIS_STATUS Status,
    _In_opt_ const VOID *InformationBuffer,
    _In_ UINT32 InformationBufferLength
    )
{
    FNMP_LOAD_FN(FNMP_OID_COMPLETE_REQUEST, __FUNCTION__);
    return Fn(Handle, Key, Status, InformationBuffer, InformationBufferLength);
}

inline
HRESULT
FnMpUpdateTaskOffload(
    _In_ HANDLE Handle,
    _In_ FN_OFFLOAD_TYPE OffloadType,
    _In_opt_ const NDIS_OFFLOAD_PARAMETERS *OffloadParameters,
    _In_ UINT32 OffloadParametersLength
    )
{
    FNMP_LOAD_FN(FNMP_UPDATE_TASK_OFFLOAD, __FUNCTION__);
    return Fn(Handle, OffloadType, OffloadParameters, OffloadParametersLength);
}

inline
HRESULT
FnMpLoadApi(
    )
{
    HRESULT Result;
    HMODULE ModuleHandle;

    ModuleHandle = LoadLibraryA("fnmpapi.dll");
    if (ModuleHandle == NULL) {
        Result = E_NOINTERFACE;
    } else {
        FnMpLoadApiContext = (FNMP_LOAD_API_CONTEXT)ModuleHandle;
        Result = S_OK;
    }

    return Result;
}

inline
VOID
FnMpUnloadApi(
    )
{
    HMODULE ModuleHandle = (HMODULE)FnMpLoadApiContext;

    FreeLibrary(ModuleHandle);
}

EXTERN_C_END
