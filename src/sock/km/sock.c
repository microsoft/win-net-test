//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

//
// Implements a user mode WinSock like sockets API with WSK. The main pieces
// needed to bridge the gap between user mode and kernel mode WinSock are fire
// and forget TX behavior and RX buffering behavior. This module implements
// both of those pieces and leverages wskclient for the basic WSK interactions.
//

#include <ntddk.h>
#include <wsk.h>

#include "fnsock.h"
#include "trace.h"
#include "wskclient.h"

#include "sock.tmh"

#define FNSOCK_POOL_SOCKET 'kSsF' // FsSk
#define FNSOCK_POOL_SOCKET_SEND 'sSsF' // FsSs
#define FNSOCK_POOL_SOCKET_RECV 'rSsF' // FsSr

typedef struct _WSK_DATAGRAM_SOCKET {
    const WSK_PROVIDER_DATAGRAM_DISPATCH* Dispatch;
} WSK_DATAGRAM_SOCKET, * PWSK_DATAGRAM_SOCKET;

typedef struct FNSOCK_SOCKET_BINDING {
    //
    // UDP socket used for sending/receiving datagrams.
    //
    union {
        PWSK_SOCKET Socket;
        PWSK_DATAGRAM_SOCKET DgrmSocket;
    };

    ULONG SockType;

    KSPIN_LOCK SendContextLock;
    LIST_ENTRY SendContextList;

    KSPIN_LOCK RecvDataLock;
    LIST_ENTRY RecvDataList;

    UINT32 ReceiveTimeout;
} FNSOCK_SOCKET_BINDING;

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

static NTSTATUS FnSockSocketLastError;

_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_
NTSTATUS
NTAPI
FnSockDatagramSocketReceive(
    _In_opt_ void* Context,
    _In_ ULONG Flags,
    _In_opt_ PWSK_DATAGRAM_INDICATION DataIndicationHead
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
            NonPagedPoolNx, sizeof(*Binding), FNSOCK_POOL_SOCKET);
    if (Binding == NULL) {
        TraceError(
            "[data] ERROR, %s.",
            "Could not allocate memory for socket");
        Status = STATUS_UNSUCCESSFUL;
        goto Exit;
    }

    switch (SocketType) {
    case SOCK_DGRAM:
        WskSockType = WSK_FLAG_DATAGRAM_SOCKET;
        Dispatch = &WskDatagramDispatch;
        break;
    case SOCK_STREAM:
        WskSockType = WSK_FLAG_STREAM_SOCKET;
        break;
    default:
        return STATUS_NOT_SUPPORTED;
    }

    Binding->SockType = WskSockType;
    Binding->ReceiveTimeout = WSKCLIENT_INFINITE;

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

    KeInitializeSpinLock(&Binding->RecvDataLock);
    InitializeListHead(&Binding->RecvDataList);
    KeInitializeSpinLock(&Binding->SendContextLock);
    InitializeListHead(&Binding->SendContextList);

    *Socket = (FNSOCK_HANDLE)Binding;

