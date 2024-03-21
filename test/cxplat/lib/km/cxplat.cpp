//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include <ntddk.h>
#include <wsk.h>

#include "cxplat.h"
#include "trace.h"

#include "cxplat.tmh"

#define CXPLAT_POOL_SOCKET 'kSxC' // CxSk
#define CXPLAT_POOL_SOCKET_SEND 'sSxC' // CxSs
#define CXPLAT_POOL_SOCKET_RECV 'rSxC' // CxSr

typedef struct _WSK_DATAGRAM_SOCKET {
    const WSK_PROVIDER_DATAGRAM_DISPATCH* Dispatch;
} WSK_DATAGRAM_SOCKET, * PWSK_DATAGRAM_SOCKET;

typedef struct CXPLAT_SOCKET_BINDING {
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
} CXPLAT_SOCKET_BINDING;

typedef struct CXPLAT_SOCKET_SEND_DATA {
    CXPLAT_SOCKET_BINDING* Binding;

    //
    // The IRP buffer for the async WskSendMessages call.
    //
    union {
        IRP Irp;
        UCHAR IrpBuffer[sizeof(IRP) + sizeof(IO_STACK_LOCATION)];
    };

    //
    // Contains the list of CXPLAT_DATAPATH_SEND_BUFFER.
    //
    WSK_BUF_LIST WskBufs;
    PMDL Mdl;
    UCHAR Data[0];
} CXPLAT_SOCKET_SEND_DATA;

typedef struct CXPLAT_SOCKET_RECV_DATA {
    LIST_ENTRY Link;
    ULONG DataLength;
    UCHAR Data[0];
} CXPLAT_SOCKET_RECV_DATA;

UINT64 CxPlatPerfFreq;
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

IO_COMPLETION_ROUTINE CxPlatDataPathIoCompletion;
IO_COMPLETION_ROUTINE CxPlatWskCloseSocketIoCompletion;
IO_COMPLETION_ROUTINE CxPlatDataPathSendComplete;

_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_
NTSTATUS
NTAPI
CxPlatDatagramSocketReceive(
    _In_opt_ void* Context,
    _In_ ULONG Flags,
    _In_opt_ PWSK_DATAGRAM_INDICATION DataIndicationHead
    );

