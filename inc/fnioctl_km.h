//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

// #include <windows.h>
// #include <winioctl.h>
// #include <winternl.h>
// #include <ifdef.h>

EXTERN_C_START

typedef VOID* FNIOCTL_HANDLE;

//
// This file implements common file handle and IOCTL helpers.
//

//
// This struct is defined in public kernel headers, but not user mode headers.
//
// typedef struct _FILE_FULL_EA_INFORMATION {
//     ULONG NextEntryOffset;
//     UCHAR Flags;
//     UCHAR EaNameLength;
//     USHORT EaValueLength;
//     CHAR EaName[1];
// } FILE_FULL_EA_INFORMATION;

NTSTATUS
FnIoctlOpen(
    _In_ WCHAR *DeviceName,
    _In_ UINT32 Disposition,
    _In_opt_ VOID *EaBuffer,
    _In_ UINT32 EaLength,
    _Out_ FNIOCTL_HANDLE *Handle
    )
{
    UNREFERENCED_PARAMETER(DeviceName);
    UNREFERENCED_PARAMETER(Disposition);
    UNREFERENCED_PARAMETER(EaBuffer);
    UNREFERENCED_PARAMETER(EaLength);
    UNREFERENCED_PARAMETER(Handle);
    *Handle = NULL;
    return STATUS_NOT_IMPLEMENTED;
}

VOID
FnIoctlClose(
    _In_ FNIOCTL_HANDLE Handle
    )
{
    UNREFERENCED_PARAMETER(Handle);
}

NTSTATUS
FnIoctl(
    _In_ FNIOCTL_HANDLE Handle,
    _In_ UINT32 Operation,
    _In_opt_ VOID *InBuffer,
    _In_ UINT32 InBufferSize,
    _Out_opt_ VOID *OutBuffer,
    _In_ UINT32 OutputBufferSize,
    _Out_opt_ UINT32 *BytesReturned,
    _In_opt_ VOID *Overlapped
    )
{
    UNREFERENCED_PARAMETER(Handle);
    UNREFERENCED_PARAMETER(Operation);
    UNREFERENCED_PARAMETER(InBuffer);
    UNREFERENCED_PARAMETER(InBufferSize);
    UNREFERENCED_PARAMETER(OutBuffer);
    UNREFERENCED_PARAMETER(OutputBufferSize);
    UNREFERENCED_PARAMETER(BytesReturned);
    UNREFERENCED_PARAMETER(Overlapped);
    return STATUS_NOT_IMPLEMENTED;
}

EXTERN_C_END
