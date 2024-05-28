//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include <ntddk.h>
#include <wsk.h>

#include "fnsock.h"
#include "trace.h"

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

    //
    // Event used to wait for completion of socket functions.
    //
    KEVENT WskCompletionEvent;

    //
    // IRP used for socket functions.
    //
    union {
        IRP Irp;
        UCHAR IrpBuffer[sizeof(IRP) + sizeof(IO_STACK_LOCATION)];
    };

    KSPIN_LOCK RecvDataLock;
    LIST_ENTRY RecvDataList;
    KEVENT RecvDataEvent;

    UINT32 ReceiveTimeout;
} FNSOCK_SOCKET_BINDING;

typedef struct FNSOCK_SOCKET_SEND_DATA {
    FNSOCK_SOCKET_BINDING* Binding;

    //
    // The IRP buffer for the async WskSendMessages call.
    //
    union {
        IRP Irp;
        UCHAR IrpBuffer[sizeof(IRP) + sizeof(IO_STACK_LOCATION)];
    };

    //
    // Contains the list of FNSOCK_DATAPATH_SEND_BUFFER.
    //
    WSK_BUF_LIST WskBufs;
    PMDL Mdl;
    UCHAR Data[0];
} FNSOCK_SOCKET_SEND_DATA;

typedef struct FNSOCK_SOCKET_RECV_DATA {
    LIST_ENTRY Link;
    ULONG DataLength;
    UCHAR Data[0];
} FNSOCK_SOCKET_RECV_DATA;

static WSK_REGISTRATION WskRegistration;
static WSK_PROVIDER_NPI WskProviderNpi;
static WSK_CLIENT_DATAGRAM_DISPATCH WskDatagramDispatch;

//
// WSK Client version
//
static const WSK_CLIENT_DISPATCH WskAppDispatch = {
    MAKE_WSK_VERSION(1,0), // Use WSK version 1.0
    0,    // Reserved
    NULL  // WskClientEvent callback not required for WSK version 1.0
};

static NTSTATUS FnSockSocketLastError;

IO_COMPLETION_ROUTINE FnSockDataPathIoCompletion;
IO_COMPLETION_ROUTINE FnSockWskCloseSocketIoCompletion;
IO_COMPLETION_ROUTINE FnSockDataPathSendComplete;

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
    NTSTATUS Status;
    WSK_CLIENT_NPI WskClientNpi = { NULL, &WskAppDispatch };
    BOOLEAN WskRegistered = FALSE;
    WSK_EVENT_CALLBACK_CONTROL CallbackControl =
    {
        &NPI_WSK_INTERFACE_ID,
        WSK_EVENT_RECEIVE_FROM
    };

    PAGED_CODE();

    TraceInfo("FnSockInitialize");

    WskDatagramDispatch.WskReceiveFromEvent = FnSockDatagramSocketReceive;

    Status = WskRegister(&WskClientNpi, &WskRegistration);
    if (!NT_SUCCESS(Status)) {
        TraceError(
            "[ lib] ERROR, %u, %s.",
            Status,
            "WskRegister");
        goto Exit;
    }
    WskRegistered = TRUE;

    //
    // Capture the WSK Provider NPI. If WSK subsystem is not ready yet,
    // wait until it becomes ready.
    //
    Status =
        WskCaptureProviderNPI(
            &WskRegistration,
            WSK_INFINITE_WAIT,
            &WskProviderNpi);
    if (!NT_SUCCESS(Status)) {
        TraceError(
            "[ lib] ERROR, %u, %s.",
            Status,
            "WskCaptureProviderNPI");
        goto Exit;
    }

    Status =
        WskProviderNpi.Dispatch->
        WskControlClient(
            WskProviderNpi.Client,
            WSK_SET_STATIC_EVENT_CALLBACKS,
            sizeof(CallbackControl),
            &CallbackControl,
            0,
            NULL,
            NULL,
            NULL);
    if (!NT_SUCCESS(Status)) {
        TraceError(
            "[ lib] ERROR, %u, %s.",
            Status,
            "WskControlClient WSK_SET_STATIC_EVENT_CALLBACKS");
        goto Exit;
    }

