//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include <winsock2.h>
#include <windows.h>
#include <winternl.h>
#include <stdlib.h>
#include <mswsock.h>

#define FNSOCKAPI __declspec(dllexport)

#include "fnsock.h"
#include "trace.h"

#include "sock.tmh"

BOOL
WINAPI
DllMain(
    HINSTANCE Module,
    DWORD Reason,
    LPVOID Reserved
    )
{
    UNREFERENCED_PARAMETER(Module);
    UNREFERENCED_PARAMETER(Reserved);

    switch (Reason)
    {
    case DLL_PROCESS_ATTACH:
        WPP_INIT_TRACING(NULL);
        break;
    case DLL_PROCESS_DETACH:
        WPP_CLEANUP();
        break;
    default:
        break;
    }
    return TRUE;
}

FNSOCKAPI
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

FNSOCKAPI
PAGEDX
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
FnSockUninitialize(
    VOID
    )
{
    TraceInfo("FnSockUninitialize");
    WSACleanup();
    WPP_CLEANUP();
}

FNSOCKAPI
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

FNSOCKAPI
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
FnSockClose(
    _In_ FNSOCK_HANDLE Socket
    )
{
    closesocket((SOCKET)Socket);
}

FNSOCKAPI
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

FNSOCKAPI
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

FNSOCKAPI
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

FNSOCKAPI
_IRQL_requires_max_(PASSIVE_LEVEL)
FNSOCK_STATUS
FnSockGetSockOpt(
    _In_ FNSOCK_HANDLE Socket,
    _In_ ULONG Level,
    _In_ ULONG OptionName,
    _Out_writes_bytes_(*OptionLength) VOID* OptionValue,
    _Inout_ SIZE_T* OptionLength
    )
{
    HRESULT Status;
    INT Error;

    Error = getsockopt((SOCKET)Socket, Level, OptionName, (CHAR*)OptionValue, (INT*)OptionLength);
    if (Error == SOCKET_ERROR) {
        TraceError(
            "[ lib] ERROR, %u, %s.",
            WSAGetLastError(),
            "getsockopt");
        Status = E_FAIL;
    } else {
        Status = S_OK;
    }

    return Status;
}

FNSOCKAPI
_IRQL_requires_max_(PASSIVE_LEVEL)
FNSOCK_STATUS
FnSockIoctl(
    _In_ FNSOCK_HANDLE Socket,
    _In_ ULONG ControlCode,
    _In_reads_bytes_opt_(InputLength) VOID* Input,
    _In_ ULONG InputLength,
    _Out_writes_bytes_opt_(OutputLength) VOID* Output,
    _In_ ULONG OutputLength,
    _Out_ ULONG* BytesReturned
    )
{
    HRESULT Status;
    INT Error;

    Error =
        WSAIoctl(
            (SOCKET)Socket, ControlCode, Input, InputLength, Output, OutputLength, BytesReturned,
            NULL, NULL);
    if (Error == SOCKET_ERROR) {
        TraceError(
            "[ lib] ERROR, %u, %s.",
            WSAGetLastError(),
            "WSAIoctl");
        Status = E_FAIL;
    } else {
        Status = S_OK;
    }

    return Status;
}

FNSOCKAPI
_IRQL_requires_max_(PASSIVE_LEVEL)
FNSOCK_STATUS
FnSockListen(
    _In_ FNSOCK_HANDLE Socket,
    _In_ ULONG Backlog
    )
{
    HRESULT Status;
    INT Error;

    Error = listen((SOCKET)Socket, Backlog);
    if (Error == SOCKET_ERROR) {
        TraceError(
            "[ lib] ERROR, %u, %s.",
            WSAGetLastError(),
            "listen");
        Status = E_FAIL;
    } else {
        Status = S_OK;
    }

    return Status;
}

FNSOCKAPI
_IRQL_requires_max_(PASSIVE_LEVEL)
FNSOCK_HANDLE
FnSockAccept(
    _In_ FNSOCK_HANDLE Socket,
    _Out_writes_bytes_(*AddressLength) struct sockaddr* Address,
    _Inout_ INT* AddressLength
    )
{
    HRESULT Status;
    SOCKET AcceptedSocket;

    AcceptedSocket = accept((SOCKET)Socket, Address, AddressLength);
    if (AcceptedSocket == INVALID_SOCKET) {
        TraceError(
            "[ lib] ERROR, %u, %s.",
            WSAGetLastError(),
            "accept");
        Status = E_FAIL;
        AcceptedSocket = 0;
    } else {
        Status = S_OK;
    }

    return (FNSOCK_HANDLE)AcceptedSocket;
}

