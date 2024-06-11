//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// Implements a user mode WinSock like sockets API with WSK. The main pieces
// needed to bridge the gap between user mode and kernel mode WinSock are
// - fire and forget TX
// - RX buffering
// - accept buffering
//
// This module implements these pieces and leverages wskclient for the basic
// WSK interactions where possible.
//

#include <ntddk.h>
#include <wsk.h>

#include "fnsock.h"
#include "pooltag.h"
#include "trace.h"
#include "wskclient.h"

#include "sock.tmh"

typedef struct _WSK_DATAGRAM_SOCKET {
    const WSK_PROVIDER_DATAGRAM_DISPATCH* Dispatch;
} WSK_DATAGRAM_SOCKET, * PWSK_DATAGRAM_SOCKET;

typedef struct FNSOCK_SOCKET_BINDING {
    PWSK_SOCKET Socket;

    ADDRESS_FAMILY AddressFamily;
    ULONG SockType;
    BOOLEAN IsBound;
    UINT32 ReceiveTimeout;

    KSPIN_LOCK SendContextLock;
    LIST_ENTRY SendContextList;

    KSPIN_LOCK RecvDataLock;
    LIST_ENTRY RecvDataList;
    KEVENT RecvEvent;

    KSPIN_LOCK AcceptLock;
    LIST_ENTRY AcceptList;
    KEVENT AcceptEvent;
} FNSOCK_SOCKET_BINDING;

typedef struct FNSOCK_SOCKET_ACCEPT_CONTEXT {
    LIST_ENTRY Link;
    FNSOCK_SOCKET_BINDING* AcceptedSocket;
} FNSOCK_SOCKET_ACCEPT_CONTEXT;

typedef struct FNSOCK_SOCKET_RECV_DATA {
    LIST_ENTRY Link;
    ULONG DataLength;
    UCHAR Data[0];
} FNSOCK_SOCKET_RECV_DATA;

typedef struct FNSOCK_SOCKET_SEND_CONTEXT {
    LIST_ENTRY Link;
    VOID* WskClientSendContext;
} FNSOCK_SOCKET_SEND_CONTEXT;

static WSK_CLIENT_DATAGRAM_DISPATCH WskDatagramDispatch;

static WSK_CLIENT_LISTEN_DISPATCH WskListenDispatch;
static WSK_CLIENT_CONNECTION_DISPATCH WskConnectionDispatch;
static WSK_CLIENT_STREAM_DISPATCH WskStreamDispatch;

static NTSTATUS FnSockSocketLastError;