Exit:

    if (!NT_SUCCESS(Status)) {
        if (WskRegistered) {
            WskDeregister(&WskRegistration);
        }
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
    PAGED_CODE();

    WskReleaseProviderNPI(&WskRegistration);
    WskDeregister(&WskRegistration);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
void
FnSockSocketDeleteComplete(
    _In_ FNSOCK_SOCKET_BINDING* Binding
    )
{
    while (!IsListEmpty(&Binding->RecvDataList)) {
        LIST_ENTRY* Entry;
        FNSOCK_SOCKET_RECV_DATA* RecvData;

        Entry = RemoveHeadList(&Binding->RecvDataList);
        RecvData = CONTAINING_RECORD(Entry, FNSOCK_SOCKET_RECV_DATA, Link);
        ExFreePoolWithTag(RecvData, FNSOCK_POOL_SOCKET_RECV);
    }
    IoCleanupIrp(&Binding->Irp);
    ExFreePoolWithTag(Binding, FNSOCK_POOL_SOCKET);
}

//
// Used for all WSK IoCompletion routines
//
_Use_decl_annotations_
NTSTATUS
FnSockDataPathIoCompletion(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp,
    VOID* Context
    )
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    NT_ASSERT(Context != NULL);
    __analysis_assume(Context != NULL);
    KeSetEvent((KEVENT*)Context, IO_NO_INCREMENT, FALSE);

    //
    // Always return STATUS_MORE_PROCESSING_REQUIRED to
    // terminate the completion processing of the IRP.
    //
    return STATUS_MORE_PROCESSING_REQUIRED;
}

//
// Completion callbacks for IRP used with WskCloseSocket
//
_Use_decl_annotations_
NTSTATUS
FnSockWskCloseSocketIoCompletion(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp,
    VOID* Context
    )
{
    FNSOCK_SOCKET_BINDING* Binding;

    UNREFERENCED_PARAMETER(DeviceObject);

    NT_ASSERT(Context != NULL);
    __analysis_assume(Context != NULL);
    Binding = (FNSOCK_SOCKET_BINDING*)Context;

    if (Irp->PendingReturned) {
        if (!NT_SUCCESS(Binding->Irp.IoStatus.Status)) {
            TraceError(
                "[data][%p] ERROR, %u, %s.",
                Binding,
                Binding->Irp.IoStatus.Status,
                "WskCloseSocket completion");
        }

        FnSockSocketDeleteComplete(Binding);
    }

    //
    // Always return STATUS_MORE_PROCESSING_REQUIRED to
    // terminate the completion processing of the IRP.
    //
    return STATUS_MORE_PROCESSING_REQUIRED;
}

