#include <ntddk.h>
#include <wsk.h>
#include "wskclient.h"

#define LOG(x, ...) DbgPrint((x), __VA_ARGS__)

static const WSK_CLIENT_DISPATCH WskAppDispatch = {MAKE_WSK_VERSION(1,0), 0, NULL};
static WSK_REGISTRATION Registration;
static WSK_CLIENT_NPI ClientNpi;
static WSK_PROVIDER_NPI ProviderNpi;

NTSTATUS
WskClientReg(
    VOID
    )
{
    NTSTATUS Status;
    ClientNpi.ClientContext = NULL;
    ClientNpi.Dispatch = &WskAppDispatch;
    Status = WskRegister(&ClientNpi, &Registration);
    if (!NT_SUCCESS(Status)) {
        LOG("WskRegister failed with %d\n", Status);
        goto Fail;
    }
    Status = WskCaptureProviderNPI(&Registration, WSK_NO_WAIT, &ProviderNpi);
    if (!NT_SUCCESS(Status)) {
        LOG("WskCaptureProviderNPI failed with %d\n", Status);
        goto Deregister;
    }

    return Status;

Deregister:
    WskDeregister(&Registration);
Fail:
    return Status;
}

VOID
WskClientDereg(
    VOID
    )
{
    WskReleaseProviderNPI(&Registration);
    WskDeregister(&Registration);
}