_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_
NTSTATUS
NTAPI
FnSockDatagramSocketReceive(
    _In_opt_ void* SocketContext,
    _In_ ULONG Flags,
    _In_opt_ PWSK_DATAGRAM_INDICATION DataIndicationHead
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_
_At_(AcceptSocket, __drv_aliasesMem)
NTSTATUS
NTAPI
FnSockStreamSocketAccept(
    _In_opt_ void* SocketContext,
    _In_ ULONG Flags,
    _In_ PSOCKADDR LocalAddress,
    _In_ PSOCKADDR RemoteAddress,
    _In_opt_ PWSK_SOCKET AcceptSocket,
    _Outptr_result_maybenull_ void** AcceptSocketContext,
    _Outptr_result_maybenull_ CONST WSK_CLIENT_CONNECTION_DISPATCH** AcceptSocketDispatch
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_
NTSTATUS
NTAPI
FnSockStreamSocketReceive(
    _In_opt_ void* SocketContext,
    _In_ ULONG Flags,
    _In_opt_ PWSK_DATA_INDICATION DataIndicationHead,
    _In_ SIZE_T BytesIndicated,
    _Inout_ SIZE_T* BytesAccepted
    );

PAGEDX
_IRQL_requires_max_(PASSIVE_LEVEL)
FNSOCK_STATUS
FnSockInitialize(
    VOID
    )
{
    PAGED_CODE();

    TraceInfo("FnSockInitialize");

    WskDatagramDispatch.WskReceiveFromEvent = FnSockDatagramSocketReceive;

    WskListenDispatch.WskAcceptEvent = FnSockStreamSocketAccept;

    WskConnectionDispatch.WskReceiveEvent = FnSockStreamSocketReceive;

    WskStreamDispatch.Listen = &WskListenDispatch;
    WskStreamDispatch.Connect = &WskConnectionDispatch;

    return WskClientReg();
}

PAGEDX
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
FnSockUninitialize(
    VOID
    )
{
    PAGED_CODE();

    WskClientDereg();
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
InitializeBinding(
    _In_ FNSOCK_SOCKET_BINDING* Binding,
    _In_ ADDRESS_FAMILY AddressFamily,
    _In_ ULONG WskSockType
    )
{
    Binding->AddressFamily = AddressFamily;
    Binding->SockType = WskSockType;
    Binding->ReceiveTimeout = WSKCLIENT_INFINITE;

    KeInitializeSpinLock(&Binding->AcceptLock);
    InitializeListHead(&Binding->AcceptList);
    KeInitializeEvent(&Binding->AcceptEvent, NotificationEvent, FALSE);

    KeInitializeSpinLock(&Binding->RecvDataLock);
    InitializeListHead(&Binding->RecvDataList);
    KeInitializeEvent(&Binding->RecvEvent, NotificationEvent, FALSE);

    KeInitializeSpinLock(&Binding->SendContextLock);
    InitializeListHead(&Binding->SendContextList);
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
    NTSTATUS Status;
    FNSOCK_SOCKET_BINDING* Binding = NULL;
    INT WskSockType;
    VOID* Dispatch = NULL;

    *Socket = NULL;

    Binding =
#pragma warning( suppress : 4996 )
        (FNSOCK_SOCKET_BINDING*)ExAllocatePoolWithTag(
            NonPagedPoolNx, sizeof(*Binding), POOLTAG_FNSOCK_SOCKET);
    if (Binding == NULL) {
        TraceError(
            "[data] ERROR, %s.",
            "Could not allocate memory for socket");
        Status = STATUS_UNSUCCESSFUL;
        goto Exit;
    }
    RtlZeroMemory(Binding, sizeof(*Binding));

    switch (SocketType) {
    case SOCK_DGRAM:
    case SOCK_RAW:
        WskSockType = WSK_FLAG_DATAGRAM_SOCKET;
        Dispatch = &WskDatagramDispatch;
        break;
    case SOCK_STREAM:
        WskSockType = WSK_FLAG_STREAM_SOCKET;
        Dispatch = &WskStreamDispatch;
        break;
    default:
        return STATUS_NOT_SUPPORTED;
    }

    InitializeBinding(Binding, (ADDRESS_FAMILY)AddressFamily, WskSockType);

    Status =
        WskSocketSync(
            WskSockType,
            (ADDRESS_FAMILY)AddressFamily,
            (USHORT)SocketType,
            Protocol,
            &Binding->Socket,
            Binding,
            Dispatch
            );
    if (!NT_SUCCESS(Status)) {
        TraceError(
            "[data][%p] ERROR, %u, %s.",
            Binding,
            Status,
            "WskSocketSync");
        goto Exit;
    }

    *Socket = (FNSOCK_HANDLE)Binding;

Exit:

    if (!NT_SUCCESS(Status)) {
        FnSockSocketLastError = Status;

        if (Binding != NULL) {
            ExFreePoolWithTag(Binding, POOLTAG_FNSOCK_SOCKET);
        }
    }

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
FnSockClose(
    _In_ FNSOCK_HANDLE Socket
    )
{
    NTSTATUS Status;
    FNSOCK_SOCKET_BINDING* Binding = (FNSOCK_SOCKET_BINDING*)Socket;

    while (!IsListEmpty(&Binding->SendContextList)) {
        LIST_ENTRY* Entry;
        FNSOCK_SOCKET_SEND_CONTEXT* SendContext;
        ULONG BytesSent;

        Entry = RemoveHeadList(&Binding->SendContextList);
        SendContext = CONTAINING_RECORD(Entry, FNSOCK_SOCKET_SEND_CONTEXT, Link);

        Status =
            WskSendToAwait(
                SendContext->WskClientSendContext,
                WSKCLIENT_INFINITE,
                WSKCLIENT_UNKNOWN_BYTES,
                &BytesSent);
        if (!NT_SUCCESS(Status)) {
            TraceError(
                "[data][%p] ERROR, %u, %s.",
                Binding,
                Status,
                "WskSendToAwait");
        }

        ExFreePoolWithTag(SendContext, POOLTAG_FNSOCK_SEND);
    }

    Status =
        WskCloseSocketSync(
            Binding->Socket);
    if (!NT_SUCCESS(Status)) {
        TraceError(
            "[data][%p] ERROR, %u, %s.",
            Binding,
            Status,
            "WskCloseSocketSync");
    }

    while (!IsListEmpty(&Binding->RecvDataList)) {
        LIST_ENTRY* Entry;
        FNSOCK_SOCKET_RECV_DATA* RecvData;

        Entry = RemoveHeadList(&Binding->RecvDataList);
        RecvData = CONTAINING_RECORD(Entry, FNSOCK_SOCKET_RECV_DATA, Link);
        ExFreePoolWithTag(RecvData, POOLTAG_FNSOCK_RECV);
    }

    while (!IsListEmpty(&Binding->AcceptList)) {
        LIST_ENTRY* Entry;
        FNSOCK_SOCKET_ACCEPT_CONTEXT* AcceptContext;

        Entry = RemoveHeadList(&Binding->AcceptList);
        AcceptContext = CONTAINING_RECORD(Entry, FNSOCK_SOCKET_ACCEPT_CONTEXT, Link);
        Status =
            WskCloseSocketSync(
                AcceptContext->AcceptedSocket->Socket);
        if (!NT_SUCCESS(Status)) {
            TraceError(
                "[data][%p] ERROR, %u, %s.",
                Binding,
                Status,
                "WskCloseSocketSync(accepted)");
        }
        ExFreePoolWithTag(AcceptContext->AcceptedSocket, POOLTAG_FNSOCK_SOCKET);
        ExFreePoolWithTag(AcceptContext, POOLTAG_FNSOCK_ACCEPT);
    }

    ExFreePoolWithTag(Binding, POOLTAG_FNSOCK_SOCKET);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
FNSOCK_STATUS
FnSockBind(
    _In_ FNSOCK_HANDLE Socket,
    _In_reads_bytes_(AddressLength) const struct sockaddr* Address,
    _In_ INT AddressLength
    )
{
    NTSTATUS Status;
    FNSOCK_SOCKET_BINDING* Binding = (FNSOCK_SOCKET_BINDING*)Socket;

    UNREFERENCED_PARAMETER(AddressLength);

    Status =
        WskBindSync(
            Binding->Socket,
            Binding->SockType,
            (PSOCKADDR)Address
            );
    if (!NT_SUCCESS(Status)) {
        TraceError(
            "[data][%p] ERROR, %u, %s.",
            Binding,
            Status,
            "WskBindSync");
        goto Exit;
    }

    if (Binding->SockType == WSK_FLAG_DATAGRAM_SOCKET) {
        Status =
            WskEnableCallbacks(
                Binding->Socket,
                Binding->SockType,
                WSK_EVENT_RECEIVE_FROM);
        if (!NT_SUCCESS(Status)) {
            TraceError(
                "[data][%p] ERROR, %u, %s.",
                Binding,
                Status,
                "WskEnableCallbacks");
            goto Exit;
        }
    }

    Binding->IsBound = TRUE;

Exit:

    if (!NT_SUCCESS(Status)) {
        FnSockSocketLastError = Status;
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
    NTSTATUS Status;
    FNSOCK_SOCKET_BINDING* Binding = (FNSOCK_SOCKET_BINDING*)Socket;

    UNREFERENCED_PARAMETER(AddressLength);

    Status =
        WskGetLocalAddrSync(
            Binding->Socket,
            Binding->SockType,
            Address);
    if (!NT_SUCCESS(Status)) {
        TraceError(
            "[data][%p] ERROR, %u, %s.",
            Binding,
            Status,
            "WskGetLocalAddrSync");
        goto Exit;
    }

Exit:

    if (!NT_SUCCESS(Status)) {
        FnSockSocketLastError = Status;
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
    NTSTATUS Status;
    FNSOCK_SOCKET_BINDING* Binding = (FNSOCK_SOCKET_BINDING*)Socket;

    if (Level == SOL_SOCKET && OptionName == SO_RCVTIMEO) {
        if (OptionLength < sizeof(UINT32) ||
            OptionValue == NULL ||
            *(UINT32*)OptionValue == MAXUINT32) {
            TraceError(
                "[data] ERROR, %s.",
                "SO_RCVTIMEO invalid parameter");
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }
        Binding->ReceiveTimeout = *(UINT32*)OptionValue;
        Status = STATUS_SUCCESS;
        goto Exit;
    }

    Status =
        WskControlSocketSync(
            Binding->Socket,
            Binding->SockType,
            WskSetOption,
            OptionName,
            Level,
            OptionLength,
            OptionValue,
            0,
            NULL,
            NULL);
    if (!NT_SUCCESS(Status)) {
        TraceError(
            "[data][%p] ERROR, %u, %s.",
            Binding,
            Status,
            "WskControlSocketSync");
        goto Exit;
    }

Exit:

    if (!NT_SUCCESS(Status)) {
        FnSockSocketLastError = Status;
    }

    return Status;
}

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
    NTSTATUS Status;
    FNSOCK_SOCKET_BINDING* Binding = (FNSOCK_SOCKET_BINDING*)Socket;
    SIZE_T OutputSizeReturned;

    if (Level == SOL_SOCKET && OptionName == SO_RCVTIMEO) {
        if (*OptionLength < sizeof(UINT32) ||
            OptionValue == NULL) {
            TraceError(
                "[data] ERROR, %s.",
                "SO_RCVTIMEO invalid parameter");
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }
        *(UINT32*)OptionValue = Binding->ReceiveTimeout;
        *OptionLength = sizeof(UINT32);
        Status = STATUS_SUCCESS;
        goto Exit;
    }

    Status =
        WskControlSocketSync(
            Binding->Socket,
            Binding->SockType,
            WskGetOption,
            OptionName,
            Level,
            0,
            NULL,
            *OptionLength,
            OptionValue,
            &OutputSizeReturned);
    if (!NT_SUCCESS(Status)) {
        TraceError(
            "[data][%p] ERROR, %u, %s.",
            Binding,
            Status,
            "WskControlSocketSync");
        goto Exit;
    }

    *OptionLength = OutputSizeReturned;

Exit:

    if (!NT_SUCCESS(Status)) {
        FnSockSocketLastError = Status;
    }

    return Status;
}

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
    NTSTATUS Status;
    FNSOCK_SOCKET_BINDING* Binding = (FNSOCK_SOCKET_BINDING*)Socket;
    SIZE_T OutputSizeReturned = 0;

    Status =
        WskControlSocketSync(
            Binding->Socket,
            Binding->SockType,
            WskIoctl,
            ControlCode,
            0,
            InputLength,
            Input,
            OutputLength,
            Output,
            &OutputSizeReturned);
    if (!NT_SUCCESS(Status)) {
        TraceError(
            "[data][%p] ERROR, %u, %s.",
            Binding,
            Status,
            "WskControlSocketSync");
        goto Exit;
    }

Exit:

    *BytesReturned = (ULONG)OutputSizeReturned;

    if (!NT_SUCCESS(Status)) {
        FnSockSocketLastError = Status;
    }

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
FNSOCK_STATUS
FnSockListen(
    _In_ FNSOCK_HANDLE Socket,
    _In_ ULONG Backlog
    )
{
    NTSTATUS Status;
    FNSOCK_SOCKET_BINDING* Binding = (FNSOCK_SOCKET_BINDING*)Socket;

    UNREFERENCED_PARAMETER(Backlog);

    Status =
        WskListenSync(
            Binding->Socket,
            Binding->SockType);
    if (!NT_SUCCESS(Status)) {
        TraceError(
            "[data][%p] ERROR, %u, %s.",
            Binding,
            Status,
            "WskListenSync");
        goto Exit;
    }

    Status =
        WskEnableCallbacks(
            Binding->Socket,
            Binding->SockType,
            WSK_EVENT_ACCEPT);
    if (!NT_SUCCESS(Status)) {
        TraceError(
            "[data][%p] ERROR, %u, %s.",
            Binding,
            Status,
            "WskEnableCallbacks");
        goto Exit;
    }

Exit:

    if (!NT_SUCCESS(Status)) {
        FnSockSocketLastError = Status;
    }

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
FNSOCK_HANDLE
FnSockAccept(
    _In_ FNSOCK_HANDLE Socket,
    _Out_writes_bytes_(*AddressLength) struct sockaddr* Address,
    _Inout_ INT* AddressLength
    )
{
    NTSTATUS Status;
    FNSOCK_SOCKET_BINDING* Binding = (FNSOCK_SOCKET_BINDING*)Socket;
    FNSOCK_SOCKET_BINDING* NewBinding = NULL;
    LIST_ENTRY* Entry;
    FNSOCK_SOCKET_ACCEPT_CONTEXT* AcceptContext = NULL;
    KIRQL PrevIrql;

    UNREFERENCED_PARAMETER(Address);
    UNREFERENCED_PARAMETER(AddressLength);

    KeAcquireSpinLock(&Binding->AcceptLock, &PrevIrql);

    if (IsListEmpty(&Binding->AcceptList)) {
        LARGE_INTEGER Timeout;
        LARGE_INTEGER* TimeoutPtr;

        KeReleaseSpinLock(&Binding->AcceptLock, PrevIrql);

        if (Binding->ReceiveTimeout == WSKCLIENT_INFINITE) {
            TimeoutPtr = NULL;
        } else {
            Timeout.QuadPart = UInt32x32To64(Binding->ReceiveTimeout, 10000);
            Timeout.QuadPart = -Timeout.QuadPart;
            TimeoutPtr = &Timeout;
        }

        Status = KeWaitForSingleObject(&Binding->AcceptEvent, Executive, KernelMode, FALSE, TimeoutPtr);
        if (!NT_SUCCESS(Status)) {
            TraceError(
                "[data][%p] ERROR, %u, %s.",
                Binding,
                Status,
                "KeWaitForSingleObject");
            goto Exit;
        }

        KeAcquireSpinLock(&Binding->AcceptLock, &PrevIrql);

        if (IsListEmpty(&Binding->AcceptList)) {
            KeReleaseSpinLock(&Binding->AcceptLock, PrevIrql);
            Status = STATUS_UNSUCCESSFUL;
            goto Exit;
        }
    }

    Entry = RemoveHeadList(&Binding->AcceptList);

    KeReleaseSpinLock(&Binding->AcceptLock, PrevIrql);

    AcceptContext = CONTAINING_RECORD(Entry, FNSOCK_SOCKET_ACCEPT_CONTEXT, Link);

    NewBinding = AcceptContext->AcceptedSocket;

    Status =
        WskEnableCallbacks(
            NewBinding->Socket,
            NewBinding->SockType,
            WSK_EVENT_RECEIVE);
    if (!NT_SUCCESS(Status)) {
        TraceError(
            "[data][%p] ERROR, %u, %s.",
            NewBinding,
            Status,
            "WskEnableCallbacks");
        goto Exit;
    }

    Status = STATUS_SUCCESS;

Exit:

    if (AcceptContext != NULL) {
        ExFreePoolWithTag(AcceptContext, POOLTAG_FNSOCK_ACCEPT);
    }

    if (!NT_SUCCESS(Status)) {
        FnSockSocketLastError = Status;

        if (NewBinding != NULL) {
            FnSockClose((FNSOCK_HANDLE)NewBinding);
            NewBinding = NULL;
        }
    }

    return (FNSOCK_HANDLE)NewBinding;
}

FNSOCK_STATUS
FnSockConnect(
    _In_ FNSOCK_HANDLE Socket,
    _In_reads_bytes_(AddressLength) struct sockaddr* Address,
    _In_ INT AddressLength
    )
{
    NTSTATUS Status;
    FNSOCK_SOCKET_BINDING* Binding = (FNSOCK_SOCKET_BINDING*)Socket;

    UNREFERENCED_PARAMETER(AddressLength);

    //
    // Bind to wildcard address if not bound already.
    //
    if (!Binding->IsBound) {
        SOCKADDR_INET WildcardAddress = {0};
        WildcardAddress.si_family = Binding->AddressFamily;

        Status =
            FnSockBind(
                Socket,
                (struct sockaddr*)&WildcardAddress,
                sizeof(WildcardAddress));
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }
    }

    Status =
        WskConnectSync(
            Binding->Socket,
            Binding->SockType,
            Address);
    if (!NT_SUCCESS(Status)) {
        TraceError(
            "[data][%p] ERROR, %u, %s.",
            Binding,
            Status,
            "WskConnectSync");
        goto Exit;
    }

    if (Binding->SockType == WSK_FLAG_STREAM_SOCKET) {
        Status =
            WskEnableCallbacks(
                Binding->Socket,
                Binding->SockType,
                WSK_EVENT_RECEIVE);
        if (!NT_SUCCESS(Status)) {
            TraceError(
                "[data][%p] ERROR, %u, %s.",
                Binding,
                Status,
                "WskEnableCallbacks");
            goto Exit;
        }
    }

Exit:

    if (!NT_SUCCESS(Status)) {
        FnSockSocketLastError = Status;
    }

    return Status;
}

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
    NTSTATUS Status;
    FNSOCK_SOCKET_BINDING* Binding = (FNSOCK_SOCKET_BINDING*)Socket;
    FNSOCK_SOCKET_SEND_CONTEXT* SendContext;
    INT BytesSent = 0;
    KIRQL PrevIrql;

    UNREFERENCED_PARAMETER(Flags);

    SendContext =
#pragma warning( suppress : 4996 )
        (FNSOCK_SOCKET_SEND_CONTEXT*)ExAllocatePoolWithTag(
            NonPagedPoolNx, sizeof(*SendContext), POOLTAG_FNSOCK_SEND);
    if (SendContext == NULL) {
        TraceError(
            "[data] ERROR, %s.",
            "Could not allocate memory for socket send");
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }
    RtlZeroMemory(SendContext, sizeof(*SendContext));

    //
    // To emulate the fire and forget behavior of user mode WinSock with WSK,
    // allocate a send context, initiate a send and clean up the send context
    // later when the socket is closed. Accumulating send contexts throughout
    // the lifetime of the socket is not ideal and this emulation can be
    // improved upon if needed.
    //
    Status =
        WskSendAsync(
            Binding->Socket,
            Binding->SockType,
            (char *)Buffer,
            BufferLength,
            BufferIsNonPagedPool,
            0,
            &SendContext->WskClientSendContext);
    if (!NT_SUCCESS(Status)) {
        TraceError(
            "[data][%p] ERROR, %u, %s.",
            Binding,
            Status,
            "WskSendSync");
        goto Exit;
    }

    KeAcquireSpinLock(&Binding->SendContextLock, &PrevIrql);
    InsertTailList(&Binding->SendContextList, &SendContext->Link);
    KeReleaseSpinLock(&Binding->SendContextLock, PrevIrql);

    Status = STATUS_SUCCESS;
    BytesSent = BufferLength;

Exit:

    if (!NT_SUCCESS(Status)) {
        FnSockSocketLastError = Status;

        BytesSent = -1;

        if (SendContext != NULL) {
            ExFreePoolWithTag(SendContext, POOLTAG_FNSOCK_SEND);
        }
    }

    return BytesSent;
}

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
    NTSTATUS Status;
    FNSOCK_SOCKET_BINDING* Binding = (FNSOCK_SOCKET_BINDING*)Socket;
    FNSOCK_SOCKET_SEND_CONTEXT* SendContext;
    INT BytesSent = 0;
    KIRQL PrevIrql;

    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(AddressLength);

    SendContext =
#pragma warning( suppress : 4996 )
        (FNSOCK_SOCKET_SEND_CONTEXT*)ExAllocatePoolWithTag(
            NonPagedPoolNx, sizeof(*SendContext), POOLTAG_FNSOCK_SEND);
    if (SendContext == NULL) {
        TraceError(
            "[data] ERROR, %s.",
            "Could not allocate memory for socket send");
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }
    RtlZeroMemory(SendContext, sizeof(*SendContext));

    //
    // To emulate the fire and forget behavior of user mode WinSock with WSK,
    // allocate a send context, initiate a send and clean up the send context
    // later when the socket is closed. Accumulating send contexts throughout
    // the lifetime of the socket is not ideal and this emulation can be
    // improved upon if needed.
    //
    Status =
        WskSendToAsync(
            Binding->Socket,
            (char *)Buffer,
            BufferLength,
            BufferIsNonPagedPool,
            (PSOCKADDR)Address,
            0,
            NULL,
            &SendContext->WskClientSendContext);
    if (!NT_SUCCESS(Status)) {
        TraceError(
            "[data][%p] ERROR, %u, %s.",
            Binding,
            Status,
            "WskSendToSync");
        goto Exit;
    }

    KeAcquireSpinLock(&Binding->SendContextLock, &PrevIrql);
    InsertTailList(&Binding->SendContextList, &SendContext->Link);
    KeReleaseSpinLock(&Binding->SendContextLock, PrevIrql);

    Status = STATUS_SUCCESS;
    BytesSent = BufferLength;

Exit:

    if (!NT_SUCCESS(Status)) {
        FnSockSocketLastError = Status;

        BytesSent = -1;

        if (SendContext != NULL) {
            ExFreePoolWithTag(SendContext, POOLTAG_FNSOCK_SEND);
        }
    }

    return BytesSent;
}

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
    FNSOCK_SOCKET_BINDING* Binding = (FNSOCK_SOCKET_BINDING*)Socket;
    LIST_ENTRY* Entry;
    FNSOCK_SOCKET_RECV_DATA* RecvData;
    KIRQL PrevIrql;
    INT BytesReceived = 0;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(BufferIsNonPagedPool);
    UNREFERENCED_PARAMETER(Flags);

    KeAcquireSpinLock(&Binding->RecvDataLock, &PrevIrql);

    if (IsListEmpty(&Binding->RecvDataList)) {
        LARGE_INTEGER Timeout;
        LARGE_INTEGER* TimeoutPtr;

        KeReleaseSpinLock(&Binding->RecvDataLock, PrevIrql);

        if (Binding->ReceiveTimeout == WSKCLIENT_INFINITE) {
            TimeoutPtr = NULL;
        } else {
            Timeout.QuadPart = UInt32x32To64(Binding->ReceiveTimeout, 10000);
            Timeout.QuadPart = -Timeout.QuadPart;
            TimeoutPtr = &Timeout;
        }

        Status = KeWaitForSingleObject(&Binding->RecvEvent, Executive, KernelMode, FALSE, TimeoutPtr);
        if (!NT_SUCCESS(Status)) {
            TraceError(
                "[data][%p] ERROR, %u, %s.",
                Binding,
                Status,
                "KeWaitForSingleObject");
            goto Exit;
        }

        KeAcquireSpinLock(&Binding->RecvDataLock, &PrevIrql);

        if (IsListEmpty(&Binding->RecvDataList)) {
            KeReleaseSpinLock(&Binding->RecvDataLock, PrevIrql);
            Status = STATUS_UNSUCCESSFUL;
            goto Exit;
        }
    }

    Entry = RemoveHeadList(&Binding->RecvDataList);

    KeReleaseSpinLock(&Binding->RecvDataLock, PrevIrql);

    RecvData = CONTAINING_RECORD(Entry, FNSOCK_SOCKET_RECV_DATA, Link);
    // TODO: Iterate through the received data list to fill buffers for stream sockets.
    if (RecvData->DataLength <= (ULONG)BufferLength) {
        BytesReceived = RecvData->DataLength;
        Status = STATUS_SUCCESS;
    } else {
        Status = STATUS_BUFFER_OVERFLOW;
    }
    RtlCopyMemory(Buffer, RecvData->Data, min(RecvData->DataLength, (ULONG)BufferLength));

    ExFreePoolWithTag(RecvData, POOLTAG_FNSOCK_RECV);

Exit:

    if (!NT_SUCCESS(Status)) {
        FnSockSocketLastError = Status;

        BytesReceived = -1;
    }

    return BytesReceived;
}

static
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
QueueRecvData(
    _In_ FNSOCK_SOCKET_BINDING* Binding,
    _In_ WSK_BUF* Buffer
    )
{
    PMDL Mdl = Buffer->Mdl;
    ULONG MdlOffset = Buffer->Offset;
    SIZE_T DataLength = Buffer->Length;
    SIZE_T CopiedLength;

    if (Mdl == NULL || DataLength == 0) {
        TraceWarn(
            "[%p] Dropping data with empty mdl.",
            Binding);
        return;
    }

    //
    // We require contiguous buffers.
    //
    if ((SIZE_T)DataLength > Mdl->ByteCount - MdlOffset) {
        TraceWarn(
            "[%p] Dropping data with fragmented MDL.",
            Binding);
        NT_ASSERT(FALSE);
        return;
    }

    UCHAR* Va = MmGetSystemAddressForMdlSafe(Mdl, NormalPagePriority | MdlMappingNoExecute);
    if (Va == NULL) {
        TraceError(
            "[%p] Dropping data due to failed mapping.",
            Binding);
        return;
    }

    FNSOCK_SOCKET_RECV_DATA* RecvData = NULL;
    SIZE_T AllocSize = sizeof(*RecvData) + DataLength;
    RecvData =
#pragma warning( suppress : 4996 )
        (FNSOCK_SOCKET_RECV_DATA*)ExAllocatePoolWithTag(
            NonPagedPoolNx, AllocSize, POOLTAG_FNSOCK_RECV);
    if (RecvData == NULL) {
        TraceError(
            "[%p] Dropping data due to insufficient memory.",
            Binding);
        return;
    }
    RtlZeroMemory(RecvData, AllocSize);

    RecvData->DataLength = (ULONG)DataLength;
    CopiedLength = 0;
    while (CopiedLength < DataLength && Mdl != NULL) {
        SIZE_T CopySize = min(MmGetMdlByteCount(Mdl) - MdlOffset, DataLength - CopiedLength);
        RtlCopyMemory(
            RecvData->Data + CopiedLength,
            Va + MdlOffset,
            CopySize);
        CopiedLength += CopySize;

        Mdl = Mdl->Next;
        MdlOffset = 0;
    }

    KIRQL PrevIrql;
    KeAcquireSpinLock(&Binding->RecvDataLock, &PrevIrql);
    InsertTailList(&Binding->RecvDataList, &RecvData->Link);
    KeSetEvent(&Binding->RecvEvent, EVENT_INCREMENT, FALSE);
    KeReleaseSpinLock(&Binding->RecvDataLock, PrevIrql);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_
NTSTATUS
NTAPI
FnSockDatagramSocketReceive(
    _In_opt_ void* SocketContext,
    _In_ ULONG Flags,
    _In_opt_ PWSK_DATAGRAM_INDICATION DataIndicationHead
    )
{
    FNSOCK_SOCKET_BINDING* Binding = (FNSOCK_SOCKET_BINDING*)SocketContext;

    UNREFERENCED_PARAMETER(Flags);

    NT_ASSERT(Binding != NULL);
    if (Binding == NULL) {
        TraceError(
            "[%p] Unexpected context in WSK callback.",
            Binding);
        return STATUS_SUCCESS;
    }

    //
    // Check to see if the DataIndicate is NULL, which indicates that the
    // socket has been closed
    //
    if (DataIndicationHead == NULL) {
        TraceWarn(
            "[%p] Unexpected socket state in WSK callback.",
            Binding);
        return STATUS_SUCCESS;
    }

    //
    // Process all the data indicated by the callback.
    //
    while (DataIndicationHead != NULL) {
        PWSK_DATAGRAM_INDICATION DataIndication = DataIndicationHead;

        DataIndicationHead = DataIndicationHead->Next;
        DataIndication->Next = NULL;

        TraceInfo(
            "[data][%p] Recv %u bytes",
            Binding,
            (ULONG)DataIndication->Buffer.Length);

        QueueRecvData(Binding, &DataIndication->Buffer);
    }

    return STATUS_SUCCESS;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_
_At_(AcceptSocket, __drv_aliasesMem)
NTSTATUS
NTAPI
FnSockStreamSocketAccept(
    _In_opt_ void* SocketContext,
    _In_ ULONG Flags,
    _In_ PSOCKADDR LocalAddress,
    _In_ PSOCKADDR RemoteAddress,
    _In_opt_ PWSK_SOCKET AcceptSocket,
    _Outptr_result_maybenull_ void** AcceptSocketContext,
    _Outptr_result_maybenull_ CONST WSK_CLIENT_CONNECTION_DISPATCH** AcceptSocketDispatch
    )
{
    NTSTATUS Status;
    FNSOCK_SOCKET_BINDING* Binding = (FNSOCK_SOCKET_BINDING*)SocketContext;
    FNSOCK_SOCKET_BINDING* NewBinding = NULL;
    FNSOCK_SOCKET_ACCEPT_CONTEXT* AcceptContext = NULL;
    KIRQL PrevIrql;

    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(LocalAddress);
    UNREFERENCED_PARAMETER(RemoteAddress);

    *AcceptSocketContext = NULL;
    *AcceptSocketDispatch = NULL;

    NT_ASSERT(Binding != NULL);
    if (Binding == NULL) {
        TraceError(
            "[%p] Unexpected context in WSK callback.",
            Binding);
        return STATUS_SUCCESS;
    }

    //
    // If AcceptSocket is NULL, we are supposed to close the listening socket.
    //
    NT_ASSERT(AcceptSocket != NULL);

    NewBinding =
#pragma warning( suppress : 4996 )
        (FNSOCK_SOCKET_BINDING*)ExAllocatePoolWithTag(
            NonPagedPoolNx, sizeof(*NewBinding), POOLTAG_FNSOCK_SOCKET);
    if (NewBinding == NULL) {
        TraceError(
            "[data] ERROR, %s.",
            "Could not allocate memory for socket");
        Status = STATUS_UNSUCCESSFUL;
        goto Exit;
    }
    RtlZeroMemory(NewBinding, sizeof(*NewBinding));

    AcceptContext =
#pragma warning( suppress : 4996 )
        (FNSOCK_SOCKET_ACCEPT_CONTEXT*)ExAllocatePoolWithTag(
            NonPagedPoolNx, sizeof(*AcceptContext), POOLTAG_FNSOCK_ACCEPT);
    if (AcceptContext == NULL) {
        TraceError(
            "[data] ERROR, %s.",
            "Could not allocate memory for accept context");
        Status = STATUS_UNSUCCESSFUL;
        goto Exit;
    }
    RtlZeroMemory(AcceptContext, sizeof(*AcceptContext));

    InitializeBinding(NewBinding, Binding->AddressFamily, WSK_FLAG_CONNECTION_SOCKET);
    NewBinding->Socket = AcceptSocket;
    NewBinding->IsBound = TRUE;

    AcceptContext->AcceptedSocket = NewBinding;

    KeAcquireSpinLock(&Binding->AcceptLock, &PrevIrql);
    InsertTailList(&Binding->AcceptList, &AcceptContext->Link);
    KeSetEvent(&Binding->AcceptEvent, EVENT_INCREMENT, FALSE);
    KeReleaseSpinLock(&Binding->AcceptLock, PrevIrql);

    *AcceptSocketContext = NewBinding;
    *AcceptSocketDispatch = &WskConnectionDispatch;

    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        if (AcceptContext != NULL) {
            ExFreePoolWithTag(AcceptContext, POOLTAG_FNSOCK_ACCEPT);
        }
        if (NewBinding != NULL) {
            ExFreePoolWithTag(NewBinding, POOLTAG_FNSOCK_SOCKET);
        }
    }

    return Status;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_
NTSTATUS
NTAPI
FnSockStreamSocketReceive(
    _In_opt_ void* SocketContext,
    _In_ ULONG Flags,
    _In_opt_ PWSK_DATA_INDICATION DataIndicationHead,
    _In_ SIZE_T BytesIndicated,
    _Inout_ SIZE_T* BytesAccepted
    )
{
    FNSOCK_SOCKET_BINDING* Binding = (FNSOCK_SOCKET_BINDING*)SocketContext;

    UNREFERENCED_PARAMETER(Flags);

    NT_ASSERT(Binding != NULL);
    if (Binding == NULL) {
        TraceError(
            "[%p] Unexpected context in WSK callback.",
            Binding);
        return STATUS_SUCCESS;
    }

    //
    // Check to see if the DataIndicate is NULL, which indicates that the
    // socket has been closed
    //
    if (DataIndicationHead == NULL) {
        return STATUS_SUCCESS;
    }

    //
    // Process all the data indicated by the callback.
    //
    while (DataIndicationHead != NULL) {
        PWSK_DATA_INDICATION DataIndication = DataIndicationHead;

        DataIndicationHead = DataIndicationHead->Next;
        DataIndication->Next = NULL;

        TraceInfo(
            "[data][%p] Recv %u bytes",
            Binding,
            (ULONG)DataIndication->Buffer.Length);

        QueueRecvData(Binding, &DataIndication->Buffer);
    }

    *BytesAccepted = BytesIndicated;

    return STATUS_SUCCESS;
}

#ifndef NO_ERROR
#define NO_ERROR 0
#endif

#ifndef ERROR_IO_PENDING
#define ERROR_IO_PENDING 997L
#endif

static
INT
NtStatusToSocketError(
    _In_ NTSTATUS Status
    )
{
    INT err;

    switch (Status) {

    case STATUS_PENDING:
        err = ERROR_IO_PENDING;
        break;

    case STATUS_INVALID_HANDLE:
    case STATUS_OBJECT_TYPE_MISMATCH:
        err = WSAENOTSOCK;
        break;

    case STATUS_INSUFFICIENT_RESOURCES:
    case STATUS_PAGEFILE_QUOTA:
    case STATUS_COMMITMENT_LIMIT:
    case STATUS_WORKING_SET_QUOTA:
    case STATUS_NO_MEMORY:
    case STATUS_CONFLICTING_ADDRESSES:
    case STATUS_QUOTA_EXCEEDED:
    case STATUS_TOO_MANY_PAGING_FILES:
    case STATUS_REMOTE_RESOURCES:
    case STATUS_TOO_MANY_ADDRESSES:
        err = WSAENOBUFS;
        break;

    case STATUS_SHARING_VIOLATION:
    case STATUS_ADDRESS_ALREADY_EXISTS:
        err = WSAEADDRINUSE;
        break;

    case STATUS_LINK_TIMEOUT:
    case STATUS_IO_TIMEOUT:
    case STATUS_TIMEOUT:
        err = WSAETIMEDOUT;
        break;

    case STATUS_GRACEFUL_DISCONNECT:
        err = WSAEDISCON;
        break;

    case STATUS_REMOTE_DISCONNECT:
    case STATUS_CONNECTION_RESET:
    case STATUS_LINK_FAILED:
    case STATUS_CONNECTION_DISCONNECTED:
    case STATUS_PORT_UNREACHABLE:
        err = WSAECONNRESET;
        break;

    case STATUS_HOPLIMIT_EXCEEDED:
        err = WSAENETRESET;
        break;

    case STATUS_LOCAL_DISCONNECT:
    case STATUS_TRANSACTION_ABORTED:
    case STATUS_CONNECTION_ABORTED:
        err = WSAECONNABORTED;
        break;

    case STATUS_BAD_NETWORK_PATH:
    case STATUS_NETWORK_UNREACHABLE:
    case STATUS_PROTOCOL_UNREACHABLE:
        err = WSAENETUNREACH;
        break;

    case STATUS_HOST_UNREACHABLE:
        err = WSAEHOSTUNREACH;
        break;

    case STATUS_HOST_DOWN:
        err = WSAEHOSTDOWN;
        break;

    case STATUS_CANCELLED:
    case STATUS_REQUEST_ABORTED:
        err = WSAEINTR;
        break;

    case STATUS_BUFFER_OVERFLOW:
    case STATUS_INVALID_BUFFER_SIZE:
        err = WSAEMSGSIZE;
        break;

    case STATUS_BUFFER_TOO_SMALL:
    case STATUS_ACCESS_VIOLATION:
    case STATUS_DATATYPE_MISALIGNMENT_ERROR:
        err = WSAEFAULT;
        break;

    case STATUS_DEVICE_NOT_READY:
    case STATUS_REQUEST_NOT_ACCEPTED:
        err = WSAEWOULDBLOCK;
        break;

    case STATUS_INVALID_NETWORK_RESPONSE:
    case STATUS_NETWORK_BUSY:
    case STATUS_NO_SUCH_DEVICE:
    case STATUS_NO_SUCH_FILE:
    case STATUS_OBJECT_PATH_NOT_FOUND:
    case STATUS_OBJECT_NAME_NOT_FOUND:
    case STATUS_UNEXPECTED_NETWORK_ERROR:
        err = WSAENETDOWN;
        break;

    case STATUS_INVALID_CONNECTION:
        err = WSAENOTCONN;
        break;

    case STATUS_REMOTE_NOT_LISTENING:
    case STATUS_CONNECTION_REFUSED:
        err = WSAECONNREFUSED;
        break;

    case STATUS_PIPE_DISCONNECTED:
        err = WSAESHUTDOWN;
        break;

    case STATUS_INVALID_ADDRESS:
    case STATUS_INVALID_ADDRESS_COMPONENT:
        err = WSAEADDRNOTAVAIL;
        break;

    case STATUS_NOT_SUPPORTED:
    case STATUS_NOT_IMPLEMENTED:
        err = WSAEOPNOTSUPP;
        break;

    case STATUS_ACCESS_DENIED:
        err = WSAEACCES;
        break;

    case STATUS_CONNECTION_ACTIVE:
        err = WSAEISCONN;
        break;

    case STATUS_PROTOCOL_NOT_SUPPORTED:
        return WSAEAFNOSUPPORT;

    case STATUS_SUCCESS:
        return NO_ERROR;

    default:

        if (NT_SUCCESS(Status)) {
            NT_ASSERT(FALSE);

            err = NO_ERROR;
            break;
        }

        //
        // Fall over to default error code
        //
    case STATUS_UNSUCCESSFUL:
    case STATUS_INVALID_PARAMETER:
    case STATUS_ADDRESS_CLOSED:
    case STATUS_CONNECTION_INVALID:
    case STATUS_ADDRESS_ALREADY_ASSOCIATED:
    case STATUS_ADDRESS_NOT_ASSOCIATED:
    case STATUS_INVALID_DEVICE_STATE:
    case STATUS_INVALID_DEVICE_REQUEST:
        err = WSAEINVAL;
        break;

    }

    return err;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
INT
FnSockGetLastError(
    VOID
    )
{
    return NtStatusToSocketError(FnSockSocketLastError);
}
