//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include <winsock2.h>
#include <windows.h>
#include <winternl.h>
#include <stdlib.h>

#include "cxplat.h"
#include "trace.h"

#include "cxplat.tmh"

//
// Sockets API.
//

PAGEDX
_IRQL_requires_max_(PASSIVE_LEVEL)
CXPLAT_STATUS
FnSockInitialize(
    VOID
    )
{
    INT WsaError;
    CXPLAT_STATUS Status;
    WSADATA WsaData;

    TraceInfo("FnSockInitialize");

    if ((WsaError = WSAStartup(MAKEWORD(2, 2), &WsaData)) != 0) {
        TraceError(
            "[ lib] ERROR, %u, %s.",
            WsaError,
            "WSAStartup");
        Status = HRESULT_FROM_WIN32(WsaError);
    } else {
        Status = CXPLAT_STATUS_SUCCESS;
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
CXPLAT_STATUS
FnSockCreate(
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
FnSockClose(
    _In_ CXPLAT_SOCKET Socket
    )
{
    closesocket((SOCKET)Socket);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
CXPLAT_STATUS
FnSockBind(
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
FnSockGetSockName(
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
FnSockSetSockOpt(
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
FnSockSendto(
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
FnSockRecv(
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
FnSockGetLastError(
    VOID
    )
{
    return WSAGetLastError();
}

//
// Time Measurement Interfaces
//

UINT64 CxPlatPerfFreq;

UINT64
CxPlatTimePlat(
    void
    )
{
    UINT64 Count;
    QueryPerformanceCounter((LARGE_INTEGER*)&Count);
    return Count;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
CxPlatSleep(
    _In_ UINT32 DurationMs
    )
{
    Sleep(DurationMs);
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
    UNREFERENCED_PARAMETER(Tag);
    return malloc(Size);
}

VOID
CxPlatFree(
    _In_ VOID* Mem,
    _In_ ULONG Tag
    )
{
    UNREFERENCED_PARAMETER(Tag);
    free(Mem);
}

VOID
CxPlatFreeNoTag(
    _In_opt_ VOID* Mem
    )
{
    if (Mem != NULL) {
        free(Mem);
    }
}

//
// CPU
//

typedef struct CXPLAT_PROCESSOR_INFO {
    UINT32 Index;  // Index in the current group
    UINT16 Group;  // The group number this processor is a part of
} CXPLAT_PROCESSOR_INFO;

typedef struct CXPLAT_PROCESSOR_GROUP_INFO {
    KAFFINITY Mask;  // Bit mask of active processors in the group
    UINT32 Count;  // Count of active processors in the group
    UINT32 Offset; // Base process index offset this group starts at
} CXPLAT_PROCESSOR_GROUP_INFO;

static CXPLAT_PROCESSOR_INFO* CxPlatProcessorInfo;
static CXPLAT_PROCESSOR_GROUP_INFO* CxPlatProcessorGroupInfo;
static UINT32 CxPlatProcessorCount;

_IRQL_requires_max_(PASSIVE_LEVEL)
_Must_inspect_result_
CXPLAT_STATUS
CxPlatGetProcessorGroupInfo(
    _In_ LOGICAL_PROCESSOR_RELATIONSHIP Relationship,
    _Outptr_ _At_(*Buffer, __drv_allocatesMem(Mem)) _Pre_defensive_
        PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* Buffer,
    _Out_ PDWORD BufferLength
    )
{
    *BufferLength = 0;
    GetLogicalProcessorInformationEx(Relationship, NULL, BufferLength);
    if (*BufferLength == 0) {
        TraceError(
            "[ lib] ERROR, %s.",
            "Failed to determine PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX size");
        return HRESULT_FROM_WIN32(GetLastError());
    }

    *Buffer = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)malloc(*BufferLength);
    if (*Buffer == NULL) {
        TraceError(
            "Allocation of '%s' failed. (%llu bytes)",
            "PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX",
            *BufferLength);
        return CXPLAT_STATUS_FAIL;
    }

    if (!GetLogicalProcessorInformationEx(
            Relationship,
            *Buffer,
            BufferLength)) {
        TraceError(
            "[ lib] ERROR, %u, %s.",
            GetLastError(),
            "GetLogicalProcessorInformationEx failed");
        free(*Buffer);
        return HRESULT_FROM_WIN32(GetLastError());
    }

    return CXPLAT_STATUS_SUCCESS;
}

CXPLAT_STATUS
CxPlatProcessorInfoInit(
    void
    )
{
    CXPLAT_STATUS Status = CXPLAT_STATUS_SUCCESS;
    DWORD InfoLength = 0;
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* Info = NULL;
    UINT32 ActiveProcessorCount = 0, MaxProcessorCount = 0;
    Status =
        CxPlatGetProcessorGroupInfo(
            RelationGroup,
            &Info,
            &InfoLength);
    if (CXPLAT_FAILED(Status)) {
        goto Error;
    }

    if (Info->Group.ActiveGroupCount == 0) {
        TraceError(
            "[ lib] ERROR, %s.",
            "Failed to determine processor group count");
        Status = CXPLAT_STATUS_FAIL;
        goto Error;
    }

    for (WORD i = 0; i < Info->Group.ActiveGroupCount; ++i) {
        ActiveProcessorCount += Info->Group.GroupInfo[i].ActiveProcessorCount;
        MaxProcessorCount += Info->Group.GroupInfo[i].MaximumProcessorCount;
    }

    if (ActiveProcessorCount == 0 || ActiveProcessorCount > MAXUINT16) {
        TraceError(
            "[ lib] ERROR, %u, %s.",
            ActiveProcessorCount,
            "Invalid active processor count");
        Status = CXPLAT_STATUS_FAIL;
        goto Error;
    }

    TraceInfo(
        "[ dll] Processors: (%u active, %u max), Groups: (%hu active, %hu max)",
        ActiveProcessorCount,
        MaxProcessorCount,
        Info->Group.ActiveGroupCount,
        Info->Group.MaximumGroupCount);

    CxPlatProcessorInfo =
        (CXPLAT_PROCESSOR_INFO*)malloc(
            ActiveProcessorCount * sizeof(CXPLAT_PROCESSOR_INFO));
    if (CxPlatProcessorInfo == NULL) {
        TraceError(
            "Allocation of '%s' failed. (%llu bytes)",
            "CxPlatProcessorInfo",
            ActiveProcessorCount * sizeof(CXPLAT_PROCESSOR_INFO));
        Status = CXPLAT_STATUS_FAIL;
        goto Error;
    }

    CxPlatProcessorGroupInfo =
        (CXPLAT_PROCESSOR_GROUP_INFO*)malloc(
            Info->Group.ActiveGroupCount * sizeof(CXPLAT_PROCESSOR_GROUP_INFO));
    if (CxPlatProcessorGroupInfo == NULL) {
        TraceError(
            "Allocation of '%s' failed. (%llu bytes)",
            "CxPlatProcessorGroupInfo",
            Info->Group.ActiveGroupCount * sizeof(CXPLAT_PROCESSOR_GROUP_INFO));
        Status = CXPLAT_STATUS_FAIL;
        goto Error;
    }

    CxPlatProcessorCount = 0;
    for (WORD i = 0; i < Info->Group.ActiveGroupCount; ++i) {
        CxPlatProcessorGroupInfo[i].Mask = Info->Group.GroupInfo[i].ActiveProcessorMask;
        CxPlatProcessorGroupInfo[i].Count = Info->Group.GroupInfo[i].ActiveProcessorCount;
        CxPlatProcessorGroupInfo[i].Offset = CxPlatProcessorCount;
        CxPlatProcessorCount += Info->Group.GroupInfo[i].ActiveProcessorCount;
    }

    for (UINT32 Proc = 0; Proc < ActiveProcessorCount; ++Proc) {
        for (WORD Group = 0; Group < Info->Group.ActiveGroupCount; ++Group) {
            if (Proc >= CxPlatProcessorGroupInfo[Group].Offset &&
                Proc < CxPlatProcessorGroupInfo[Group].Offset + Info->Group.GroupInfo[Group].ActiveProcessorCount) {
                CxPlatProcessorInfo[Proc].Group = Group;
                CxPlatProcessorInfo[Proc].Index = (Proc - CxPlatProcessorGroupInfo[Group].Offset);
                TraceInfo(
                    "[ dll] Proc[%u] Group[%hu] Index[%u] Active=%hu",
                    Proc,
                    (UINT16)Group,
                    CxPlatProcessorInfo[Proc].Index,
                    (UINT8)!!(CxPlatProcessorGroupInfo[Group].Mask & (1ULL << CxPlatProcessorInfo[Proc].Index)));
                break;
            }
        }
    }

    Status = CXPLAT_STATUS_SUCCESS;

Error:

    if (Info) {
        free(Info);
    }

    if (CXPLAT_FAILED(Status)) {
        if (CxPlatProcessorGroupInfo) {
            free(CxPlatProcessorGroupInfo);
            CxPlatProcessorGroupInfo = NULL;
        }
        if (CxPlatProcessorInfo) {
            free(CxPlatProcessorInfo);
            CxPlatProcessorInfo = NULL;
        }
    }

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
void
CxPlatProcessorInfoUnInit(
    void
    )
{
    free(CxPlatProcessorGroupInfo);
    CxPlatProcessorGroupInfo = NULL;
    free(CxPlatProcessorInfo);
    CxPlatProcessorInfo = NULL;
}

//
// Create Thread Interfaces
//

#define ThreadNameInformationPrivate ((THREADINFOCLASS)38)

typedef struct _THREAD_NAME_INFORMATION_PRIVATE {
    UNICODE_STRING ThreadName;
} THREAD_NAME_INFORMATION_PRIVATE, *PTHREAD_NAME_INFORMATION_PRIVATE;

__kernel_entry
NTSTATUS
NTAPI
NtSetInformationThread(
    _In_ HANDLE ThreadHandle,
    _In_ THREADINFOCLASS ThreadInformationClass,
    _In_reads_bytes_(ThreadInformationLength) PVOID ThreadInformation,
    _In_ ULONG ThreadInformationLength
    );

CXPLAT_STATUS
CxPlatThreadCreate(
    _In_ CXPLAT_THREAD_CONFIG* Config,
    _Out_ CXPLAT_THREAD* Thread
    )
{
    HANDLE Handle;
    Handle =
        CreateThread(
            NULL,
            0,
            Config->Callback,
            Config->Context,
            0,
            NULL);
    if (Handle == NULL) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    const CXPLAT_PROCESSOR_INFO* ProcInfo = &CxPlatProcessorInfo[Config->IdealProcessor];
    GROUP_AFFINITY Group = {0};
    if (Config->Flags & CXPLAT_THREAD_FLAG_SET_AFFINITIZE) {
        Group.Mask = (KAFFINITY)(1ull << ProcInfo->Index);          // Fixed processor
    } else {
        Group.Mask = CxPlatProcessorGroupInfo[ProcInfo->Group].Mask;
    }
    Group.Group = ProcInfo->Group;
    SetThreadGroupAffinity(Handle, &Group, NULL);
    if (Config->Flags & CXPLAT_THREAD_FLAG_SET_IDEAL_PROC) {
        SetThreadIdealProcessor(Handle, ProcInfo->Index);
    }
    if (Config->Flags & CXPLAT_THREAD_FLAG_HIGH_PRIORITY) {
        SetThreadPriority(Handle, THREAD_PRIORITY_HIGHEST);
    }
    if (Config->Name) {
        WCHAR WideName[64] = L"";
        size_t WideNameLength;
        mbstowcs_s(
            &WideNameLength,
            WideName,
            ARRAYSIZE(WideName) - 1,
            Config->Name,
            _TRUNCATE);
        THREAD_NAME_INFORMATION_PRIVATE ThreadNameInfo;
        RtlInitUnicodeString(&ThreadNameInfo.ThreadName, WideName);
        NtSetInformationThread(
            Handle,
            ThreadNameInformationPrivate,
            &ThreadNameInfo,
            sizeof(ThreadNameInfo));
    }
    *Thread = (CXPLAT_THREAD)Handle;
    return CXPLAT_STATUS_SUCCESS;
}

VOID
CxPlatThreadDelete(
    _In_ CXPLAT_THREAD Thread
    )
{
    HANDLE Handle = (HANDLE)Thread;

    CloseHandle(Handle);
}

BOOLEAN
CxPlatThreadWait(
    _In_ CXPLAT_THREAD Thread,
    _In_ UINT32 TimeoutMs
    )
{
    HANDLE Handle = (HANDLE)Thread;

    return WAIT_OBJECT_0 == WaitForSingleObject(Handle, TimeoutMs);
}

//
// Module initialize and cleanup
//

PAGEDX
_IRQL_requires_max_(PASSIVE_LEVEL)
CXPLAT_STATUS
CxPlatInitialize(
    VOID
    )
{
    CXPLAT_STATUS Status;

    TraceInfo("CxPlatInitialize");

    (VOID)QueryPerformanceFrequency((LARGE_INTEGER*)&CxPlatPerfFreq);

    Status = CxPlatProcessorInfoInit();
    if (CXPLAT_FAILED(Status)) {
        goto Exit;
    }

    Status = CXPLAT_STATUS_SUCCESS;

Exit:

    return Status;
}

PAGEDX
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
CxPlatUninitialize(
    VOID
    )
{
    CxPlatProcessorInfoUnInit();
}
