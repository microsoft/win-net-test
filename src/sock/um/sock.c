//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include <winsock2.h>
#include <windows.h>
#include <winternl.h>
#include <stdlib.h>

#include "fnsock.h"
#include "trace.h"

#include "sock.tmh"

PAGEDX
_IRQL_requires_max_(PASSIVE_LEVEL)
FNSOCK_STATUS
FnSockInitialize(
    VOID
    )
{
    INT WsaError;
    HRESULT Status;
    WSADATA WsaData;

    TraceInfo("FnSockInitialize");

    if ((WsaError = WSAStartup(MAKEWORD(2, 2), &WsaData)) != 0) {
        TraceError(
            "[ lib] ERROR, %u, %s.",
            WsaError,
            "WSAStartup");
        Status = HRESULT_FROM_WIN32(WsaError);
    } else {
        Status = S_OK;
    }

    return Status;
}

PAGEDX
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
FnSockUninitialize(
    VOID
    )
{
    WSACleanup();
}

_IRQL_requires_max_(PASSIVE_LEVEL)
FNSOCK_STATUS
FnSockCreate(
    _In_ INT AddressFamily,
    _In_ INT SocketType,
    _In_ INT Protocol,
    _Out_ FNSOCK_HANDLE* Socket
    )
{
    HRESULT Status;
    SOCKET WsaSocket;

    WsaSocket = socket(AddressFamily, SocketType, Protocol);
    if (WsaSocket == INVALID_SOCKET) {
        TraceError(
            "[ lib] ERROR, %u, %s.",
            WSAGetLastError(),
            "socket");
        Status = E_FAIL;
    } else {
        Status = S_OK;
        *Socket = (FNSOCK_HANDLE)WsaSocket;
    }

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
FnSockClose(
    _In_ FNSOCK_HANDLE Socket
    )
{
    closesocket((SOCKET)Socket);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
FNSOCK_STATUS
FnSockBind(
    _In_ FNSOCK_HANDLE Socket,
    _In_reads_bytes_(AddressLength) const struct sockaddr* Address,
    _In_ INT AddressLength
    )
{
    HRESULT Status;
    INT Error;

    Error = bind((SOCKET)Socket, Address, AddressLength);
    if (Error == SOCKET_ERROR) {
        TraceError(
            "[ lib] ERROR, %u, %s.",
            WSAGetLastError(),
            "bind");
        Status = E_FAIL;
    } else {
        Status = S_OK;
    }

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
FNSOCK_STATUS
FnSockGetSockName(
    _In_ FNSOCK_HANDLE Socket,
    _Out_writes_bytes_(*AddressLength) struct sockaddr* Address,
    _Inout_ INT* AddressLength
    )
{
    HRESULT Status;
    INT Error;

    Error = getsockname((SOCKET)Socket, Address, AddressLength);
    if (Error == SOCKET_ERROR) {
        TraceError(
            "[ lib] ERROR, %u, %s.",
            WSAGetLastError(),
            "getsockname");
        Status = E_FAIL;
    } else {
        Status = S_OK;
    }

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
FNSOCK_STATUS
FnSockSetSockOpt(
    _In_ FNSOCK_HANDLE Socket,
    _In_ ULONG Level,
    _In_ ULONG OptionName,
    _In_reads_bytes_opt_(OptionLength) VOID* OptionValue,
    _In_ SIZE_T OptionLength
    )
{
    HRESULT Status;
    INT Error;

    Error = setsockopt((SOCKET)Socket, Level, OptionName, (const CHAR*)OptionValue, (INT)OptionLength);
    if (Error == SOCKET_ERROR) {
        TraceError(
            "[ lib] ERROR, %u, %s.",
            WSAGetLastError(),
            "setsockopt");
        Status = E_FAIL;
    } else {
        Status = S_OK;
    }

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
INT
FnSockSendto(
    _In_ FNSOCK_HANDLE Socket,
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
FnSockRecv(
    _In_ FNSOCK_HANDLE Socket,
    _Out_writes_bytes_to_(BufferLength, return) CHAR* Buffer,
    _In_ INT BufferLength,
    _In_ INT Flags
    )
{
    return recv((SOCKET)Socket, Buffer, BufferLength, Flags);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
INT
FnSockGetLastError(
    VOID
    )
{
    return WSAGetLastError();
}
