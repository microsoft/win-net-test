//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// This API is comprised of copied snippets of the cross-platform pieces of
// https://github.com/microsoft/msquic, along with a hand-rolled sockets API.
// Eventually this API should be replaced with https://github.com/microsoft/cxplat.
//

#pragma once

EXTERN_C_START

#if defined(KERNEL_MODE)

#define CXPLAT_STATUS NTSTATUS
#define CXPLAT_FAILED(X) !NT_SUCCESS(X)
#define CXPLAT_SUCCEEDED(X) NT_SUCCESS(X)
#define CXPLAT_STATUS_SUCCESS STATUS_SUCCESS
#define CXPLAT_STATUS_FAIL STATUS_UNSUCCESSFUL
#ifndef KRTL_INIT_SEGMENT
#define KRTL_INIT_SEGMENT "INIT"
#endif
#ifndef KRTL_PAGE_SEGMENT
#define KRTL_PAGE_SEGMENT "PAGE"
#endif
#ifndef KRTL_NONPAGED_SEGMENT
#define KRTL_NONPAGED_SEGMENT ".text"
#endif
// Use on code in the INIT segment. (Code is discarded after DriverEntry returns.)
#define INITCODE __declspec(code_seg(KRTL_INIT_SEGMENT))
// Use on pageable functions.
#define PAGEDX __declspec(code_seg(KRTL_PAGE_SEGMENT))

#else // defined(KERNEL_MODE)

#define CXPLAT_STATUS HRESULT
#define CXPLAT_FAILED(X) FAILED(X)
#define CXPLAT_SUCCEEDED(X) SUCCEEDED(X)
#define CXPLAT_STATUS_SUCCESS S_OK
#define CXPLAT_STATUS_FAIL E_FAIL
#define PAGEDX

#endif // defined(KERNEL_MODE)

PAGEDX
_IRQL_requires_max_(PASSIVE_LEVEL)
CXPLAT_STATUS
CxPlatInitialize(
    VOID
    );

PAGEDX
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
CxPlatUninitialize(
    VOID
    );

//
// Time Measurement Interfaces
//

#define US_TO_MS(x)     ((x) / 1000)
#define MS_TO_US(x)     ((x) * 1000)

//
// Performance counter frequency.
//
extern UINT64 CxPlatPerfFreq;

//
// Returns the current time in platform specific time units.
//
UINT64
CxPlatTimePlat(
    VOID
    );

//
// Converts platform time to microseconds.
//
inline
UINT64
CxPlatTimePlatToUs64(
    UINT64 Count
    )
{
    //
    // Multiply by a big number (1000000, to convert seconds to microseconds)
    // and divide by a big number (CxPlatPerfFreq, to convert counts to secs).
    //
    // Avoid overflow with separate multiplication/division of the high and low
    // bits. Taken from TcpConvertPerformanceCounterToMicroseconds.
    //
    UINT64 High = (Count >> 32) * 1000000;
    UINT64 Low = (Count & 0xFFFFFFFF) * 1000000;
    return
        ((High / CxPlatPerfFreq) << 32) +
        ((Low + ((High % CxPlatPerfFreq) << 32)) / CxPlatPerfFreq);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
CxPlatSleep(
    _In_ UINT32 DurationMs
    );

//
// Allocation/Memory Interfaces
//

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID*
CxPlatAllocNonPaged(
    _In_ SIZE_T Size,
    _In_ ULONG Tag
    );

VOID
CxPlatFree(
    _In_ VOID* Mem,
    _In_ ULONG Tag
    );

VOID
CxPlatFreeNoTag(
    _In_opt_ VOID* Mem
    );

//
// Create Thread Interfaces
//

DECLARE_HANDLE(CXPLAT_THREAD);

#if defined(KERNEL_MODE)
typedef VOID CXPLAT_THREAD_RETURN_TYPE;
#define CXPLAT_THREAD_RETURN(Status) PsTerminateSystemThread(Status)
#else
typedef DWORD CXPLAT_THREAD_RETURN_TYPE;
#define CXPLAT_THREAD_RETURN(Status) return (DWORD)(Status)
#endif

typedef
_IRQL_requires_same_
CXPLAT_THREAD_RETURN_TYPE
CXPLAT_THREAD_ROUTINE(
    _In_ VOID* Context
    );

typedef enum CXPLAT_THREAD_FLAGS {
    CXPLAT_THREAD_FLAG_NONE               = 0x0000,
    CXPLAT_THREAD_FLAG_SET_IDEAL_PROC     = 0x0001,
    CXPLAT_THREAD_FLAG_SET_AFFINITIZE     = 0x0002,
    CXPLAT_THREAD_FLAG_HIGH_PRIORITY      = 0x0004
} CXPLAT_THREAD_FLAGS;

#ifdef DEFINE_ENUM_FLAG_OPERATORS
DEFINE_ENUM_FLAG_OPERATORS(CXPLAT_THREAD_FLAGS);
#endif

typedef struct CXPLAT_THREAD_CONFIG {
    UINT16 Flags;
    UINT16 IdealProcessor;
    _Field_z_ const CHAR* Name;
    CXPLAT_THREAD_ROUTINE* Callback;
    VOID* Context;
} CXPLAT_THREAD_CONFIG;

CXPLAT_STATUS
CxPlatThreadCreate(
    _In_ CXPLAT_THREAD_CONFIG* Config,
    _Out_ CXPLAT_THREAD* Thread
    );

VOID
CxPlatThreadDelete(
    _In_ CXPLAT_THREAD
    );

BOOLEAN
CxPlatThreadWait(
    _In_ CXPLAT_THREAD,
    _In_ UINT32 TimeoutMs
    );

EXTERN_C_END