PAGEDX
_IRQL_requires_max_(PASSIVE_LEVEL)
CXPLAT_STATUS
CxPlatInitialize(
    VOID
    )
{
    CXPLAT_STATUS Status;
    WSK_CLIENT_NPI WskClientNpi = { NULL, &WskAppDispatch };
    BOOLEAN WskRegistered = FALSE;
    WSK_EVENT_CALLBACK_CONTROL CallbackControl =
    {
        &NPI_WSK_INTERFACE_ID,
        WSK_EVENT_RECEIVE_FROM
    };

    PAGED_CODE();

    TraceError("CxPlatInitialize");

    (VOID)KeQueryPerformanceCounter((LARGE_INTEGER*)&CxPlatPerfFreq);

    WskDatagramDispatch.WskReceiveFromEvent = CxPlatDatagramSocketReceive;

    Status = WskRegister(&WskClientNpi, &WskRegistration);
    if (CXPLAT_FAILED(Status)) {
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
    if (CXPLAT_FAILED(Status)) {
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
    if (CXPLAT_FAILED(Status)) {
        TraceError(
            "[ lib] ERROR, %u, %s.",
            Status,
            "WskControlClient WSK_SET_STATIC_EVENT_CALLBACKS");
        goto Exit;
    }

Exit:

    if (CXPLAT_FAILED(Status)) {
        if (WskRegistered) {
            WskDeregister(&WskRegistration);
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
    PAGED_CODE();

    WskReleaseProviderNPI(&WskRegistration);
    WskDeregister(&WskRegistration);
}

//
// Sockets API.
//

static NTSTATUS CxPlatSocketLastError;

_IRQL_requires_max_(PASSIVE_LEVEL)
void
CxPlatSocketDeleteComplete(
    _In_ CXPLAT_SOCKET_BINDING* Binding
    )
{
    while (!IsListEmpty(&Binding->RecvDataList)) {
        LIST_ENTRY* Entry;
        CXPLAT_SOCKET_RECV_DATA* RecvData;

        Entry = RemoveHeadList(&Binding->RecvDataList);
        RecvData = CONTAINING_RECORD(Entry, CXPLAT_SOCKET_RECV_DATA, Link);
        ExFreePoolWithTag(RecvData, CXPLAT_POOL_SOCKET_RECV);
    }
    IoCleanupIrp(&Binding->Irp);
    ExFreePoolWithTag(Binding, CXPLAT_POOL_SOCKET);
}

//
// Used for all WSK IoCompletion routines
//
_Use_decl_annotations_
NTSTATUS
CxPlatDataPathIoCompletion(
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
CxPlatWskCloseSocketIoCompletion(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp,
    VOID* Context
    )
{
    CXPLAT_SOCKET_BINDING* Binding;

    UNREFERENCED_PARAMETER(DeviceObject);

    NT_ASSERT(Context != NULL);
    __analysis_assume(Context != NULL);
    Binding = (CXPLAT_SOCKET_BINDING*)Context;

    if (Irp->PendingReturned) {
        if (!NT_SUCCESS(Binding->Irp.IoStatus.Status)) {
            TraceError(
                "[data][%p] ERROR, %u, %s.",
                Binding,
                Binding->Irp.IoStatus.Status,
                "WskCloseSocket completion");
        }

        CxPlatSocketDeleteComplete(Binding);
    }

    //
    // Always return STATUS_MORE_PROCESSING_REQUIRED to
    // terminate the completion processing of the IRP.
    //
    return STATUS_MORE_PROCESSING_REQUIRED;
}

_Use_decl_annotations_
NTSTATUS
CxPlatDataPathSendComplete(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp,
    VOID* Context
    )
{
    CXPLAT_SOCKET_SEND_DATA* SendData;
    CXPLAT_SOCKET_BINDING* Binding;

    UNREFERENCED_PARAMETER(DeviceObject);

    NT_ASSERT(Context != NULL);
    __analysis_assume(Context != NULL);
    SendData = (CXPLAT_SOCKET_SEND_DATA*)Context;
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
    ExFreePoolWithTag(SendData, CXPLAT_POOL_SOCKET_SEND);

    return STATUS_MORE_PROCESSING_REQUIRED;
}

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
    CXPLAT_SOCKET_BINDING* Binding = NULL;
    ULONG Flags = 0;

    if (SocketType != SOCK_DGRAM || Protocol != IPPROTO_UDP) {
        TraceError(
            "[data] ERROR, %s.",
            "Unsupported sock type or protocol");
        Status = CXPLAT_STATUS_FAIL;
        goto Exit;
    }
    Flags |= WSK_FLAG_DATAGRAM_SOCKET;

    Binding =
        (CXPLAT_SOCKET_BINDING*)ExAllocatePool2(
            POOL_FLAG_NON_PAGED, sizeof(*Binding), CXPLAT_POOL_SOCKET);
    if (Binding == NULL) {
        TraceError(
            "[data] ERROR, %s.",
            "Could not allocate memory for socket");
        Status = CXPLAT_STATUS_FAIL;
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
        CxPlatDataPathIoCompletion,
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
    *Socket = (CXPLAT_SOCKET)Binding;

Exit:

    if (!NT_SUCCESS(Status)) {
        CxPlatSocketLastError = Status;
    }

    if (CXPLAT_FAILED(Status)) {
        if (Binding != NULL) {
            IoCleanupIrp(&Binding->Irp);
            ExFreePoolWithTag(Binding, CXPLAT_POOL_SOCKET);
        }
    }

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
CxPlatSocketClose(
    _In_ CXPLAT_SOCKET Socket
    )
{
    CXPLAT_SOCKET_BINDING* Binding = (CXPLAT_SOCKET_BINDING*)Socket;

    IoReuseIrp(&Binding->Irp, STATUS_SUCCESS);
    IoSetCompletionRoutine(
        &Binding->Irp,
        CxPlatWskCloseSocketIoCompletion,
        Binding,
        TRUE,
        TRUE,
        TRUE);

    NTSTATUS Status =
        Binding->DgrmSocket->Dispatch->Basic.
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

    CxPlatSocketDeleteComplete(Binding);
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
    CXPLAT_SOCKET_BINDING* Binding = (CXPLAT_SOCKET_BINDING*)Socket;

    UNREFERENCED_PARAMETER(AddressLength);

    IoReuseIrp(&Binding->Irp, STATUS_SUCCESS);
    IoSetCompletionRoutine(
        &Binding->Irp,
        CxPlatDataPathIoCompletion,
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
        CxPlatSocketLastError = Status;
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
    CXPLAT_SOCKET_BINDING* Binding = (CXPLAT_SOCKET_BINDING*)Socket;

    UNREFERENCED_PARAMETER(AddressLength);

    IoReuseIrp(&Binding->Irp, STATUS_SUCCESS);
    IoSetCompletionRoutine(
        &Binding->Irp,
        CxPlatDataPathIoCompletion,
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
        CxPlatSocketLastError = Status;
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
    CXPLAT_SOCKET_BINDING* Binding = (CXPLAT_SOCKET_BINDING*)Socket;

    if (Level == SOL_SOCKET && OptionName == SO_RCVTIMEO) {
        if (OptionLength < sizeof(UINT32) || OptionValue == NULL || *(UINT32*)OptionValue == MAXUINT32) {
            TraceError(
                "[data] ERROR, %s.",
                "SO_RCVTIMEO invalid parameter");
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }
        Binding->ReceiveTimeout = *(UINT32*)OptionValue;
        Status = CXPLAT_STATUS_SUCCESS;
        goto Exit;
    }

    IoReuseIrp(&Binding->Irp, STATUS_SUCCESS);
    IoSetCompletionRoutine(
        &Binding->Irp,
        CxPlatDataPathIoCompletion,
        &Binding->WskCompletionEvent,
        TRUE,
        TRUE,
        TRUE);
    KeResetEvent(&Binding->WskCompletionEvent);

    Status =
        Binding->DgrmSocket->Dispatch->Basic.
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
        CxPlatSocketLastError = Status;
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
    CXPLAT_STATUS Status;
    CXPLAT_SOCKET_BINDING* Binding = (CXPLAT_SOCKET_BINDING*)Socket;
    CXPLAT_SOCKET_SEND_DATA* SendData;
    INT BytesSent = 0;

    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(AddressLength);

    SendData =
        (CXPLAT_SOCKET_SEND_DATA*)ExAllocatePool2(
            POOL_FLAG_NON_PAGED, sizeof(*SendData) + BufferLength, CXPLAT_POOL_SOCKET_SEND);
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
        CxPlatDataPathSendComplete,
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
        CxPlatSocketLastError = Status;

        if (SendData != NULL) {
            NT_ASSERT(SendData->WskBufs.Next == NULL);
            if (SendData->WskBufs.Buffer.Mdl != NULL) {
                IoCleanupIrp(&SendData->Irp);
                NT_ASSERT(SendData->WskBufs.Buffer.Mdl->Next == NULL);
                IoFreeMdl(SendData->WskBufs.Buffer.Mdl);
            }
            ExFreePoolWithTag(SendData, CXPLAT_POOL_SOCKET_SEND);
        }
    }

    return BytesSent;
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
    CXPLAT_SOCKET_BINDING* Binding = (CXPLAT_SOCKET_BINDING*)Socket;
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
        CXPLAT_SOCKET_RECV_DATA* RecvData;

        Entry = RemoveHeadList(&Binding->RecvDataList);

        KeReleaseSpinLock(&Binding->RecvDataLock, PrevIrql);

        RecvData = CONTAINING_RECORD(Entry, CXPLAT_SOCKET_RECV_DATA, Link);
        if (RecvData->DataLength <= (ULONG)BufferLength) {
            BytesReceived = RecvData->DataLength;
        }
        RtlCopyMemory(Buffer, RecvData->Data, min(RecvData->DataLength, (ULONG)BufferLength));

        ExFreePoolWithTag(RecvData, CXPLAT_POOL_SOCKET_RECV);
    } else {
        KeReleaseSpinLock(&Binding->RecvDataLock, PrevIrql);
    }

Exit:

    if (!NT_SUCCESS(Status)) {
        CxPlatSocketLastError = Status;
    }

    return (BytesReceived < 0) ? CXPLAT_STATUS_FAIL : BytesReceived;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_
NTSTATUS
NTAPI
CxPlatDatagramSocketReceive(
    _In_opt_ void* Context,
    _In_ ULONG Flags,
    _In_opt_ PWSK_DATAGRAM_INDICATION DataIndicationHead
    )
{
    CXPLAT_SOCKET_BINDING* Binding = (CXPLAT_SOCKET_BINDING*)Context;

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

        CXPLAT_SOCKET_RECV_DATA* RecvData = NULL;
        RecvData =
            (CXPLAT_SOCKET_RECV_DATA*)ExAllocatePool2(
                POOL_FLAG_NON_PAGED, sizeof(*RecvData) + DataLength, CXPLAT_POOL_SOCKET_RECV);
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
CxPlatSocketGetLastError(
    VOID
    )
{
    return NtStatusToSocketError(CxPlatSocketLastError);
}

//
// Time Measurement Interfaces
//

UINT64
CxPlatTimePlat(
    void
    )
{
    return (UINT64)KeQueryPerformanceCounter(NULL).QuadPart;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
CxPlatSleep(
    _In_ UINT32 DurationMs
    )
{
    NT_ASSERT(DurationMs != (UINT32)-1);

    KTIMER SleepTimer;
    LARGE_INTEGER TimerValue;

    KeInitializeTimerEx(&SleepTimer, SynchronizationTimer);
    TimerValue.QuadPart = (UINT64)DurationMs * -10000;
    KeSetTimer(&SleepTimer, TimerValue, NULL);

    KeWaitForSingleObject(&SleepTimer, Executive, KernelMode, FALSE, NULL);
}

//
// Allocation/Memory Interfaces
//

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID*
CxPlatAllocNonPaged(
    _In_ SIZE_T Size,
    _In_ ULONG Tag
    )
{
    return ExAllocatePool2(POOL_FLAG_NON_PAGED | POOL_FLAG_UNINITIALIZED, Size, Tag);
}

VOID
CxPlatFree(
    _In_ VOID* Mem,
    _In_ ULONG Tag
    )
{
    ExFreePoolWithTag(Mem, Tag);
}

VOID
CxPlatFreeNoTag(
    _In_opt_ VOID* Mem
    )
{
    if (Mem != NULL) {
        ExFreePool(Mem);
    }
}

//
// Create Thread Interfaces
//

CXPLAT_STATUS
CxPlatThreadCreate(
    _In_ CXPLAT_THREAD_CONFIG* Config,
    _Out_ CXPLAT_THREAD* Thread
    )
{
    CXPLAT_STATUS Status;
    _ETHREAD *EThread = NULL;
    HANDLE ThreadHandle;
    Status =
        PsCreateSystemThread(
            &ThreadHandle,
            THREAD_ALL_ACCESS,
            NULL,
            NULL,
            NULL,
            Config->Callback,
            Config->Context);
    NT_ASSERT(CXPLAT_SUCCEEDED(Status));
    if (CXPLAT_FAILED(Status)) {
        goto Error;
    }
    Status =
        ObReferenceObjectByHandle(
            ThreadHandle,
            THREAD_ALL_ACCESS,
            *PsThreadType,
            KernelMode,
            (void**)&EThread,
            NULL);
    NT_ASSERT(CXPLAT_SUCCEEDED(Status));
    if (CXPLAT_FAILED(Status)) {
        goto Cleanup;
    }
    PROCESSOR_NUMBER Processor, IdealProcessor;
    Status =
        KeGetProcessorNumberFromIndex(
            Config->IdealProcessor,
            &Processor);
    if (CXPLAT_FAILED(Status)) {
        Status = CXPLAT_STATUS_SUCCESS; // Currently we don't treat this as fatal
        goto SetPriority;             // TODO: Improve this logic.
    }
    IdealProcessor = Processor;
    if (Config->Flags & CXPLAT_THREAD_FLAG_SET_AFFINITIZE) {
        GROUP_AFFINITY Affinity;
        RtlZeroMemory(&Affinity, sizeof(Affinity));
        Affinity.Group = Processor.Group;
        Affinity.Mask = (1ull << Processor.Number);
        Status =
            ZwSetInformationThread(
                ThreadHandle,
                ThreadGroupInformation,
                &Affinity,
                sizeof(Affinity));
        NT_ASSERT(CXPLAT_SUCCEEDED(Status));
        if (CXPLAT_FAILED(Status)) {
            goto Cleanup;
        }
    } else { // NUMA Node Affinity
        SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX Info;
        ULONG InfoLength = sizeof(Info);
        Status =
            KeQueryLogicalProcessorRelationship(
                &Processor,
                RelationNumaNode,
                &Info,
                &InfoLength);
        NT_ASSERT(CXPLAT_SUCCEEDED(Status));
        if (CXPLAT_FAILED(Status)) {
            goto Cleanup;
        }
        Status =
            ZwSetInformationThread(
                ThreadHandle,
                ThreadGroupInformation,
                &Info.NumaNode.GroupMask,
                sizeof(GROUP_AFFINITY));
        NT_ASSERT(CXPLAT_SUCCEEDED(Status));
        if (CXPLAT_FAILED(Status)) {
            goto Cleanup;
        }
    }
    if (Config->Flags & CXPLAT_THREAD_FLAG_SET_IDEAL_PROC) {
        Status =
            ZwSetInformationThread(
                ThreadHandle,
                ThreadIdealProcessorEx,
                &IdealProcessor, // Don't pass in Processor because this overwrites on output.
                sizeof(IdealProcessor));
        NT_ASSERT(CXPLAT_SUCCEEDED(Status));
        if (CXPLAT_FAILED(Status)) {
            goto Cleanup;
        }
    }
SetPriority:
    if (Config->Flags & CXPLAT_THREAD_FLAG_HIGH_PRIORITY) {
        KeSetBasePriorityThread(
            (PKTHREAD)EThread,
            IO_NETWORK_INCREMENT + 1);
    }
    if (Config->Name) {
        DECLARE_UNICODE_STRING_SIZE(UnicodeName, 64);
        ULONG UnicodeNameLength = 0;
        Status =
            RtlUTF8ToUnicodeN(
                UnicodeName.Buffer,
                UnicodeName.MaximumLength,
                &UnicodeNameLength,
                Config->Name,
                (ULONG)strnlen(Config->Name, 64));
        NT_ASSERT(CXPLAT_SUCCEEDED(Status));
        UnicodeName.Length = (USHORT)UnicodeNameLength;
#define ThreadNameInformation ((THREADINFOCLASS)38)
        Status =
            ZwSetInformationThread(
                ThreadHandle,
                ThreadNameInformation,
                &UnicodeName,
                sizeof(UNICODE_STRING));
        NT_ASSERT(CXPLAT_SUCCEEDED(Status));
        Status = CXPLAT_STATUS_SUCCESS;
    }
    *Thread = (CXPLAT_THREAD)EThread;
Cleanup:
    ZwClose(ThreadHandle);
Error:
    return Status;
}

VOID
CxPlatThreadDelete(
    _In_ CXPLAT_THREAD Thread
    )
{
    _ETHREAD *EThread = (_ETHREAD *)Thread;

    ObDereferenceObject(EThread);
}

BOOLEAN
CxPlatThreadWait(
    _In_ CXPLAT_THREAD Thread,
    _In_ UINT32 TimeoutMs
    )
{
    _ETHREAD *EThread = (_ETHREAD *)Thread;

    LARGE_INTEGER Timeout100Ns;
    NT_ASSERT(TimeoutMs != MAXUINT32);
    Timeout100Ns.QuadPart = -1 * ((UINT64)TimeoutMs * 10000);
    return KeWaitForSingleObject(EThread, Executive, KernelMode, FALSE, &Timeout100Ns) != STATUS_TIMEOUT;
}