_Use_decl_annotations_
NTSTATUS
FnSockDataPathSendComplete(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp,
    VOID* Context
    )
{
    FNSOCK_SOCKET_SEND_DATA* SendData;
    FNSOCK_SOCKET_BINDING* Binding;

    UNREFERENCED_PARAMETER(DeviceObject);

    NT_ASSERT(Context != NULL);
    __analysis_assume(Context != NULL);
    SendData = (FNSOCK_SOCKET_SEND_DATA*)Context;
    Binding = SendData->Binding;

    if (!NT_SUCCESS(Irp->IoStatus.Status)) {
        TraceError(
            "[data][%p] ERROR, %u, %s.",
            Binding,
            Irp->IoStatus.Status,
            "WskSendMessages completion");
    }

    IoCleanupIrp(&SendData->Irp);
    NT_ASSERT(SendData->WskBufs.Next == NULL);
    if (SendData->WskBufs.Buffer.Mdl != NULL) {
        NT_ASSERT(SendData->WskBufs.Buffer.Mdl->Next == NULL);
        IoFreeMdl(SendData->WskBufs.Buffer.Mdl);
    }
    ExFreePoolWithTag(SendData, FNSOCK_POOL_SOCKET_SEND);

    return STATUS_MORE_PROCESSING_REQUIRED;
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
    ULONG Flags = 0;

    if (SocketType != SOCK_DGRAM || Protocol != IPPROTO_UDP) {
        TraceError(
            "[data] ERROR, %s.",
            "Unsupported sock type or protocol");
        Status = STATUS_UNSUCCESSFUL;
        goto Exit;
    }
    Flags |= WSK_FLAG_DATAGRAM_SOCKET;

    Binding =
        (FNSOCK_SOCKET_BINDING*)ExAllocatePoolZero(
            NonPagedPoolNx, sizeof(*Binding), FNSOCK_POOL_SOCKET);
    if (Binding == NULL) {
        TraceError(
            "[data] ERROR, %s.",
            "Could not allocate memory for socket");
        Status = STATUS_UNSUCCESSFUL;
        goto Exit;
    }

    KeInitializeEvent(&Binding->WskCompletionEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Binding->RecvDataEvent, NotificationEvent, FALSE);
    InitializeListHead(&Binding->RecvDataList);

    IoInitializeIrp(
        &Binding->Irp,
        sizeof(Binding->IrpBuffer),
        1);
    IoSetCompletionRoutine(
        &Binding->Irp,
        FnSockDataPathIoCompletion,
        &Binding->WskCompletionEvent,
        TRUE,
        TRUE,
        TRUE);

    Status =
        WskProviderNpi.Dispatch->
        WskSocket(
         WskProviderNpi.Client,
            (ADDRESS_FAMILY)AddressFamily,
            (USHORT)SocketType,
            Protocol,
            Flags,
            Binding,
            &WskDatagramDispatch,
            NULL,
            NULL,
            NULL,
            &Binding->Irp);
    if (Status == STATUS_PENDING) {
        KeWaitForSingleObject(&Binding->WskCompletionEvent, Executive, KernelMode, FALSE, NULL);
        Status = Binding->Irp.IoStatus.Status;
    }

    if (!NT_SUCCESS(Status)) {
        TraceError(
            "[data][%p] ERROR, %u, %s.",
            Binding,
            Status,
            "WskSocket");
        goto Exit;
    }

    Binding->Socket = (PWSK_SOCKET)(Binding->Irp.IoStatus.Information);
    *Socket = (FNSOCK_HANDLE)Binding;

Exit:

    if (!NT_SUCCESS(Status)) {
        FnSockSocketLastError = Status;

        if (Binding != NULL) {
            IoCleanupIrp(&Binding->Irp);
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
    FNSOCK_SOCKET_BINDING* Binding = (FNSOCK_SOCKET_BINDING*)Socket;

    IoReuseIrp(&Binding->Irp, STATUS_SUCCESS);
    IoSetCompletionRoutine(
        &Binding->Irp,
        FnSockWskCloseSocketIoCompletion,
        Binding,
        TRUE,
        TRUE,
        TRUE);

    NTSTATUS Status =
        Binding->DgrmSocket->Dispatch->
        WskCloseSocket(
            Binding->Socket,
            &Binding->Irp);

    if (Status == STATUS_PENDING) {
        return; // The rest is handled asynchronously
    }

    if (!NT_SUCCESS(Status)) {
        TraceError(
            "[data][%p] ERROR, %u, %s.",
            Binding,
            Status,
            "WskCloseSocket");
    }

    FnSockSocketDeleteComplete(Binding);
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

    IoReuseIrp(&Binding->Irp, STATUS_SUCCESS);
    IoSetCompletionRoutine(
        &Binding->Irp,
        FnSockDataPathIoCompletion,
        &Binding->WskCompletionEvent,
        TRUE,
        TRUE,
        TRUE);
    KeResetEvent(&Binding->WskCompletionEvent);

    Status =
        Binding->DgrmSocket->Dispatch->
        WskBind(
            Binding->Socket,
            (PSOCKADDR)Address,
            0, // No flags
            &Binding->Irp
            );
    if (Status == STATUS_PENDING) {
        KeWaitForSingleObject(&Binding->WskCompletionEvent, Executive, KernelMode, FALSE, NULL);
        Status = Binding->Irp.IoStatus.Status;
    }

    if (!NT_SUCCESS(Status)) {
        TraceError(
            "[data][%p] ERROR, %u, %s.",
            Binding,
            Status,
            "WskBind");
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

    IoReuseIrp(&Binding->Irp, STATUS_SUCCESS);
    IoSetCompletionRoutine(
        &Binding->Irp,
        FnSockDataPathIoCompletion,
        &Binding->WskCompletionEvent,
        TRUE,
        TRUE,
        TRUE);
    KeResetEvent(&Binding->WskCompletionEvent);

    Status =
        Binding->DgrmSocket->Dispatch->
        WskGetLocalAddress(
            Binding->Socket,
            (PSOCKADDR)Address,
            &Binding->Irp);
    if (Status == STATUS_PENDING) {
        KeWaitForSingleObject(&Binding->WskCompletionEvent, Executive, KernelMode, FALSE, NULL);
        Status = Binding->Irp.IoStatus.Status;
    }

    if (!NT_SUCCESS(Status)) {
        TraceError(
            "[data][%p] ERROR, %u, %s.",
            Binding,
            Status,
            "WskGetLocalAddress");
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
        if (OptionLength < sizeof(UINT32) || OptionValue == NULL || *(UINT32*)OptionValue == MAXUINT32) {
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

    IoReuseIrp(&Binding->Irp, STATUS_SUCCESS);
    IoSetCompletionRoutine(
        &Binding->Irp,
        FnSockDataPathIoCompletion,
        &Binding->WskCompletionEvent,
        TRUE,
        TRUE,
        TRUE);
    KeResetEvent(&Binding->WskCompletionEvent);

    Status =
        Binding->DgrmSocket->Dispatch->
        WskControlSocket(
            Binding->Socket,
            WskSetOption,
            OptionName,
            Level,
            OptionLength,
            OptionValue,
            0,
            NULL,
            NULL,
            &Binding->Irp);
    if (Status == STATUS_PENDING) {
        KeWaitForSingleObject(&Binding->WskCompletionEvent, Executive, KernelMode, FALSE, NULL);
        Status = Binding->Irp.IoStatus.Status;
    }

    if (!NT_SUCCESS(Status)) {
        TraceError(
            "[data][%p] ERROR, %u, %s.",
            Binding,
            Status,
            "WskControlSocket completion");
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
    _In_ INT Flags,
    _In_reads_bytes_(AddressLength) const struct sockaddr* Address,
    _In_ INT AddressLength
    )
{
    NTSTATUS Status;
    FNSOCK_SOCKET_BINDING* Binding = (FNSOCK_SOCKET_BINDING*)Socket;
    FNSOCK_SOCKET_SEND_DATA* SendData;
    INT BytesSent = 0;

    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(AddressLength);

    SendData =
        (FNSOCK_SOCKET_SEND_DATA*)ExAllocatePoolZero(
            NonPagedPoolNx, sizeof(*SendData) + BufferLength, FNSOCK_POOL_SOCKET_SEND);
    if (SendData == NULL) {
        TraceError(
            "[data] ERROR, %s.",
            "Could not allocate memory for socket send");
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    SendData->WskBufs.Next = NULL;
    SendData->WskBufs.Buffer.Length = BufferLength;
    SendData->WskBufs.Buffer.Offset = 0;
    RtlCopyMemory(SendData->Data, Buffer, BufferLength);

    SendData->WskBufs.Buffer.Mdl = IoAllocateMdl(SendData->Data, BufferLength, FALSE, FALSE, NULL);
    if (SendData->WskBufs.Buffer.Mdl == NULL) {
        TraceError(
            "[data] ERROR, %s.",
            "Could not allocate memory for socket send MDL");
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    MmBuildMdlForNonPagedPool(SendData->WskBufs.Buffer.Mdl);

    SendData->Binding = Binding;

    IoInitializeIrp(
        &SendData->Irp,
        sizeof(SendData->IrpBuffer),
        1);
    IoSetCompletionRoutine(
        &SendData->Irp,
        FnSockDataPathSendComplete,
        SendData,
        TRUE,
        TRUE,
        TRUE);

    Status =
        Binding->DgrmSocket->Dispatch->
        WskSendMessages(
            Binding->Socket,
            &SendData->WskBufs,
            0,
            (PSOCKADDR)Address,
            0,
            NULL,
            &SendData->Irp);

    if (!NT_SUCCESS(Status)) {
        TraceError(
            "[data][%p] ERROR, %u, %s.",
            Binding,
            Status,
            "WskSendMessages");
        //
        // Callback still gets invoked on failure to do the cleanup.
        //
    }

    Status = STATUS_SUCCESS;
    BytesSent = BufferLength;

Exit:

    if (!NT_SUCCESS(Status)) {
        FnSockSocketLastError = Status;

        if (SendData != NULL) {
            NT_ASSERT(SendData->WskBufs.Next == NULL);
            if (SendData->WskBufs.Buffer.Mdl != NULL) {
                IoCleanupIrp(&SendData->Irp);
                NT_ASSERT(SendData->WskBufs.Buffer.Mdl->Next == NULL);
                IoFreeMdl(SendData->WskBufs.Buffer.Mdl);
            }
            ExFreePoolWithTag(SendData, FNSOCK_POOL_SOCKET_SEND);
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
    _In_ INT Flags
    )
{
    FNSOCK_SOCKET_BINDING* Binding = (FNSOCK_SOCKET_BINDING*)Socket;
    KIRQL PrevIrql;
    INT BytesReceived = -1;
    NTSTATUS Status = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(Flags);

    KeAcquireSpinLock(&Binding->RecvDataLock, &PrevIrql);

    if (IsListEmpty(&Binding->RecvDataList)) {
        LARGE_INTEGER Storage;
        LARGE_INTEGER* Timeout100Ns;

        KeReleaseSpinLock(&Binding->RecvDataLock, PrevIrql);

        if (Binding->ReceiveTimeout != 0) {
            Timeout100Ns = &Storage;
            Timeout100Ns->QuadPart = -1 * ((ULONGLONG)Binding->ReceiveTimeout * 10000);
        } else {
            Timeout100Ns = NULL;
        }

        Status = KeWaitForSingleObject(&Binding->RecvDataEvent, Executive, KernelMode, FALSE, Timeout100Ns);
        if (Status == STATUS_TIMEOUT) {
            goto Exit;
        }
        KeAcquireSpinLock(&Binding->RecvDataLock, &PrevIrql);
        KeResetEvent(&Binding->RecvDataEvent);
    }

    if (!IsListEmpty(&Binding->RecvDataList)) {
        LIST_ENTRY* Entry;
        FNSOCK_SOCKET_RECV_DATA* RecvData;

        Entry = RemoveHeadList(&Binding->RecvDataList);

        KeReleaseSpinLock(&Binding->RecvDataLock, PrevIrql);

        RecvData = CONTAINING_RECORD(Entry, FNSOCK_SOCKET_RECV_DATA, Link);
        if (RecvData->DataLength <= (ULONG)BufferLength) {
            BytesReceived = RecvData->DataLength;
        }
        RtlCopyMemory(Buffer, RecvData->Data, min(RecvData->DataLength, (ULONG)BufferLength));

        ExFreePoolWithTag(RecvData, FNSOCK_POOL_SOCKET_RECV);
    } else {
        KeReleaseSpinLock(&Binding->RecvDataLock, PrevIrql);
    }

Exit:

    if (!NT_SUCCESS(Status)) {
        FnSockSocketLastError = Status;
    }

    return (BytesReceived < 0) ? FNSOCK_STATUS_FAIL : BytesReceived;
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
            (FNSOCK_SOCKET_RECV_DATA*)ExAllocatePoolZero(
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
            RtlCopyMemory(RecvData->Data + CopiedLength, (UCHAR*)Mdl->MappedSystemVa + MdlOffset, CopySize);
            CopiedLength += CopySize;

            Mdl = Mdl->Next;
            MdlOffset = 0;
        }

        KIRQL PrevIrql;
        KeAcquireSpinLock(&Binding->RecvDataLock, &PrevIrql);
        InsertTailList(&Binding->RecvDataList, &RecvData->Link);
        KeSetEvent(&Binding->RecvDataEvent, IO_NO_INCREMENT, FALSE);
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
