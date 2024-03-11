//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include <windows.h>

#include "cxplat.h"
#include "trace.h"

#include "cxplat.tmh"

PAGEDX
_IRQL_requires_max_(PASSIVE_LEVEL)
CXPLAT_STATUS
CxPlatInitialize(
    VOID
    )
{
    INT WsaError;
    CXPLAT_STATUS Status;
    WSADATA WsaData;
    BOOLEAN WsaInitialized = FALSE;

    TraceError("CxPlatInitialize");

    if ((WsaError = WSAStartup(MAKEWORD(2, 2), &WsaData)) != 0) {
        TraceError(
            "[ lib] ERROR, %u, %s.",
            WsaError,
            "WSAStartup");
        Status = HRESULT_FROM_WIN32(WsaError);
        goto Exit;
    }
    WsaInitialized = TRUE;

    Status = CXPLAT_STATUS_SUCCESS;

Exit:

    if (CXPLAT_FAILED(Status)) {
        if (WsaInitialized) {
            (VOID)WSACleanup();
        }
    }

    return Status;
}

PAGEDX
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
CxPlatUninitialize(
    VOID
    )
{
    WSACleanup();
}

//
// Sockets API.
//

_IRQL_requires_max_(PASSIVE_LEVEL)
CXPLAT_STATUS
CxPlatSocketCreate(
    _In_ INT AddressFamily,
    _In_ INT SocketType,
    _In_ INT Protocol,
    _Out_ CXPLAT_SOCKET* Socket
    )
{
    CXPLAT_STATUS Status;
    SOCKET WsaSocket;

    WsaSocket = socket(AddressFamily, SocketType, Protocol);
    if (WsaSocket == INVALID_SOCKET) {
        TraceError(
            "[ lib] ERROR, %u, %s.",
            WSAGetLastError(),
            "socket");
        Status = CXPLAT_STATUS_FAIL;
    } else {
        Status = CXPLAT_STATUS_SUCCESS;
        *Socket = (CXPLAT_SOCKET)WsaSocket;
    }

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
CxPlatSocketClose(
    _In_ CXPLAT_SOCKET Socket
    )
{
    closesocket((SOCKET)Socket);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
CXPLAT_STATUS
CxPlatSocketBind(
    _In_ CXPLAT_SOCKET Socket,
    _In_reads_bytes_(AddressLength) const struct sockaddr* Address,
    _In_ INT AddressLength
    )
{
    CXPLAT_STATUS Status;
    INT Error;

    Error = bind((SOCKET)Socket, Address, AddressLength);
    if (Error == SOCKET_ERROR) {
        TraceError(
            "[ lib] ERROR, %u, %s.",
            WSAGetLastError(),
            "bind");
        Status = CXPLAT_STATUS_FAIL;
    } else {
        Status = CXPLAT_STATUS_SUCCESS;
    }

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
CXPLAT_STATUS
CxPlatSocketGetSockName(
    _In_ CXPLAT_SOCKET Socket,
    _Out_writes_bytes_(*AddressLength) struct sockaddr* Address,
    _Inout_ INT* AddressLength
    )
{
    CXPLAT_STATUS Status;
    INT Error;

    Error = getsockname((SOCKET)Socket, Address, AddressLength);
    if (Error == SOCKET_ERROR) {
        TraceError(
            "[ lib] ERROR, %u, %s.",
            WSAGetLastError(),
            "getsockname");
        Status = CXPLAT_STATUS_FAIL;
    } else {
        Status = CXPLAT_STATUS_SUCCESS;
    }

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
CXPLAT_STATUS
CxPlatSocketSetSockOpt(
    _In_ CXPLAT_SOCKET Socket,
    _In_ ULONG Level,
    _In_ ULONG OptionName,
    _In_reads_bytes_opt_(OptionLength) VOID* OptionValue,
    _In_ SIZE_T OptionLength
    )
{
    CXPLAT_STATUS Status;
    INT Error;

    Error = setsockopt((SOCKET)Socket, Level, OptionName, (const CHAR*)OptionValue, (INT)OptionLength);
    if (Error == SOCKET_ERROR) {
        TraceError(
            "[ lib] ERROR, %u, %s.",
            WSAGetLastError(),
            "setsockopt");
        Status = CXPLAT_STATUS_FAIL;
    } else {
        Status = CXPLAT_STATUS_SUCCESS;
    }

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
INT
CxPlatSocketSendto(
    _In_ CXPLAT_SOCKET Socket,
    _In_reads_bytes_(BufferLength) const CHAR* Buffer,
    _In_ INT BufferLength,
    _In_ INT Flags,
    _In_reads_bytes_(AddressLength) const struct sockaddr* Address,
    _In_ INT AddressLength
    )
{
    return sendto((SOCKET)Socket, Buffer, BufferLength, Flags, Address, AddressLength);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
INT
CxPlatSocketRecv(
    _In_ CXPLAT_SOCKET Socket,
    _Out_writes_bytes_to_(BufferLength, return) CHAR* Buffer,
    _In_ INT BufferLength,
    _In_ INT Flags
    )
{
    return recv((SOCKET)Socket, Buffer, BufferLength, Flags);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
INT
CxPlatSocketGetLastError(
    VOID
    )
{
    return WSAGetLastError();
}