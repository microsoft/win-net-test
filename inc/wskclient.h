#pragma once

EXTERN_C_START

#define WSKCLIENT_UNKNOWN_BYTES ((ULONG)-1)
#define WSKCLIENT_INFINITE ((ULONG)-1)

typedef struct _WSKCONNECTEX_COMPLETION {
    PIRP Irp;
    WSK_BUF WskBuf;
    KEVENT Event;
    NTSTATUS Status;
    BOOLEAN BufLocked;
} WSKCONNECTEX_COMPLETION, *PWSKCONNECTEX_COMPLETION;

typedef struct _WSKDISCONNECT_COMPLETION {
    PIRP Irp;
    WSK_BUF WskBuf;
    KEVENT Event;
    NTSTATUS Status;
    BOOLEAN BufLocked;
} WSKDISCONNECT_COMPLETION, *PWSKDISCONNECT_COMPLETION;

typedef struct _WSKACCEPT_COMPLETION {
    PIRP Irp;
    KEVENT Event;
    NTSTATUS Status;
} WSKACCEPT_COMPLETION, *PWSKACCEPT_COMPLETION;

typedef struct _WSK_IOREQUEST_COMPLETION {
    PIRP Irp;
    WSK_BUF WskBuf;
    KEVENT Event;
    NTSTATUS Status;
    BOOLEAN BufLocked;
} WSKIOREQUEST_COMPLETION, *PWSKIOREQUEST_COMPLETION;

NTSTATUS
WskClientReg(
    VOID
    );

VOID
WskClientDereg(
    VOID
    );

NTSTATUS
WskSocketSync(
    int WskSockType, // e.g. WSK_FLAG_STREAM_SOCKET
    ADDRESS_FAMILY Family,
    USHORT SockType, // e.g. SOCK_STREAM
    IPPROTO Protocol,
    _Out_ PWSK_SOCKET* Sock,
    _In_opt_ PVOID Context,
    _In_opt_ PVOID Dispatch
    );

NTSTATUS
WskSocketConnectSync(
    USHORT SockType, // e.g. SOCK_STREAM
    IPPROTO Protocol,
    PSOCKADDR LocalAddr,
    PSOCKADDR RemoteAddr,
    _Out_ PWSK_SOCKET* Sock,
    _In_opt_ PVOID Context,
    _In_opt_ WSK_CLIENT_CONNECTION_DISPATCH* Dispatch
    );

NTSTATUS
WskBindSync(
    PWSK_SOCKET Sock,
    int SockType,
    PSOCKADDR Addr
    );

NTSTATUS
WskListenSync(
    PWSK_SOCKET Sock,
    int SockType
    );

NTSTATUS
WskConnectSync(
    PWSK_SOCKET Sock,
    int SockType,
    PSOCKADDR Addr
    );

NTSTATUS
WskConnectExSync(
    PWSK_SOCKET Sock,
    int SockType,
    PSOCKADDR Addr,
    char *Buf,
    ULONG BufLen,
    BOOLEAN BufIsNonPagedPool
    );

NTSTATUS
WskConnectExAsync(
    PWSK_SOCKET Sock,
    int SockType,
    PSOCKADDR Addr,
    char *Buf,
    ULONG BufLen,
    BOOLEAN BufIsNonPagedPool,
    _Out_ PVOID* ConnectCompletion
    );

NTSTATUS
WskConnectExAwait(
    _Inout_ PVOID ConnectCompletion,
    _In_ ULONG TimeoutMs
    );

VOID
WskConnectExCancel(
    _Inout_ PVOID ConnectCompletion
    );

NTSTATUS
WskAcceptSync(
    PWSK_SOCKET ListenSock,
    int SockType,
    ULONG TimeoutMs,
    _Out_ PWSK_SOCKET* AcceptSock
    );

NTSTATUS
WskAcceptAsync(
    PWSK_SOCKET ListenSock,
    int SockType,
    _Out_ PVOID* AcceptCompletion
    );

NTSTATUS
WskAcceptAwait(
    PVOID AcceptCompletion,
    ULONG TimeoutMs,
    _Out_ PWSK_SOCKET* AcceptSock
    );