FNSOCKAPI
FNSOCK_STATUS
FnSockConnect(
    _In_ FNSOCK_HANDLE Socket,
    _In_reads_bytes_(AddressLength) struct sockaddr* Address,
    _In_ INT AddressLength
    )
{
    HRESULT Status;
    INT Error;

    Error = connect((SOCKET)Socket, Address, AddressLength);
    if (Error == SOCKET_ERROR) {
        TraceError(
            "[ lib] ERROR, %u, %s.",
            WSAGetLastError(),
            "connect");
        Status = E_FAIL;
    } else {
        Status = S_OK;
    }

    return Status;
}

FNSOCKAPI
_IRQL_requires_max_(PASSIVE_LEVEL)
INT
FnSockSend(
    _In_ FNSOCK_HANDLE Socket,
    _In_reads_bytes_(BufferLength) const CHAR* Buffer,
    _In_ INT BufferLength,
    _In_ BOOLEAN BufferIsNonPagedPool,
    _In_ INT Flags
    )
{
    UNREFERENCED_PARAMETER(BufferIsNonPagedPool);

    return send((SOCKET)Socket, Buffer, BufferLength, Flags);
}

FNSOCKAPI
_IRQL_requires_max_(PASSIVE_LEVEL)
INT
FnSockSendto(
    _In_ FNSOCK_HANDLE Socket,
    _In_reads_bytes_(BufferLength) const CHAR* Buffer,
    _In_ INT BufferLength,
    _In_ BOOLEAN BufferIsNonPagedPool,
    _In_ INT Flags,
    _In_reads_bytes_(AddressLength) const struct sockaddr* Address,
    _In_ INT AddressLength
    )
{
    UNREFERENCED_PARAMETER(BufferIsNonPagedPool);

    return sendto((SOCKET)Socket, Buffer, BufferLength, Flags, Address, AddressLength);
}

FNSOCKAPI
_IRQL_requires_max_(PASSIVE_LEVEL)
INT
FnSockRecv(
    _In_ FNSOCK_HANDLE Socket,
    _Out_writes_bytes_to_(BufferLength, return) CHAR* Buffer,
    _In_ INT BufferLength,
    _In_ BOOLEAN BufferIsNonPagedPool,
    _In_ INT Flags
    )
{
    UNREFERENCED_PARAMETER(BufferIsNonPagedPool);

    return recv((SOCKET)Socket, Buffer, BufferLength, Flags);
}

FNSOCKAPI
_IRQL_requires_max_(PASSIVE_LEVEL)
INT
FnSockRecvMsg(
    _In_ FNSOCK_HANDLE Socket,
    _Out_writes_bytes_to_(BufferLength, return) CHAR* Buffer,
    _In_ INT BufferLength,
    _In_ BOOLEAN BufferIsNonPagedPool,
    _Out_writes_bytes_to_(*ControlBufferLength, *ControlBufferLength) CMSGHDR *ControlBuffer,
    _Inout_ INT *ControlBufferLength,
    _In_ INT *Flags
    )
{
    static LPFN_WSARECVMSG CachedWsaRecvMsg = NULL;
    LPFN_WSARECVMSG WsaRecvMsg = (LPFN_WSARECVMSG)ReadPointerNoFence(&(VOID *)CachedWsaRecvMsg);
    DWORD BytesReceived;
    WSAMSG Msg = {0};
    WSABUF Buf = {0};

    UNREFERENCED_PARAMETER(BufferIsNonPagedPool);

    if (WsaRecvMsg == NULL) {
        GUID Guid = WSAID_WSARECVMSG;
        DWORD BytesReturned;

        if (WSAIoctl((SOCKET)Socket, SIO_GET_EXTENSION_FUNCTION_POINTER, &Guid, sizeof(Guid),
                &WsaRecvMsg, sizeof(WsaRecvMsg), &BytesReturned, NULL, NULL) == SOCKET_ERROR) {
            TraceError(
                "[ lib] ERROR, %u, %s.",
                WSAGetLastError(),
                "WSAIoctl");
            return SOCKET_ERROR;
        }

        WritePointerNoFence(&(VOID *)CachedWsaRecvMsg, (VOID *)WsaRecvMsg);
    }

    Buf.buf = Buffer;
    Buf.len = BufferLength;
    Msg.lpBuffers = &Buf;
    Msg.dwBufferCount = 1;
    Msg.Control.buf = (CHAR*)ControlBuffer;
    Msg.Control.len = *ControlBufferLength;
    Msg.dwFlags = *Flags;

    if (WsaRecvMsg((SOCKET)Socket, &Msg, &BytesReceived, NULL, NULL) == SOCKET_ERROR) {
        TraceError(
            "[ lib] ERROR, %u, %s.",
            WSAGetLastError(),
            "WSARecvMsg");
        return SOCKET_ERROR;
    }

    *ControlBufferLength = (INT)Msg.Control.len;
    *Flags = (INT)Msg.dwFlags;

    return BytesReceived;
}

FNSOCKAPI
_IRQL_requires_max_(PASSIVE_LEVEL)
INT
FnSockGetLastError(
    VOID
    )
{
    return WSAGetLastError();
}
