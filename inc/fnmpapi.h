//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

#include <fniotypes.h>

#ifndef FNMPAPI
#define FNMPAPI __declspec(dllimport)
#endif

#define FNMP_DEFAULT_RSS_QUEUES 4
#define FNMP_MAX_RSS_INDIR_COUNT 128

FNMPAPI
HRESULT
FnMpOpenShared(
    _In_ UINT32 IfIndex,
    _Out_ HANDLE *Handle
    );

FNMPAPI
HRESULT
FnMpOpenExclusive(
    _In_ UINT32 IfIndex,
    _Out_ HANDLE *Handle
    );

FNMPAPI
HRESULT
FnMpRxEnqueue(
    _In_ HANDLE Handle,
    _In_ DATA_FRAME *Frame
    );

FNMPAPI
HRESULT
FnMpRxFlush(
    _In_ HANDLE Handle,
    _In_opt_ DATA_FLUSH_OPTIONS *Options
    );

FNMPAPI
HRESULT
FnMpTxFilter(
    _In_ HANDLE Handle,
    _In_ const VOID *Pattern,
    _In_ const VOID *Mask,
    _In_ UINT32 Length
    );

FNMPAPI
HRESULT
FnMpTxGetFrame(
    _In_ HANDLE Handle,
    _In_ UINT32 FrameIndex,
    _In_ UINT32 FrameSubIndex,
    _Inout_ UINT32 *FrameBufferLength,
    _Out_opt_ DATA_FRAME *Frame
    );

FNMPAPI
HRESULT
FnMpTxDequeueFrame(
    _In_ HANDLE Handle,
    _In_ UINT32 FrameIndex
    );

FNMPAPI
HRESULT
FnMpTxFlush(
    _In_ HANDLE Handle
    );

FNMPAPI
HRESULT
FnMpGetLastMiniportPauseTimestamp(
    _In_ HANDLE Handle,
    _Out_ LARGE_INTEGER *Timestamp
    );

#define FNMP_MIN_MTU 1514
#define FNMP_MAX_MTU (16 * 1024 * 1024)
#define FNMP_DEFAULT_MTU FNMP_MAX_MTU

FNMPAPI
HRESULT
FnMpSetMtu(
    _In_ HANDLE Handle,
    _In_ UINT32 Mtu
    );

FNMPAPI
HRESULT
FnMpOidFilter(
    _In_ HANDLE Handle,
    _In_ const OID_KEY *Keys,
    _In_ UINT32 KeyCount
    );

FNMPAPI
HRESULT
FnMpOidGetRequest(
    _In_ HANDLE Handle,
    _In_ OID_KEY Key,
    _Inout_ UINT32 *InformationBufferLength,
    _Out_opt_ VOID *InformationBuffer
    );

FNMPAPI
HRESULT
FnMpOidCompleteRequest(
    _In_ HANDLE Handle,
    _In_ OID_KEY Key,
    _In_ NDIS_STATUS Status,
    _In_opt_ const VOID *InformationBuffer,
    _In_ UINT32 InformationBufferLength
    );

EXTERN_C_END