VOID
WskAcceptCancel(
    _Inout_ PVOID AcceptCompletion
    );

NTSTATUS
WskCloseSocketSync(
    PWSK_SOCKET Sock
    );

NTSTATUS
WskDisconnectSync(
    PWSK_SOCKET Sock,
    int SockType,
    char *Buf,
    ULONG BufLen,
    BOOLEAN BufIsNonPagedPool
    );

NTSTATUS
WskDisconnectAsync(
    PWSK_SOCKET Sock,
    int SockType,
    char *Buf,
    ULONG BufLen,
    BOOLEAN BufIsNonPagedPool,
    ULONG Flags,
    _Out_ PWSKDISCONNECT_COMPLETION* DisconnectCompletion
    );

NTSTATUS
WskDisconnectAwait(
    _Inout_ PWSKDISCONNECT_COMPLETION Completion,
    _In_ ULONG TimeoutMs
    );

NTSTATUS
WskAbortSync(
    PWSK_SOCKET Sock,
    int SockType
    );

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
    );

NTSTATUS
WskSendExSync(
    PWSK_SOCKET Sock,
    int SockType,
    char *Buf,
    ULONG BufLen,
    BOOLEAN BufIsNonPagedPool,
    char* Control,
    ULONG ControlLen,
    ULONG ExpectedBytesSent,
    ULONG Flags,
    _Out_opt_ ULONG *BytesSent
    );

NTSTATUS
WskSendAsync(
    PWSK_SOCKET Sock,
    int SockType,
    char *Buf,
    ULONG BufLen,
    BOOLEAN BufIsNonPagedPool,
    ULONG Flags,
    _Out_ PVOID* SendCompletion
    );

NTSTATUS
WskSendAwait(
    _Inout_ PVOID SendCompletion,
    _In_ ULONG TimeoutMs,
    _In_ ULONG ExpectedBytesSent,
    _Out_opt_ ULONG *BytesSent
    );

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
    );

NTSTATUS
WskReceiveExSync(
    PWSK_SOCKET Sock,
    int SockType,
    char *Buf,
    ULONG BufLen,
    BOOLEAN BufIsNonPagedPool,
    PCMSGHDR Control,
    PULONG ControlLen,
    ULONG ExpectedBytesReceived,
    ULONG TimeoutMs,
    _Out_opt_ ULONG *BytesReceived
    );

NTSTATUS
WskReceiveAsync(
    PWSK_SOCKET Sock,
    int SockType,
    char *Buf,
    ULONG BufLen,
    BOOLEAN BufIsNonPagedPool,
    _Out_ PVOID* ReceiveCompletion
    );

NTSTATUS
WskReceiveAwait(
    _Inout_ PVOID ReceiveCompletion,
    _In_ ULONG TimeoutMs,
    _In_ ULONG ExpectedBytesReceived,
    _Out_opt_ ULONG *BytesReceived
    );

NTSTATUS
WskSendToSync(
    PWSK_SOCKET Sock,
    char *Buf,
    ULONG BufLen,
    BOOLEAN BufIsNonPagedPool,
    ULONG ExpectedBytesSent,
    _In_ PSOCKADDR RemoteAddr,
    ULONG ControlLen,
    _In_opt_ PCMSGHDR Control,
    _Out_opt_ ULONG *BytesSent
    );

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
    );

NTSTATUS
WskSendToAwait(
    _Inout_ PVOID SendCompletion,
    _In_ ULONG TimeoutMs,
    _In_ ULONG ExpectedBytesSent,
    _Out_opt_ ULONG *BytesSent
    );

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
    );

NTSTATUS
WskGetLocalAddrSync(
    PWSK_SOCKET Sock,
    int SockType,
    PSOCKADDR Addr
    );

NTSTATUS
WskGetRemoteAddrSync(
    PWSK_SOCKET Sock,
    int SockType,
    PSOCKADDR Addr
    );

NTSTATUS
WskEnableCallbacks(
    PWSK_SOCKET Sock,
    int SockType,
    ULONG EventMask
    );

EXTERN_C_END