Exit:

    if (!NT_SUCCESS(Status)) {
        FnSockSocketLastError = Status;

        if (Binding != NULL) {
            ExFreePoolWithTag(Binding, FNSOCK_POOL_SOCKET);
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

        ExFreePoolWithTag(SendContext, FNSOCK_POOL_SOCKET_SEND);
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
        ExFreePoolWithTag(RecvData, FNSOCK_POOL_SOCKET_RECV);
    }

    ExFreePoolWithTag(Binding, FNSOCK_POOL_SOCKET);
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
            NonPagedPoolNx, sizeof(*SendContext), FNSOCK_POOL_SOCKET_SEND);
    if (SendContext == NULL) {
        TraceError(
            "[data] ERROR, %s.",
            "Could not allocate memory for socket send");
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

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
            ExFreePoolWithTag(SendContext, FNSOCK_POOL_SOCKET_SEND);
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
    KIRQL PrevIrql;
    INT BytesReceived = 0;
    NTSTATUS Status = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(Flags);

    KeAcquireSpinLock(&Binding->RecvDataLock, &PrevIrql);

    if (!IsListEmpty(&Binding->RecvDataList)) {
        LIST_ENTRY* Entry;
        FNSOCK_SOCKET_RECV_DATA* RecvData;

        Entry = RemoveHeadList(&Binding->RecvDataList);

        KeReleaseSpinLock(&Binding->RecvDataLock, PrevIrql);

        RecvData = CONTAINING_RECORD(Entry, FNSOCK_SOCKET_RECV_DATA, Link);
        if (RecvData->DataLength <= (ULONG)BufferLength) {
            BytesReceived = RecvData->DataLength;
            Status = STATUS_SUCCESS;
        } else {
            Status = STATUS_BUFFER_OVERFLOW;
        }
        RtlCopyMemory(Buffer, RecvData->Data, min(RecvData->DataLength, (ULONG)BufferLength));

        ExFreePoolWithTag(RecvData, FNSOCK_POOL_SOCKET_RECV);
    } else {
        KeReleaseSpinLock(&Binding->RecvDataLock, PrevIrql);

        Status =
            WskReceiveSync(
                Binding->Socket,
                Binding->SockType,
                Buffer,
                BufferLength,
                BufferIsNonPagedPool,
                WSKCLIENT_UNKNOWN_BYTES,
                Binding->ReceiveTimeout,
                (ULONG *)&BytesReceived);
        if (!NT_SUCCESS(Status)) {
            TraceError(
                "[data][%p] ERROR, %u, %s.",
                Binding,
                Status,
                "WskReceiveSync");
            goto Exit;
        }
    }

Exit:

    if (!NT_SUCCESS(Status)) {
        FnSockSocketLastError = Status;

        BytesReceived = -1;
    }

    return BytesReceived;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_
NTSTATUS
NTAPI
FnSockDatagramSocketReceive(
    _In_opt_ void* Context,
    _In_ ULONG Flags,
    _In_opt_ PWSK_DATAGRAM_INDICATION DataIndicationHead
    )
{
    FNSOCK_SOCKET_BINDING* Binding = (FNSOCK_SOCKET_BINDING*)Context;

    NT_ASSERT(Context);
    UNREFERENCED_PARAMETER(Flags);

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

        PWSK_DATAGRAM_INDICATION DataIndication = DataIndicationHead;
        DataIndicationHead = DataIndicationHead->Next;
        DataIndication->Next = NULL;

        if (DataIndication->Buffer.Mdl == NULL ||
            DataIndication->Buffer.Length == 0) {
            TraceWarn(
                "[%p] Dropping datagram with empty mdl.",
                Binding);
            continue;
        }

        PMDL Mdl = DataIndication->Buffer.Mdl;
        ULONG MdlOffset = DataIndication->Buffer.Offset;
        SIZE_T DataLength = DataIndication->Buffer.Length;
        SIZE_T CopiedLength;

        TraceInfo(
            "[data][%p] Recv %u bytes",
            Binding,
            (ULONG)DataLength);

        //
        // We require contiguous buffers.
        //
        if ((SIZE_T)DataLength > Mdl->ByteCount - MdlOffset) {
            TraceWarn(
                "[%p] Dropping datagram with fragmented MDL.",
                Binding);
            NT_ASSERT(FALSE);
            continue;
        }

        FNSOCK_SOCKET_RECV_DATA* RecvData = NULL;
        RecvData =
#pragma warning( suppress : 4996 )
            (FNSOCK_SOCKET_RECV_DATA*)ExAllocatePoolWithTag(
                NonPagedPoolNx, sizeof(*RecvData) + DataLength, FNSOCK_POOL_SOCKET_RECV);
        if (RecvData == NULL) {
            TraceError(
                "[%p] Dropping datagram due to insufficient memory.",
                Binding);
            continue;
        }

        RecvData->DataLength = (ULONG)DataLength;
        CopiedLength = 0;
        while (CopiedLength < DataLength && Mdl != NULL) {
            SIZE_T CopySize = min(MmGetMdlByteCount(Mdl) - MdlOffset, DataLength - CopiedLength);
            RtlCopyMemory(
                RecvData->Data + CopiedLength,
                (UCHAR*)Mdl->MappedSystemVa + MdlOffset,
                CopySize);
            CopiedLength += CopySize;

            Mdl = Mdl->Next;
            MdlOffset = 0;
        }

        KIRQL PrevIrql;
        KeAcquireSpinLock(&Binding->RecvDataLock, &PrevIrql);
        InsertTailList(&Binding->RecvDataList, &RecvData->Link);
        KeReleaseSpinLock(&Binding->RecvDataLock, PrevIrql);
    }

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
