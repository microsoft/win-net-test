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

DECLARE_HANDLE(CXPLAT_SOCKET);

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
// Sockets API.
//

/*
 * WinSock error codes are also defined in winerror.h
 * Hence the IFDEF.
 */
#ifndef WSABASEERR

/*
 * All Windows Sockets error constants are biased by WSABASEERR from
 * the "normal"
 */
#define WSABASEERR              10000

/*
 * Windows Sockets definitions of regular Microsoft C error constants
 */
#define WSAEINTR                (WSABASEERR+4)
#define WSAEBADF                (WSABASEERR+9)
#define WSAEACCES               (WSABASEERR+13)
#define WSAEFAULT               (WSABASEERR+14)
#define WSAEINVAL               (WSABASEERR+22)
#define WSAEMFILE               (WSABASEERR+24)

/*
 * Windows Sockets definitions of regular Berkeley error constants
 */
#define WSAEWOULDBLOCK          (WSABASEERR+35)
#define WSAEINPROGRESS          (WSABASEERR+36)
#define WSAEALREADY             (WSABASEERR+37)
#define WSAENOTSOCK             (WSABASEERR+38)
#define WSAEDESTADDRREQ         (WSABASEERR+39)
#define WSAEMSGSIZE             (WSABASEERR+40)
#define WSAEPROTOTYPE           (WSABASEERR+41)
#define WSAENOPROTOOPT          (WSABASEERR+42)
#define WSAEPROTONOSUPPORT      (WSABASEERR+43)
#define WSAESOCKTNOSUPPORT      (WSABASEERR+44)
#define WSAEOPNOTSUPP           (WSABASEERR+45)
#define WSAEPFNOSUPPORT         (WSABASEERR+46)
#define WSAEAFNOSUPPORT         (WSABASEERR+47)
#define WSAEADDRINUSE           (WSABASEERR+48)
#define WSAEADDRNOTAVAIL        (WSABASEERR+49)
#define WSAENETDOWN             (WSABASEERR+50)
#define WSAENETUNREACH          (WSABASEERR+51)
#define WSAENETRESET            (WSABASEERR+52)
#define WSAECONNABORTED         (WSABASEERR+53)
#define WSAECONNRESET           (WSABASEERR+54)
#define WSAENOBUFS              (WSABASEERR+55)
#define WSAEISCONN              (WSABASEERR+56)
#define WSAENOTCONN             (WSABASEERR+57)
#define WSAESHUTDOWN            (WSABASEERR+58)
#define WSAETOOMANYREFS         (WSABASEERR+59)
#define WSAETIMEDOUT            (WSABASEERR+60)
#define WSAECONNREFUSED         (WSABASEERR+61)
#define WSAELOOP                (WSABASEERR+62)
#define WSAENAMETOOLONG         (WSABASEERR+63)
#define WSAEHOSTDOWN            (WSABASEERR+64)
#define WSAEHOSTUNREACH         (WSABASEERR+65)
#define WSAENOTEMPTY            (WSABASEERR+66)
#define WSAEPROCLIM             (WSABASEERR+67)
#define WSAEUSERS               (WSABASEERR+68)
#define WSAEDQUOT               (WSABASEERR+69)
#define WSAESTALE               (WSABASEERR+70)
#define WSAEREMOTE              (WSABASEERR+71)

/*
 * Extended Windows Sockets error constant definitions
 */
#define WSASYSNOTREADY          (WSABASEERR+91)
#define WSAVERNOTSUPPORTED      (WSABASEERR+92)
#define WSANOTINITIALISED       (WSABASEERR+93)
#define WSAEDISCON              (WSABASEERR+101)
#define WSAENOMORE              (WSABASEERR+102)
#define WSAECANCELLED           (WSABASEERR+103)
#define WSAEINVALIDPROCTABLE    (WSABASEERR+104)
#define WSAEINVALIDPROVIDER     (WSABASEERR+105)
#define WSAEPROVIDERFAILEDINIT  (WSABASEERR+106)
#define WSASYSCALLFAILURE       (WSABASEERR+107)
#define WSASERVICE_NOT_FOUND    (WSABASEERR+108)
#define WSATYPE_NOT_FOUND       (WSABASEERR+109)
#define WSA_E_NO_MORE           (WSABASEERR+110)
#define WSA_E_CANCELLED         (WSABASEERR+111)
#define WSAEREFUSED             (WSABASEERR+112)

#endif

_IRQL_requires_max_(PASSIVE_LEVEL)
CXPLAT_STATUS
CxPlatSocketCreate(
    _In_ INT AddressFamily,
    _In_ INT SocketType,
    _In_ INT Protocol,
    _Out_ CXPLAT_SOCKET* Socket
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
CxPlatSocketClose(
    _In_ CXPLAT_SOCKET Socket
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
CXPLAT_STATUS
CxPlatSocketBind(
    _In_ CXPLAT_SOCKET Socket,
    _In_reads_bytes_(AddressLength) const struct sockaddr* Address,
    _In_ INT AddressLength
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
CXPLAT_STATUS
CxPlatSocketGetSockName(
    _In_ CXPLAT_SOCKET Socket,
    _Out_writes_bytes_(*AddressLength) struct sockaddr* Address,
    _Inout_ INT* AddressLength
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
CXPLAT_STATUS
CxPlatSocketSetSockOpt(
    _In_ CXPLAT_SOCKET Socket,
    _In_ ULONG Level,
    _In_ ULONG OptionName,
    _In_reads_bytes_opt_(OptionLength) VOID* OptionValue,
    _In_ SIZE_T OptionLength
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
INT
CxPlatSocketSendto(
    _In_ CXPLAT_SOCKET Socket,
    _In_reads_bytes_(BufferLength) const CHAR* Buffer,
    _In_ INT BufferLength,
    _In_ INT Flags,
    _In_reads_bytes_(AddressLength) const struct sockaddr* Address,
    _In_ INT AddressLength
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
INT
CxPlatSocketRecv(
    _In_ CXPLAT_SOCKET Socket,
    _Out_writes_bytes_to_(BufferLength, return) CHAR* Buffer,
    _In_ INT BufferLength,
    _In_ INT Flags
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
INT
CxPlatSocketGetLastError(
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
    _In_ VOID* Mem
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