static
_Function_class_(IO_COMPLETION_ROUTINE)
NTSTATUS
GenericCompletionRoutine(
    const DEVICE_OBJECT* DeviceObject,
    const IRP* Irp,
    PVOID Context
    )
{
    PKEVENT Event = (PKEVENT)Context;
    UNREFERENCED_PARAMETER(DeviceObject);
    if (Irp->PendingReturned) {
        KeSetEvent(Event, 0, FALSE);
    }
    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS
WskWaitForIrpCompletion(
    PKEVENT Event,
    PIRP Irp,
    ULONG TimeoutMs
    )
{
    NTSTATUS Status;
    LARGE_INTEGER Timeout;
    LARGE_INTEGER* TimeoutPtr = NULL;

    if (TimeoutMs != WSKCLIENT_INFINITE) {
        Timeout.QuadPart = UInt32x32To64(TimeoutMs, 10000);
        Timeout.QuadPart = -Timeout.QuadPart;
        TimeoutPtr = &Timeout;
    }

    Status = KeWaitForSingleObject(Event, Executive, KernelMode, FALSE, TimeoutPtr);
    if (Status == STATUS_TIMEOUT) {
        IoCancelIrp(Irp);
        KeWaitForSingleObject(Event, Executive, KernelMode, FALSE, NULL);
    }

    return Irp->IoStatus.Status;
}

NTSTATUS
WskSocketSync(
    int WskSockType,
    ADDRESS_FAMILY Family,
    USHORT SockType,
    IPPROTO Protocol,
    _Out_ PWSK_SOCKET* Sock,
    _In_opt_ PVOID Context,
    _In_opt_ PVOID Dispatch
    )
{
    PIRP Irp;
    NTSTATUS Status;
    KEVENT Event;

    *Sock = NULL;
    Irp = IoAllocateIrp(1, FALSE);
    if (Irp == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoSetCompletionRoutine(Irp, GenericCompletionRoutine, &Event, TRUE, TRUE, TRUE);
    Status =
        ProviderNpi.Dispatch->WskSocket(
            ProviderNpi.Client, Family, SockType, Protocol,
            WskSockType, Context, Dispatch, NULL, NULL, NULL, Irp);
    if (Status == STATUS_PENDING) {
        Status = KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    } else if (!NT_SUCCESS(Status)) {
        LOG("WskSocket failed with %d\n", Status);
        goto IoStatus;
    }
    *Sock = (PWSK_SOCKET)Irp->IoStatus.Information;
IoStatus:
    Status = Irp->IoStatus.Status;
    IoFreeIrp(Irp);
    return Status;
}

NTSTATUS
WskSocketConnectSync(
    USHORT SockType,
    IPPROTO Protocol,
    PSOCKADDR LocalAddr,
    PSOCKADDR RemoteAddr,
    _Out_ PWSK_SOCKET* Sock,
    _In_opt_ PVOID Context,
    _In_opt_ WSK_CLIENT_CONNECTION_DISPATCH* Dispatch
    )
{
    PIRP Irp;
    NTSTATUS Status;
    KEVENT Event;

    *Sock = NULL;
    Irp = IoAllocateIrp(1, FALSE);
    if (Irp == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoSetCompletionRoutine(Irp, GenericCompletionRoutine, &Event, TRUE, TRUE, TRUE);
    Status =
        ProviderNpi.Dispatch->WskSocketConnect(
            ProviderNpi.Client, SockType, Protocol, LocalAddr, RemoteAddr,
            0, Context, Dispatch, NULL, NULL, NULL, Irp);
    if (Status == STATUS_PENDING) {
        Status = KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    } else if (!NT_SUCCESS(Status)) {
        LOG("WskSocketConnect failed with %d\n", Status);
        goto IoStatus;
    }
    *Sock = (PWSK_SOCKET)Irp->IoStatus.Information;
IoStatus:
    Status = Irp->IoStatus.Status;
    IoFreeIrp(Irp);
    return Status;
}

NTSTATUS
WskBindSync(
    PWSK_SOCKET Sock,
    int SockType,
    PSOCKADDR Addr
    )
{
    PIRP Irp;
    NTSTATUS Status;
    KEVENT Event;
    PFN_WSK_BIND WskBind;

    switch (SockType) {
    case WSK_FLAG_CONNECTION_SOCKET:
        WskBind = ((PWSK_PROVIDER_CONNECTION_DISPATCH)Sock->Dispatch)->WskBind;
        break;
    case WSK_FLAG_STREAM_SOCKET:
        WskBind = ((PWSK_PROVIDER_STREAM_DISPATCH)Sock->Dispatch)->WskBind;
        break;
    case WSK_FLAG_LISTEN_SOCKET:
        WskBind = ((PWSK_PROVIDER_LISTEN_DISPATCH)Sock->Dispatch)->WskBind;
        break;
    case WSK_FLAG_DATAGRAM_SOCKET:
        WskBind = ((PWSK_PROVIDER_DATAGRAM_DISPATCH)Sock->Dispatch)->WskBind;
        break;
    default:
        ASSERT(FALSE);
        return STATUS_UNSUCCESSFUL;
    }

    Irp = IoAllocateIrp(1, FALSE);
    if (Irp == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoSetCompletionRoutine(Irp, GenericCompletionRoutine, &Event, TRUE, TRUE, TRUE);
    Status = WskBind(Sock, Addr, 0, Irp);
    if (Status == STATUS_PENDING) {
        Status = KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    } else if (!NT_SUCCESS(Status)) {
        LOG("WskBind failed with %d\n", Status);
    }
    Status = Irp->IoStatus.Status;
    IoFreeIrp(Irp);
    return Status;
}

NTSTATUS
WskListenSync(
    PWSK_SOCKET Sock,
    int SockType
    )
//
// This function is used for listening with WSK stream sockets.
//
// If you need to create a socket and at the point of creation you know it
// needs to be a listening socket, you can create a wsk listening socket
// directly using WskSocketSync(WSK_FLAG_LISTEN_SOCKET, ...).
//
{
    PIRP Irp;
    NTSTATUS Status;
    KEVENT Event;
    PFN_WSK_LISTEN WskListen;

    UNREFERENCED_PARAMETER(SockType);

    ASSERT(SockType == WSK_FLAG_STREAM_SOCKET);
    WskListen = ((PWSK_PROVIDER_STREAM_DISPATCH)Sock->Dispatch)->WskListen;

    Irp = IoAllocateIrp(1, FALSE);
    if (Irp == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoSetCompletionRoutine(Irp, GenericCompletionRoutine, &Event, TRUE, TRUE, TRUE);
    Status = WskListen(Sock, Irp);
    if (Status == STATUS_PENDING) {
        Status = KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    } else if (!NT_SUCCESS(Status)) {
        LOG("WskListen failed with %d\n", Status);
    }
    Status = Irp->IoStatus.Status;
    IoFreeIrp(Irp);
    return Status;
}

NTSTATUS
WskConnectSync(
    PWSK_SOCKET Sock,
    int SockType,
    PSOCKADDR Addr
    )
{
    PIRP Irp;
    NTSTATUS Status;
    KEVENT Event;
    PFN_WSK_CONNECT WskConnect;

    switch (SockType) {
    case WSK_FLAG_CONNECTION_SOCKET:
        WskConnect = ((PWSK_PROVIDER_CONNECTION_DISPATCH)Sock->Dispatch)->WskConnect;
        break;
    case WSK_FLAG_STREAM_SOCKET:
        WskConnect = ((PWSK_PROVIDER_STREAM_DISPATCH)Sock->Dispatch)->WskConnect;
        break;
    default:
        ASSERT(FALSE);
        return STATUS_UNSUCCESSFUL;
    }

    Irp = IoAllocateIrp(1, FALSE);
    if (Irp == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoSetCompletionRoutine(Irp, GenericCompletionRoutine, &Event, TRUE, TRUE, TRUE);
    Status = WskConnect(Sock, Addr, 0, Irp);
    if (Status == STATUS_PENDING) {
        Status = KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    } else if (!NT_SUCCESS(Status)) {
        LOG("WskConnect failed with %d\n", Status);
    }
    Status = Irp->IoStatus.Status;
    IoFreeIrp(Irp);
    return Status;
}

NTSTATUS
WskConnectExSync(
    PWSK_SOCKET Sock,
    int SockType,
    PSOCKADDR Addr,
    char *Buf,
    ULONG BufLen
    )
{
    PIRP Irp;
    NTSTATUS Status;
    KEVENT Event;
    PFN_WSK_CONNECT_EX WskConnectEx;
    WSK_BUF WskBuf = {0};

    switch (SockType) {
    case WSK_FLAG_CONNECTION_SOCKET:
        WskConnectEx = ((PWSK_PROVIDER_CONNECTION_DISPATCH)Sock->Dispatch)->WskConnectEx;
        break;
    case WSK_FLAG_STREAM_SOCKET:
        WskConnectEx = ((PWSK_PROVIDER_STREAM_DISPATCH)Sock->Dispatch)->WskConnectEx;
        break;
    default:
        ASSERT(FALSE);
        return STATUS_UNSUCCESSFUL;
    }

    Irp = IoAllocateIrp(1, FALSE);
    if (Irp == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoSetCompletionRoutine(Irp, GenericCompletionRoutine, &Event, TRUE, TRUE, TRUE);

    if (Buf != NULL) {
        WskBuf.Mdl = IoAllocateMdl(Buf, BufLen, FALSE, FALSE, NULL);
        MmBuildMdlForNonPagedPool(WskBuf.Mdl);
        WskBuf.Offset = 0;
        WskBuf.Length = BufLen;
        Status = WskConnectEx(Sock, Addr, &WskBuf, 0, Irp);
    } else {
        Status = WskConnectEx(Sock, Addr, NULL, 0, Irp);
    }

    if (Status == STATUS_PENDING) {
        Status = KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    } else if (!NT_SUCCESS(Status)) {
        LOG("WskConnectEx failed with %d\n", Status);
    }
    Status = Irp->IoStatus.Status;
    IoFreeIrp(Irp);
    if (Buf != NULL) {
        IoFreeMdl(WskBuf.Mdl);
    }
    return Status;
}

NTSTATUS
WskConnectExAsync(
    PWSK_SOCKET Sock,
    int SockType,
    PSOCKADDR Addr,
    char *Buf,
    ULONG BufLen,
    _Out_ PVOID* ConnectCompletion
    )
{
    PIRP Irp;
    NTSTATUS Status;
    PFN_WSK_CONNECT_EX WskConnectEx;
    PWSKCONNECTEX_COMPLETION Completion;

    // On success, caller must follow up with a call to WskConnectExAwait
    // (regardless of whether the call was pended). If the call was pended, the
    // client can instead call WskConnectExCancel.

    #pragma warning( suppress : 4996 )
    Completion = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*Completion), 'tseT');
    if (Completion == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(Completion, sizeof(*Completion));

    switch (SockType) {
    case WSK_FLAG_CONNECTION_SOCKET:
        WskConnectEx = ((PWSK_PROVIDER_CONNECTION_DISPATCH)Sock->Dispatch)->WskConnectEx;
        break;
    case WSK_FLAG_STREAM_SOCKET:
        WskConnectEx = ((PWSK_PROVIDER_STREAM_DISPATCH)Sock->Dispatch)->WskConnectEx;
        break;
    default:
        ASSERT(FALSE);
        return STATUS_UNSUCCESSFUL;
    }

    Irp = IoAllocateIrp(1, FALSE);
    if (Irp == NULL) {
        ExFreePool(Completion);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    KeInitializeEvent(&Completion->Event, NotificationEvent, FALSE);
    IoSetCompletionRoutine(Irp, GenericCompletionRoutine, &Completion->Event, TRUE, TRUE, TRUE);

    if (Buf != NULL) {
        Completion->WskBuf.Mdl = IoAllocateMdl(Buf, BufLen, FALSE, FALSE, NULL);
        MmBuildMdlForNonPagedPool(Completion->WskBuf.Mdl);
        Completion->WskBuf.Offset = 0;
        Completion->WskBuf.Length = BufLen;
        Status = WskConnectEx(Sock, Addr, &Completion->WskBuf, 0, Irp);
    } else {
        Status = WskConnectEx(Sock, Addr, NULL, 0, Irp);
    }
    if (NT_SUCCESS(Status)) {
        // N.B. This includes the STATUS_PENDING case.
        Completion->Status = Status;
        Completion->Irp = Irp;
        Status = STATUS_SUCCESS;
        *ConnectCompletion = Completion;
    } else {
        LOG("WskConnectEx failed with %d\n", Status);
        IoFreeIrp(Irp);
        if (Buf != NULL) {
            IoFreeMdl(Completion->WskBuf.Mdl);
        }
        ExFreePool(Completion);
    }

    return Status;
}

NTSTATUS
WskConnectExAwait(
    _Inout_ PVOID ConnectCompletion,
    _In_ ULONG TimeoutMs
    )
{
    PWSKCONNECTEX_COMPLETION Completion = (PWSKCONNECTEX_COMPLETION)ConnectCompletion;
    NTSTATUS Status = Completion->Status;
    PIRP Irp = Completion->Irp;

    ASSERT(NT_SUCCESS(Completion->Status));

    if (Completion->Status == STATUS_PENDING) {
        Status = WskWaitForIrpCompletion(&Completion->Event, Irp, TimeoutMs);
    }

    if (Status == STATUS_CANCELLED) {
        // We hit our Timeout and cancelled the Irp. STATUS_TIMEOUT is a better
        // Status in this case.
        Status = STATUS_TIMEOUT;
    }
    IoFreeIrp(Irp);
    if (Completion->WskBuf.Mdl != NULL) {
        IoFreeMdl(Completion->WskBuf.Mdl);
    }
    ExFreePool(Completion);
    return Status;
}

VOID
WskConnectExCancel(
    _Inout_ PVOID ConnectCompletion
    )
{
    PWSKCONNECTEX_COMPLETION Completion = (PWSKCONNECTEX_COMPLETION)ConnectCompletion;
    PIRP Irp = Completion->Irp;
    IoCancelIrp(Irp);
    KeWaitForSingleObject(&Completion->Event, Executive, KernelMode, FALSE, NULL);
    IoFreeIrp(Irp);

    if (Completion->WskBuf.Mdl != NULL) {
        IoFreeMdl(Completion->WskBuf.Mdl);
    }

    ExFreePool(Completion);
}

NTSTATUS
WskAcceptSync(
    PWSK_SOCKET ListenSock,
    int SockType,
    ULONG TimeoutMs,
    _Out_ PWSK_SOCKET* AcceptSock
    )
{
    PIRP Irp;
    NTSTATUS Status;
    KEVENT Event;
    PFN_WSK_ACCEPT WskAccept;

    switch (SockType) {
    case WSK_FLAG_LISTEN_SOCKET:
        WskAccept = ((PWSK_PROVIDER_LISTEN_DISPATCH)ListenSock->Dispatch)->WskAccept;
        break;
    case WSK_FLAG_STREAM_SOCKET:
        WskAccept = ((PWSK_PROVIDER_STREAM_DISPATCH)ListenSock->Dispatch)->WskAccept;
        break;
    default:
        ASSERT(FALSE);
        return STATUS_UNSUCCESSFUL;
    }

    Irp = IoAllocateIrp(1, FALSE);
    if (Irp == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoSetCompletionRoutine(Irp, GenericCompletionRoutine, &Event, TRUE, TRUE, TRUE);
    Status = WskAccept(ListenSock, 0, NULL, NULL, NULL, NULL, Irp);
    if (Status == STATUS_PENDING) {
        Status = WskWaitForIrpCompletion(&Event, Irp, TimeoutMs);
    }

    if (NT_SUCCESS(Status)) {
        *AcceptSock = (PWSK_SOCKET)Irp->IoStatus.Information;
    } else {
        LOG("WskAccept failed with %d\n", Status);
    }

    IoFreeIrp(Irp);
    return Status;
}

NTSTATUS
WskAcceptAsync(
    PWSK_SOCKET ListenSock,
    int SockType,
    _Out_ PVOID* AcceptCompletion
    )
{
    PIRP Irp;
    NTSTATUS Status;
    PFN_WSK_ACCEPT WskAccept;
    PWSKACCEPT_COMPLETION Completion;

    // On success, caller must follow up with a call to WskAcceptAwait
    // (regardless of whether the call was pended).

    #pragma warning( suppress : 4996 )
    Completion = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*Completion), 'tseT');
    if (Completion == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    switch (SockType) {
    case WSK_FLAG_LISTEN_SOCKET:
        WskAccept = ((PWSK_PROVIDER_LISTEN_DISPATCH)ListenSock->Dispatch)->WskAccept;
        break;
    case WSK_FLAG_STREAM_SOCKET:
        WskAccept = ((PWSK_PROVIDER_STREAM_DISPATCH)ListenSock->Dispatch)->WskAccept;
        break;
    default:
        ASSERT(FALSE);
        return STATUS_UNSUCCESSFUL;
    }

    Irp = IoAllocateIrp(1, FALSE);
    if (Irp == NULL) {
        ExFreePool(Completion);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    KeInitializeEvent(&Completion->Event, NotificationEvent, FALSE);
    IoSetCompletionRoutine(Irp, GenericCompletionRoutine, &Completion->Event, TRUE, TRUE, TRUE);
    Status = WskAccept(ListenSock, 0, NULL, NULL, NULL, NULL, Irp);
    if (NT_SUCCESS(Status)) {
        // N.B. This includes the STATUS_PENDING case.
        Completion->Status = Status;
        Completion->Irp = Irp;
        Status = STATUS_SUCCESS;
        *AcceptCompletion = Completion;
    } else {
        LOG("WskAccept failed with %d\n", Status);
        ExFreePool(Completion);
    }

    return Status;
}

NTSTATUS
WskAcceptAwait(
    PVOID AcceptCompletion,
    ULONG TimeoutMs,
    _Out_ PWSK_SOCKET* AcceptSock
    )
{
    PWSKACCEPT_COMPLETION Completion = (PWSKACCEPT_COMPLETION)AcceptCompletion;
    NTSTATUS Status = Completion->Status;
    PIRP Irp = Completion->Irp;

    ASSERT(NT_SUCCESS(Completion->Status));

    *AcceptSock = NULL;

    if (Completion->Status == STATUS_PENDING) {
        Status = WskWaitForIrpCompletion(&Completion->Event, Irp, TimeoutMs);
    }

    if (NT_SUCCESS(Status)) {
        *AcceptSock = (PWSK_SOCKET)Irp->IoStatus.Information;
    } else if (Status == STATUS_CANCELLED) {
        // We hit our Timeout and cancelled the Irp. STATUS_TIMEOUT is a better
        // Status in this case.
        Status = STATUS_TIMEOUT;
    }

    IoFreeIrp(Irp);
    ExFreePool(Completion);
    return Status;
}

VOID
WskAcceptCancel(
    _Inout_ PVOID AcceptCompletion
    )
{
    PWSKACCEPT_COMPLETION Completion = (PWSKACCEPT_COMPLETION)AcceptCompletion;
    PIRP Irp = Completion->Irp;
    IoCancelIrp(Irp);
    KeWaitForSingleObject(&Completion->Event, Executive, KernelMode, FALSE, NULL);
    IoFreeIrp(Irp);
    ExFreePool(Completion);
}

NTSTATUS
WskCloseSocketSync(
    PWSK_SOCKET Sock
    )
{
    PIRP Irp;
    NTSTATUS Status;
    KEVENT Event;
    PFN_WSK_CLOSE_SOCKET WskCloseSocket;

    WskCloseSocket = ((PWSK_PROVIDER_BASIC_DISPATCH)Sock->Dispatch)->WskCloseSocket;

    Irp = IoAllocateIrp(1, FALSE);
    if (Irp == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoSetCompletionRoutine(Irp, GenericCompletionRoutine, &Event, TRUE, TRUE, TRUE);
    Status = WskCloseSocket(Sock, Irp);
    if (Status == STATUS_PENDING) {
        Status = KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    } else if (!NT_SUCCESS(Status)) {
        LOG("WskCloseSocket failed with %d\n", Status);
    }
    Status = Irp->IoStatus.Status;
    IoFreeIrp(Irp);
    return Status;
}

NTSTATUS
WskDisconnectSync(
    PWSK_SOCKET Sock,
    int SockType,
    char *Buf,
    ULONG BufLen,
    BOOLEAN BufIsNonPagedPool
    )
{
    PIRP Irp;
    NTSTATUS Status;
    KEVENT Event;
    PFN_WSK_DISCONNECT WskDisconnect;
    WSK_BUF WskBuf = {0};
    BOOLEAN BufLocked = FALSE;

    switch (SockType) {
    case WSK_FLAG_CONNECTION_SOCKET:
        WskDisconnect = ((PWSK_PROVIDER_CONNECTION_DISPATCH)Sock->Dispatch)->WskDisconnect;
        break;
    case WSK_FLAG_STREAM_SOCKET:
        WskDisconnect = ((PWSK_PROVIDER_STREAM_DISPATCH)Sock->Dispatch)->WskDisconnect;
        break;
    default:
        ASSERT(FALSE);
        return STATUS_UNSUCCESSFUL;
    }

    Irp = IoAllocateIrp(1, FALSE);
    if (Irp == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoSetCompletionRoutine(Irp, GenericCompletionRoutine, &Event, TRUE, TRUE, TRUE);
    if (Buf != NULL) {
        WskBuf.Mdl = IoAllocateMdl(Buf, BufLen, FALSE, FALSE, NULL);
        if (BufIsNonPagedPool) {
            MmBuildMdlForNonPagedPool(WskBuf.Mdl);
        } else {
            try {
                MmProbeAndLockPages(WskBuf.Mdl, KernelMode, IoWriteAccess);
            } except(EXCEPTION_EXECUTE_HANDLER) {
                LOG("MmProbeAndLockPages failed\n");
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto Cleanup;
            }
            BufLocked = TRUE;
        }
        WskBuf.Offset = 0;
        WskBuf.Length = BufLen;
        Status = WskDisconnect(Sock, &WskBuf, 0, Irp);
    } else {
        Status = WskDisconnect(Sock, NULL, 0, Irp);
    }
    if (Status == STATUS_PENDING) {
        Status = KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    } else if (!NT_SUCCESS(Status)) {
        LOG("WskDisconnect failed with %d\n", Status);
    }
    Status = Irp->IoStatus.Status;
Cleanup:
    if (BufLocked) {
        MmUnlockPages(WskBuf.Mdl);
    }
    IoFreeIrp(Irp);
    if (Buf != NULL) {
        IoFreeMdl(WskBuf.Mdl);
    }
    return Status;
}

NTSTATUS
WskAbortSync(
    PWSK_SOCKET Sock,
    int SockType
    )
{
    PIRP Irp;
    NTSTATUS Status;
    KEVENT Event;
    PFN_WSK_DISCONNECT WskDisconnect;

    switch (SockType) {
    case WSK_FLAG_CONNECTION_SOCKET:
        WskDisconnect = ((PWSK_PROVIDER_CONNECTION_DISPATCH)Sock->Dispatch)->WskDisconnect;
        break;
    case WSK_FLAG_STREAM_SOCKET:
        WskDisconnect = ((PWSK_PROVIDER_STREAM_DISPATCH)Sock->Dispatch)->WskDisconnect;
        break;
    default:
        ASSERT(FALSE);
        return STATUS_UNSUCCESSFUL;
    }

    Irp = IoAllocateIrp(1, FALSE);
    if (Irp == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoSetCompletionRoutine(Irp, GenericCompletionRoutine, &Event, TRUE, TRUE, TRUE);
    Status = WskDisconnect(Sock, NULL, WSK_FLAG_ABORTIVE, Irp);
    if (Status == STATUS_PENDING) {
        Status = KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    } else if (!NT_SUCCESS(Status)) {
        LOG("WskDisconnect failed with %d\n", Status);
    }
    Status = Irp->IoStatus.Status;
    IoFreeIrp(Irp);
    return Status;
}

NTSTATUS
WskDisconnectAsync(
    PWSK_SOCKET Sock,
    int SockType,
    char *Buf,
    ULONG BufLen,
    BOOLEAN BufIsNonPagedPool,
    ULONG Flags,
    _Out_ PWSKDISCONNECT_COMPLETION* DisconnectCompletion
    )
{
    PIRP Irp;
    NTSTATUS Status;
    PFN_WSK_DISCONNECT WskDisconnect;
    PWSKDISCONNECT_COMPLETION Completion;

    // On success, caller must follow up with a call to WskDisconnectAwait,

    #pragma warning( suppress : 4996 )
    Completion = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*Completion), 'tseT');
    if (Completion == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(Completion, sizeof(*Completion));

    switch (SockType) {
    case WSK_FLAG_CONNECTION_SOCKET:
        WskDisconnect = ((PWSK_PROVIDER_CONNECTION_DISPATCH)Sock->Dispatch)->WskDisconnect;
        break;
    case WSK_FLAG_STREAM_SOCKET:
        WskDisconnect = ((PWSK_PROVIDER_STREAM_DISPATCH)Sock->Dispatch)->WskDisconnect;
        break;
    default:
        ASSERT(FALSE);
        return STATUS_UNSUCCESSFUL;
    }

    Irp = IoAllocateIrp(1, FALSE);
    if (Irp == NULL) {
        ExFreePool(Completion);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    KeInitializeEvent(&Completion->Event, NotificationEvent, FALSE);
    IoSetCompletionRoutine(Irp, GenericCompletionRoutine, &Completion->Event, TRUE, TRUE, TRUE);

    if (Buf != NULL) {
        ASSERT(Flags == 0);
        Completion->WskBuf.Mdl = IoAllocateMdl(Buf, BufLen, FALSE, FALSE, NULL);
        if (BufIsNonPagedPool) {
            MmBuildMdlForNonPagedPool(Completion->WskBuf.Mdl);
        } else {
            try {
                MmProbeAndLockPages(Completion->WskBuf.Mdl, KernelMode, IoWriteAccess);
            } except(EXCEPTION_EXECUTE_HANDLER) {
                LOG("MmProbeAndLockPages failed\n");
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto Failure;
            }
            Completion->BufLocked = TRUE;
        }
        Completion->WskBuf.Offset = 0;
        Completion->WskBuf.Length = BufLen;
        Status = WskDisconnect(Sock, &Completion->WskBuf, Flags, Irp);
    } else {
        Status = WskDisconnect(Sock, NULL, Flags, Irp);
    }

    if (NT_SUCCESS(Status)) {
        // N.B. This includes the STATUS_PENDING case.
        Completion->Status = Status;
        Completion->Irp = Irp;
        Status = STATUS_SUCCESS;
        *DisconnectCompletion = Completion;
        return Status;
    }

    LOG("WskDisconnect failed with %d\n", Status);
Failure:
    if (Completion->BufLocked) {
        MmUnlockPages(Completion->WskBuf.Mdl);
    }
    IoFreeIrp(Irp);
    if (Completion->WskBuf.Mdl != NULL) {
        IoFreeMdl(Completion->WskBuf.Mdl);
    }
    ExFreePool(Completion);

    return Status;
}

NTSTATUS
WskDisconnectAwait(
    _Inout_ PWSKDISCONNECT_COMPLETION Completion,
    _In_ ULONG TimeoutMs
    )
{
    NTSTATUS Status = Completion->Status;
    PIRP Irp = Completion->Irp;

    ASSERT(NT_SUCCESS(Completion->Status));

    if (Completion->Status == STATUS_PENDING) {
        Status = WskWaitForIrpCompletion(&Completion->Event, Irp, TimeoutMs);
    }

    if (Status == STATUS_CANCELLED) {
        // We hit our Timeout and cancelled the Irp. STATUS_TIMEOUT is a better
        // Status in this case.
        Status = STATUS_TIMEOUT;
    }

    if (Completion->BufLocked) {
        MmUnlockPages(Completion->WskBuf.Mdl);
    }
    IoFreeIrp(Irp);
    if (Completion->WskBuf.Mdl != NULL) {
        IoFreeMdl(Completion->WskBuf.Mdl);
    }
    ExFreePool(Completion);
    return Status;
}

NTSTATUS
WskSendSync(
    PWSK_SOCKET Sock,
    int SockType,
    char *Buf,
    ULONG BufLen,
    BOOLEAN BufIsNonPagedPool,
    ULONG ExpectedBytesSent,
    ULONG Flags,
    _Out_opt_ ULONG *BytesSent
    )
{
    PIRP Irp = NULL;
    NTSTATUS Status;
    KEVENT Event;
    PFN_WSK_SEND WskSend;
    WSK_BUF WskBuf = {0};
    BOOLEAN BufLocked = FALSE;

#if !DBG
    UNREFERENCED_PARAMETER(ExpectedBytesSent);
#endif

    ASSERT(BufLen > 0);

    WskBuf.Mdl = IoAllocateMdl(Buf, BufLen, FALSE, FALSE, NULL);
    if (BufIsNonPagedPool) {
        MmBuildMdlForNonPagedPool(WskBuf.Mdl);
    } else {
        try {
            MmProbeAndLockPages(WskBuf.Mdl, KernelMode, IoWriteAccess);
        } except(EXCEPTION_EXECUTE_HANDLER) {
            LOG("MmProbeAndLockPages failed\n");
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
        BufLocked = TRUE;
    }
    WskBuf.Offset = 0;
    WskBuf.Length = BufLen;

    switch (SockType) {
    case WSK_FLAG_CONNECTION_SOCKET:
        WskSend = ((PWSK_PROVIDER_CONNECTION_DISPATCH)Sock->Dispatch)->WskSend;
        break;
    case WSK_FLAG_STREAM_SOCKET:
        WskSend = ((PWSK_PROVIDER_STREAM_DISPATCH)Sock->Dispatch)->WskSend;
        break;
    default:
        ASSERT(FALSE);
        Status = STATUS_UNSUCCESSFUL;
        goto Cleanup;
    }

    Irp = IoAllocateIrp(1, FALSE);
    if (Irp == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoSetCompletionRoutine(Irp, GenericCompletionRoutine, &Event, TRUE, TRUE, TRUE);
    Status = WskSend(Sock, &WskBuf, Flags, Irp);
    if (Status == STATUS_PENDING) {
        Status =
            KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    } else if (!NT_SUCCESS(Status)) {
        LOG("WskSend failed with %d\n", Status);
    }
    Status = Irp->IoStatus.Status;
    if (BytesSent != NULL) {
        *BytesSent = (ULONG)Irp->IoStatus.Information;
    }
    if (ExpectedBytesSent != WSKCLIENT_UNKNOWN_BYTES) {
        ASSERT((ULONG)Irp->IoStatus.Information == ExpectedBytesSent);
    }
Cleanup:
    if (Irp != NULL) {
        IoFreeIrp(Irp);
    }
    if (WskBuf.Mdl != NULL) {
        if (BufLocked) {
            MmUnlockPages(WskBuf.Mdl);
        }
        IoFreeMdl(WskBuf.Mdl);
    }
    return Status;
}

NTSTATUS
WskSendExSync(
    PWSK_SOCKET Sock,
    int SockType,
    char *Buf,
    ULONG BufLen,
    char* Control,
    ULONG ControlLen,
    ULONG ExpectedBytesSent,
    ULONG Flags,
    _Out_opt_ ULONG *BytesSent
    )
{
    PIRP Irp;
    NTSTATUS Status;
    KEVENT Event;
    PFN_WSK_SEND_EX WskSendEx;
    WSK_BUF WskBuf;

#if !DBG
    UNREFERENCED_PARAMETER(ExpectedBytesSent);
#endif

    ASSERT(BufLen > 0);

    WskBuf.Mdl = IoAllocateMdl(Buf, BufLen, FALSE, FALSE, NULL);
    MmBuildMdlForNonPagedPool(WskBuf.Mdl);
    WskBuf.Offset = 0;
    WskBuf.Length = BufLen;

    switch (SockType) {
    case WSK_FLAG_CONNECTION_SOCKET:
        WskSendEx = ((PWSK_PROVIDER_CONNECTION_DISPATCH)Sock->Dispatch)->WskSendEx;
        break;
    case WSK_FLAG_STREAM_SOCKET:
        WskSendEx = ((PWSK_PROVIDER_STREAM_DISPATCH)Sock->Dispatch)->WskSendEx;
        break;
    default:
        ASSERT(FALSE);
        return STATUS_UNSUCCESSFUL;
    }

    Irp = IoAllocateIrp(1, FALSE);
    if (Irp == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoSetCompletionRoutine(Irp, GenericCompletionRoutine, &Event, TRUE, TRUE, TRUE);
    Status = WskSendEx(Sock, &WskBuf, Flags, ControlLen, (PCMSGHDR)Control, Irp);
    if (Status == STATUS_PENDING) {
        Status =
            KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    } else if (!NT_SUCCESS(Status)) {
        LOG("WskSendEx failed with %d\n", Status);
    }
    Status = Irp->IoStatus.Status;
    if (BytesSent != NULL) {
        *BytesSent = (ULONG)Irp->IoStatus.Information;
    }
    if (ExpectedBytesSent != WSKCLIENT_UNKNOWN_BYTES) {
        ASSERT((ULONG)Irp->IoStatus.Information == ExpectedBytesSent);
    }
    IoFreeIrp(Irp);
    IoFreeMdl(WskBuf.Mdl);
    return Status;
}

NTSTATUS
WskSendAsync(
    PWSK_SOCKET Sock,
    int SockType,
    char *Buf,
    ULONG BufLen,
    ULONG Flags,
    _Out_ PVOID* SendCompletion
    )
{
    PIRP Irp;
    NTSTATUS Status;
    PFN_WSK_SEND WskSend;
    PWSKIOREQUEST_COMPLETION Completion;
    ASSERT(BufLen > 0);

    // On success, caller must follow up with a call to WskSendAwait.

    #pragma warning( suppress : 4996 )
    Completion = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*Completion), 'tseT');
    if (Completion == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(Completion, sizeof(*Completion));

    switch (SockType) {
    case WSK_FLAG_CONNECTION_SOCKET:
        WskSend = ((PWSK_PROVIDER_CONNECTION_DISPATCH)Sock->Dispatch)->WskSend;
        break;
    case WSK_FLAG_STREAM_SOCKET:
        WskSend = ((PWSK_PROVIDER_STREAM_DISPATCH)Sock->Dispatch)->WskSend;
        break;
    default:
        ASSERT(FALSE);
        return STATUS_UNSUCCESSFUL;
    }

    Irp = IoAllocateIrp(1, FALSE);
    if (Irp == NULL) {
        ExFreePool(Completion);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    KeInitializeEvent(&Completion->Event, NotificationEvent, FALSE);
    IoSetCompletionRoutine(Irp, GenericCompletionRoutine, &Completion->Event, TRUE, TRUE, TRUE);

    Completion->WskBuf.Mdl = IoAllocateMdl(Buf, BufLen, FALSE, FALSE, NULL);
    MmBuildMdlForNonPagedPool(Completion->WskBuf.Mdl);
    Completion->WskBuf.Offset = 0;
    Completion->WskBuf.Length = BufLen;

    Status = WskSend(Sock, &Completion->WskBuf, Flags, Irp);

    if (NT_SUCCESS(Status)) {
        // N.B. This includes the STATUS_PENDING case.
        Completion->Status = Status;
        Completion->Irp = Irp;
        Status = STATUS_SUCCESS;
        *SendCompletion = Completion;
    } else {
        LOG("WskSend failed with %d\n", Status);
        IoFreeIrp(Irp);
        if (Completion->WskBuf.Mdl != NULL) {
            IoFreeMdl(Completion->WskBuf.Mdl);
        }
        ExFreePool(Completion);
    }

    return Status;
}

NTSTATUS
WskSendAwait(
    _Inout_ PVOID SendCompletion,
    _In_ ULONG TimeoutMs,
    _In_ ULONG ExpectedBytesSent,
    _Out_opt_ ULONG *BytesSent
    )
{
#if !DBG
    UNREFERENCED_PARAMETER(ExpectedBytesSent);
#endif

    PWSKIOREQUEST_COMPLETION Completion = (PWSKIOREQUEST_COMPLETION)SendCompletion;
    NTSTATUS Status = Completion->Status;
    PIRP Irp = Completion->Irp;

    ASSERT(NT_SUCCESS(Completion->Status));

    if (BytesSent != NULL) {
        *BytesSent = 0;
    }

    if (Completion->Status == STATUS_PENDING) {
        Status = WskWaitForIrpCompletion(&Completion->Event, Irp, TimeoutMs);
    }

    if (!NT_SUCCESS(Status))
    {
        LOG("WskSend IO failed with %d\n", Status);
        if (Status == STATUS_CANCELLED)
        {
            // We hit our Timeout and cancelled the Irp. STATUS_TIMEOUT is a better
            // Status in this case.
            Status = STATUS_TIMEOUT;
        }
    }
    else
    {
        if (BytesSent != NULL) {
            *BytesSent = (ULONG)Irp->IoStatus.Information;
        }
        if (ExpectedBytesSent != WSKCLIENT_UNKNOWN_BYTES) {
            ASSERT((ULONG)Irp->IoStatus.Information == ExpectedBytesSent);
        }
    }

    IoFreeIrp(Irp);
    if (Completion->WskBuf.Mdl != NULL) {
        if (Completion->BufLocked) {
            MmUnlockPages(Completion->WskBuf.Mdl);
        }
        IoFreeMdl(Completion->WskBuf.Mdl);
    }
    ExFreePool(Completion);
    return Status;
}

NTSTATUS
WskReceiveSync(
    PWSK_SOCKET Sock,
    int SockType,
    char *Buf,
    ULONG BufLen,
    BOOLEAN BufIsNonPagedPool,
    ULONG ExpectedBytesReceived,
    ULONG TimeoutMs,
    _Out_opt_ ULONG *BytesReceived
    )
{
    PIRP Irp = NULL;
    NTSTATUS Status;
    KEVENT Event;
    PFN_WSK_RECEIVE WskReceive = NULL;
    PFN_WSK_RECEIVE_FROM WskReceiveFrom = NULL;
    WSK_BUF WskBuf = {0};
    BOOLEAN BufLocked = FALSE;

#if !DBG
    UNREFERENCED_PARAMETER(ExpectedBytesReceived);
#endif

    ASSERT(BufLen > 0);

    if (BytesReceived != NULL) {
        *BytesReceived = 0;
    }

    WskBuf.Mdl = IoAllocateMdl(Buf, BufLen, FALSE, FALSE, NULL);
    if (BufIsNonPagedPool) {
        MmBuildMdlForNonPagedPool(WskBuf.Mdl);
    } else {
        try {
            MmProbeAndLockPages(WskBuf.Mdl, KernelMode, IoWriteAccess);
        } except(EXCEPTION_EXECUTE_HANDLER) {
            LOG("MmProbeAndLockPages failed\n");
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
        BufLocked = TRUE;
    }
    WskBuf.Offset = 0;
    WskBuf.Length = BufLen;

    switch (SockType) {
    case WSK_FLAG_CONNECTION_SOCKET:
        WskReceive = ((PWSK_PROVIDER_CONNECTION_DISPATCH)Sock->Dispatch)->WskReceive;
        break;
    case WSK_FLAG_STREAM_SOCKET:
        WskReceive = ((PWSK_PROVIDER_STREAM_DISPATCH)Sock->Dispatch)->WskReceive;
        break;
    case WSK_FLAG_DATAGRAM_SOCKET:
        WskReceiveFrom = ((PWSK_PROVIDER_DATAGRAM_DISPATCH)Sock->Dispatch)->WskReceiveFrom;
        break;
    default:
        ASSERT(FALSE);
        Status = STATUS_UNSUCCESSFUL;
        goto Cleanup;
    }

    Irp = IoAllocateIrp(1, FALSE);
    if (Irp == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoSetCompletionRoutine(Irp, GenericCompletionRoutine, &Event, TRUE, TRUE, TRUE);

    if (SockType == WSK_FLAG_DATAGRAM_SOCKET) {
        ULONG ControlLen = 0;
        Status = WskReceiveFrom(Sock, &WskBuf, 0, NULL, &ControlLen, NULL, NULL, Irp);
    } else {
        Status = WskReceive(Sock, &WskBuf, WSK_FLAG_WAITALL, Irp);
    }

    if (Status == STATUS_PENDING) {
        Status = WskWaitForIrpCompletion(&Event, Irp, TimeoutMs);
    }

    if (NT_SUCCESS(Status)) {
        if (BytesReceived != NULL) {
            *BytesReceived = (ULONG)Irp->IoStatus.Information;
        }
        if (ExpectedBytesReceived != WSKCLIENT_UNKNOWN_BYTES) {
            ASSERT((ULONG)Irp->IoStatus.Information == ExpectedBytesReceived);
        }
    } else {
        LOG("WskReceive failed with %d\n", Status);
        if (Status == STATUS_CANCELLED) {
            // We hit our Timeout and cancelled the Irp. STATUS_TIMEOUT is a better
            // Status in this case.
            Status = STATUS_TIMEOUT;
        }
    }
Cleanup:
    if (Irp != NULL) {
        IoFreeIrp(Irp);
    }
    if (WskBuf.Mdl != NULL) {
        if (BufLocked) {
            MmUnlockPages(WskBuf.Mdl);
        }
        IoFreeMdl(WskBuf.Mdl);
    }
    return Status;
}

NTSTATUS
WskReceiveExSync(
    PWSK_SOCKET Sock,
    int SockType,
    char *Buf,
    ULONG BufLen,
    PCMSGHDR Control,
    PULONG ControlLen,
    ULONG ExpectedBytesReceived,
    ULONG TimeoutMs,
    _Out_opt_ ULONG *BytesReceived
    )
{
    PIRP Irp;
    NTSTATUS Status;
    KEVENT Event;
    PFN_WSK_RECEIVE_EX WskReceiveEx;
    WSK_BUF WskBuf;

#if !DBG
    UNREFERENCED_PARAMETER(ExpectedBytesReceived);
#endif

    ASSERT(BufLen > 0);

    if (BytesReceived != NULL) {
        *BytesReceived = 0;
    }

    WskBuf.Mdl = IoAllocateMdl(Buf, BufLen, FALSE, FALSE, NULL);
    MmBuildMdlForNonPagedPool(WskBuf.Mdl);
    WskBuf.Offset = 0;
    WskBuf.Length = BufLen;

    switch (SockType) {
    case WSK_FLAG_CONNECTION_SOCKET:
        WskReceiveEx = ((PWSK_PROVIDER_CONNECTION_DISPATCH)Sock->Dispatch)->WskReceiveEx;
        break;
    case WSK_FLAG_STREAM_SOCKET:
        WskReceiveEx = ((PWSK_PROVIDER_STREAM_DISPATCH)Sock->Dispatch)->WskReceiveEx;
        break;
    default:
        ASSERT(FALSE);
        return STATUS_UNSUCCESSFUL;
    }

    Irp = IoAllocateIrp(1, FALSE);
    if (Irp == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoSetCompletionRoutine(Irp, GenericCompletionRoutine, &Event, TRUE, TRUE, TRUE);

    Status = WskReceiveEx(Sock, &WskBuf, WSK_FLAG_WAITALL, ControlLen, Control, NULL, Irp);

    if (Status == STATUS_PENDING) {
        Status = WskWaitForIrpCompletion(&Event, Irp, TimeoutMs);
    }

    if (NT_SUCCESS(Status)) {
        if (BytesReceived != NULL) {
            *BytesReceived = (ULONG)Irp->IoStatus.Information;
        }
        if (ExpectedBytesReceived != WSKCLIENT_UNKNOWN_BYTES) {
            ASSERT((ULONG)Irp->IoStatus.Information == ExpectedBytesReceived);
        }
    } else {
        LOG("WskReceiveEx failed with %d\n", Status);
        if (Status == STATUS_CANCELLED) {
            // We hit our Timeout and cancelled the Irp. STATUS_TIMEOUT is a better
            // Status in this case.
            Status = STATUS_TIMEOUT;
        }
    }

    IoFreeIrp(Irp);
    IoFreeMdl(WskBuf.Mdl);
    return Status;
}

NTSTATUS
WskReceiveAsync(
    PWSK_SOCKET Sock,
    int SockType,
    char *Buf,
    ULONG BufLen,
    _Out_ PVOID* ReceiveCompletion
    )
{
    PIRP Irp;
    NTSTATUS Status;
    PFN_WSK_RECEIVE WskReceive;
    PWSKIOREQUEST_COMPLETION Completion;
    ASSERT(BufLen > 0);

    // On success, caller must follow up with a call to WskSendAwait.

    #pragma warning( suppress : 4996 )
    Completion = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*Completion), 'tseT');
    if (Completion == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(Completion, sizeof(*Completion));

    switch (SockType) {
    case WSK_FLAG_CONNECTION_SOCKET:
        WskReceive = ((PWSK_PROVIDER_CONNECTION_DISPATCH)Sock->Dispatch)->WskReceive;
        break;
    case WSK_FLAG_STREAM_SOCKET:
        WskReceive = ((PWSK_PROVIDER_STREAM_DISPATCH)Sock->Dispatch)->WskReceive;
        break;
    default:
        ASSERT(FALSE);
        return STATUS_UNSUCCESSFUL;
    }

    Irp = IoAllocateIrp(1, FALSE);
    if (Irp == NULL) {
        ExFreePool(Completion);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    KeInitializeEvent(&Completion->Event, NotificationEvent, FALSE);
    IoSetCompletionRoutine(Irp, GenericCompletionRoutine, &Completion->Event, TRUE, TRUE, TRUE);

    Completion->WskBuf.Mdl = IoAllocateMdl(Buf, BufLen, FALSE, FALSE, NULL);
    MmBuildMdlForNonPagedPool(Completion->WskBuf.Mdl);
    Completion->WskBuf.Offset = 0;
    Completion->WskBuf.Length = BufLen;

    Status = WskReceive(Sock, &Completion->WskBuf, WSK_FLAG_WAITALL, Irp);

    if (NT_SUCCESS(Status)) {
        // N.B. This includes the STATUS_PENDING case.
        Completion->Status = Status;
        Completion->Irp = Irp;
        Status = STATUS_SUCCESS;
        *ReceiveCompletion = Completion;
    } else {
        LOG("WskReceive failed with %d\n", Status);
        IoFreeIrp(Irp);
        if (Completion->WskBuf.Mdl != NULL) {
            IoFreeMdl(Completion->WskBuf.Mdl);
        }
        ExFreePool(Completion);
    }

    return Status;
}

NTSTATUS
WskReceiveAwait(
    _Inout_ PVOID ReceiveCompletion,
    _In_ ULONG TimeoutMs,
    _In_ ULONG ExpectedBytesReceived,
    _Out_opt_ ULONG *BytesReceived
    )
{
#if !DBG
    UNREFERENCED_PARAMETER(ExpectedBytesReceived);
#endif

    PWSKIOREQUEST_COMPLETION Completion = (PWSKIOREQUEST_COMPLETION)ReceiveCompletion;
    NTSTATUS Status = Completion->Status;
    PIRP Irp = Completion->Irp;

    ASSERT(NT_SUCCESS(Completion->Status));

    if (BytesReceived != NULL) {
        *BytesReceived = 0;
    }

    if (Completion->Status == STATUS_PENDING) {
        Status = WskWaitForIrpCompletion(&Completion->Event, Irp, TimeoutMs);
    }

    if (!NT_SUCCESS(Status))
    {
        if (Status == STATUS_CANCELLED)
        {
            // We hit our Timeout and cancelled the Irp. STATUS_TIMEOUT is a better
            // Status in this case.
            Status = STATUS_TIMEOUT;
        }

        LOG("WskReceive IO failed with %d\n", Status);
    }
    else
    {
        if (BytesReceived != NULL) {
            *BytesReceived = (ULONG)Irp->IoStatus.Information;
        }
        if (ExpectedBytesReceived != WSKCLIENT_UNKNOWN_BYTES) {
            ASSERT((ULONG)Irp->IoStatus.Information == ExpectedBytesReceived);
        }
    }

    IoFreeIrp(Irp);
    if (Completion->WskBuf.Mdl != NULL) {
        IoFreeMdl(Completion->WskBuf.Mdl);
    }
    ExFreePool(Completion);
    return Status;
}

NTSTATUS
WskSendToSync(
    PWSK_SOCKET Sock,
    char *Buf,
    ULONG BufLen,
    BOOLEAN BufIsNonPagedPool,
    ULONG ExpectedBytesSent,
    _In_ PSOCKADDR remoteaddr,
    ULONG ControlLen,
    _In_opt_ PCMSGHDR Control,
    _Out_opt_ ULONG *BytesSent
    )
{
    PIRP Irp = NULL;
    NTSTATUS Status;
    KEVENT Event;
    PFN_WSK_SEND_TO WskSendTo;
    WSK_BUF WskBuf = {0};
    BOOLEAN BufLocked = FALSE;

#if !DBG
    UNREFERENCED_PARAMETER(ExpectedBytesSent);
#endif

    ASSERT(BufLen > 0);

    WskBuf.Mdl = IoAllocateMdl(Buf, BufLen, FALSE, FALSE, NULL);
    if (BufIsNonPagedPool) {
        MmBuildMdlForNonPagedPool(WskBuf.Mdl);
    } else {
        try {
            MmProbeAndLockPages(WskBuf.Mdl, KernelMode, IoWriteAccess);
        } except(EXCEPTION_EXECUTE_HANDLER) {
            LOG("MmProbeAndLockPages failed\n");
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
        BufLocked = TRUE;
    }
    WskBuf.Offset = 0;
    WskBuf.Length = BufLen;

    WskSendTo = ((PWSK_PROVIDER_DATAGRAM_DISPATCH)Sock->Dispatch)->WskSendTo;
    Irp = IoAllocateIrp(1, FALSE);
    if (Irp == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoSetCompletionRoutine(Irp, GenericCompletionRoutine, &Event, TRUE, TRUE, TRUE);
    Status = WskSendTo(Sock, &WskBuf, 0, remoteaddr, ControlLen, Control, Irp);
    if (Status == STATUS_PENDING) {
        Status =
            KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    } else if (!NT_SUCCESS(Status)) {
        LOG("WskSend failed with %d\n", Status);
    }
    Status = Irp->IoStatus.Status;
    if (BytesSent != NULL) {
        *BytesSent = (ULONG)Irp->IoStatus.Information;
    }
    if (ExpectedBytesSent != WSKCLIENT_UNKNOWN_BYTES) {
        ASSERT((ULONG)Irp->IoStatus.Information == ExpectedBytesSent);
    }
Cleanup:
    if (Irp != NULL) {
        IoFreeIrp(Irp);
    }
    if (WskBuf.Mdl != NULL) {
        if (BufLocked) {
            MmUnlockPages(WskBuf.Mdl);
        }
        IoFreeMdl(WskBuf.Mdl);
    }
    return Status;
}

NTSTATUS
WskSendToAsync(
    PWSK_SOCKET Sock,
    char *Buf,
    ULONG BufLen,
    BOOLEAN BufIsNonPagedPool,
    _In_ PSOCKADDR RemoteAddr,
    ULONG ControlLen,
    _In_opt_ PCMSGHDR Control,
    _Out_ PVOID* SendCompletion
    )
{
    PIRP Irp = NULL;
    NTSTATUS Status;
    PFN_WSK_SEND_TO WskSendTo;
    PWSKIOREQUEST_COMPLETION Completion = NULL;

    // On success, caller must follow up with a call to WskSendToAwait.

#if !DBG
    UNREFERENCED_PARAMETER(ExpectedBytesSent);
#endif

    ASSERT(BufLen > 0);

    #pragma warning( suppress : 4996 )
    Completion = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*Completion), 'tseT');
    if (Completion == NULL) {
        LOG("ExAllocatePool2 failed\n");
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    RtlZeroMemory(Completion, sizeof(*Completion));

    Completion->WskBuf.Mdl = IoAllocateMdl(Buf, BufLen, FALSE, FALSE, NULL);
    if (BufIsNonPagedPool) {
        MmBuildMdlForNonPagedPool(Completion->WskBuf.Mdl);
    } else {
        try {
            MmProbeAndLockPages(Completion->WskBuf.Mdl, KernelMode, IoWriteAccess);
        } except(EXCEPTION_EXECUTE_HANDLER) {
            LOG("MmProbeAndLockPages failed\n");
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
        Completion->BufLocked = TRUE;
    }
    Completion->WskBuf.Offset = 0;
    Completion->WskBuf.Length = BufLen;

    WskSendTo = ((PWSK_PROVIDER_DATAGRAM_DISPATCH)Sock->Dispatch)->WskSendTo;
    Irp = IoAllocateIrp(1, FALSE);
    if (Irp == NULL) {
        LOG("IoAllocateIrp failed\n");
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    KeInitializeEvent(&Completion->Event, NotificationEvent, FALSE);
    IoSetCompletionRoutine(Irp, GenericCompletionRoutine, &Completion->Event, TRUE, TRUE, TRUE);
    Status = WskSendTo(Sock, &Completion->WskBuf, 0, RemoteAddr, ControlLen, Control, Irp);
    if (!NT_SUCCESS(Status)) {
        LOG("WskSendTo failed with %d\n", Status);
        goto Cleanup;
    }

    // N.B. This includes the STATUS_PENDING case.
    Completion->Status = Status;
    Completion->Irp = Irp;
    Status = STATUS_SUCCESS;
    *SendCompletion = Completion;

Cleanup:
    if (!NT_SUCCESS(Status)) {
        if (Completion != NULL) {
            if (Irp != NULL) {
                IoFreeIrp(Irp);
            }
            if (Completion->WskBuf.Mdl != NULL) {
                if (Completion->BufLocked) {
                    MmUnlockPages(Completion->WskBuf.Mdl);
                }
                IoFreeMdl(Completion->WskBuf.Mdl);
            }
            ExFreePool(Completion);
        }
    }
    return Status;
}

NTSTATUS
WskSendToAwait(
    _Inout_ PVOID SendCompletion,
    _In_ ULONG TimeoutMs,
    _In_ ULONG ExpectedBytesSent,
    _Out_opt_ ULONG *BytesSent
    )
{
    return WskSendAwait(SendCompletion, TimeoutMs, ExpectedBytesSent, BytesSent);
}

NTSTATUS
WskControlSocketSync(
    PWSK_SOCKET Sock,
    int SockType,
    WSK_CONTROL_SOCKET_TYPE RequestType, // WskSetOption, WskGetOption or WskIoctl
    ULONG ControlCode,
    ULONG Level, // e.g. SOL_SOCKET
    SIZE_T InputSize,
    PVOID InputBuf,
    SIZE_T OutputSize,
    _Out_opt_ PVOID OutputBuf,
    _Out_opt_ SIZE_T *OutputSizeReturned
    )
{
    PIRP Irp;
    NTSTATUS Status;
    KEVENT Event;
    PFN_WSK_CONTROL_SOCKET WskControlSocket;

    switch (SockType) {
    case WSK_FLAG_CONNECTION_SOCKET:
        WskControlSocket = ((PWSK_PROVIDER_CONNECTION_DISPATCH)Sock->Dispatch)->WskControlSocket;
        break;
    case WSK_FLAG_LISTEN_SOCKET:
        WskControlSocket = ((PWSK_PROVIDER_LISTEN_DISPATCH)Sock->Dispatch)->WskControlSocket;
        break;
    case WSK_FLAG_DATAGRAM_SOCKET:
        WskControlSocket = ((PWSK_PROVIDER_DATAGRAM_DISPATCH)Sock->Dispatch)->WskControlSocket;
        break;
    case WSK_FLAG_STREAM_SOCKET:
        WskControlSocket = ((PWSK_PROVIDER_STREAM_DISPATCH)Sock->Dispatch)->WskControlSocket;
        break;
    case WSK_FLAG_BASIC_SOCKET:
        WskControlSocket = ((PWSK_PROVIDER_BASIC_DISPATCH)Sock->Dispatch)->WskControlSocket;
        break;
    default:
        ASSERT(FALSE);
        return STATUS_UNSUCCESSFUL;
    }

    Irp = IoAllocateIrp(1, FALSE);
    if (Irp == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoSetCompletionRoutine(Irp, GenericCompletionRoutine, &Event, TRUE, TRUE, TRUE);
    Status =
        WskControlSocket(
            Sock, RequestType, ControlCode, Level, InputSize, InputBuf,
            OutputSize, OutputBuf, OutputSizeReturned, Irp);
    if (Status == STATUS_PENDING) {
        Status =
            KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    } else if (!NT_SUCCESS(Status)) {
        LOG("WskControlSocket failed with %d\n", Status);
    }
    Status = Irp->IoStatus.Status;
    if (OutputSizeReturned != NULL) {
        *OutputSizeReturned = Irp->IoStatus.Information;
    }
    IoFreeIrp(Irp);
    return Status;
}

NTSTATUS
WskGetLocalAddrSync(
    PWSK_SOCKET Sock,
    int SockType,
    PSOCKADDR Addr
    )
{
    PIRP Irp;
    NTSTATUS Status;
    KEVENT Event;
    PFN_WSK_GET_LOCAL_ADDRESS WskGetLocalAddr;

    switch (SockType) {
    case WSK_FLAG_CONNECTION_SOCKET:
        WskGetLocalAddr = ((PWSK_PROVIDER_CONNECTION_DISPATCH)Sock->Dispatch)->WskGetLocalAddress;
        break;
    case WSK_FLAG_STREAM_SOCKET:
        WskGetLocalAddr = ((PWSK_PROVIDER_STREAM_DISPATCH)Sock->Dispatch)->WskGetLocalAddress;
        break;
    case WSK_FLAG_LISTEN_SOCKET:
        WskGetLocalAddr = ((PWSK_PROVIDER_LISTEN_DISPATCH)Sock->Dispatch)->WskGetLocalAddress;
        break;
    case WSK_FLAG_DATAGRAM_SOCKET:
        WskGetLocalAddr = ((PWSK_PROVIDER_DATAGRAM_DISPATCH)Sock->Dispatch)->WskGetLocalAddress;
        break;
    default:
        ASSERT(FALSE);
        return STATUS_UNSUCCESSFUL;
    }

    Irp = IoAllocateIrp(1, FALSE);
    if (Irp == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoSetCompletionRoutine(Irp, GenericCompletionRoutine, &Event, TRUE, TRUE, TRUE);
    Status = WskGetLocalAddr(Sock, Addr, Irp);
    if (Status == STATUS_PENDING) {
        Status = KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    } else if (!NT_SUCCESS(Status)) {
        LOG("WskGetLocalAddr failed with %d\n", Status);
    }
    Status = Irp->IoStatus.Status;
    IoFreeIrp(Irp);
    return Status;
}

NTSTATUS
WskGetRemoteAddrSync(
    PWSK_SOCKET Sock,
    int SockType,
    PSOCKADDR Addr
    )
{
    PIRP Irp;
    NTSTATUS Status;
    KEVENT Event;
    PFN_WSK_GET_REMOTE_ADDRESS WskGetRemoteAddr;

    switch (SockType) {
    case WSK_FLAG_CONNECTION_SOCKET:
        WskGetRemoteAddr = ((PWSK_PROVIDER_CONNECTION_DISPATCH)Sock->Dispatch)->WskGetRemoteAddress;
        break;
    case WSK_FLAG_STREAM_SOCKET:
        WskGetRemoteAddr = ((PWSK_PROVIDER_STREAM_DISPATCH)Sock->Dispatch)->WskGetRemoteAddress;
        break;
    default:
        ASSERT(FALSE);
        return STATUS_UNSUCCESSFUL;
    }

    Irp = IoAllocateIrp(1, FALSE);
    if (Irp == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoSetCompletionRoutine(Irp, GenericCompletionRoutine, &Event, TRUE, TRUE, TRUE);
    Status = WskGetRemoteAddr(Sock, Addr, Irp);
    if (Status == STATUS_PENDING) {
        Status = KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    } else if (!NT_SUCCESS(Status)) {
        LOG("WskGetRemoteAddr failed with %d\n", Status);
    }
    Status = Irp->IoStatus.Status;
    IoFreeIrp(Irp);
    return Status;
}

NTSTATUS
WskEnableCallbacks(
    PWSK_SOCKET Sock,
    int SockType,
    ULONG EventMask
    )
{
    WSK_EVENT_CALLBACK_CONTROL Control = {0};
    NTSTATUS Status;

    Control.NpiId = &NPI_WSK_INTERFACE_ID;
    Control.EventMask = EventMask;
    Status = WskControlSocketSync(
        Sock, SockType, WskSetOption, SO_WSK_EVENT_CALLBACK, SOL_SOCKET,
        sizeof(WSK_EVENT_CALLBACK_CONTROL), &Control, 0, NULL, NULL);

    return Status;
}
