//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include <windows.h>
#include <winioctl.h>
#include <winternl.h>
#include <ifdef.h>

EXTERN_C_START

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif

#ifndef STATUS_UNSUCCESSFUL
#define STATUS_UNSUCCESSFUL ((NTSTATUS)0xC0000001L)
#endif

typedef HANDLE FNIOCTL_HANDLE;

//
// This file implements common file handle and IOCTL helpers.
//

//
// This struct is defined in public kernel headers, but not user mode headers.
//
typedef struct _FILE_FULL_EA_INFORMATION {
    ULONG NextEntryOffset;
    UCHAR Flags;
    UCHAR EaNameLength;
    USHORT EaValueLength;
    CHAR EaName[1];
} FILE_FULL_EA_INFORMATION;

inline
HRESULT
FnIoctlOpen(
    _In_ const WCHAR *DeviceName,
    _In_ UINT32 Disposition,
    _In_opt_ VOID *EaBuffer,
    _In_ UINT32 EaLength,
    _Out_ FNIOCTL_HANDLE *Handle
    )
{
    UNICODE_STRING DeviceNameUnicode;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS Status;

    RtlInitUnicodeString(&DeviceNameUnicode, DeviceName);
    InitializeObjectAttributes(
        &ObjectAttributes, &DeviceNameUnicode, OBJ_CASE_INSENSITIVE, NULL, NULL);

    Status =
        NtCreateFile(
            Handle,
            GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE,
            &ObjectAttributes,
            &IoStatusBlock,
            NULL,
            0L,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            Disposition,
            0,
            EaBuffer,
            EaLength);

    return HRESULT_FROM_WIN32(RtlNtStatusToDosError(Status));
}

inline
VOID
FnIoctlClose(
    _In_ FNIOCTL_HANDLE Handle
    )
{
    CloseHandle(Handle);
}

inline
HRESULT
FnIoctl(
    _In_ FNIOCTL_HANDLE Handle,
    _In_ UINT32 Operation,
    _In_opt_ VOID *InBuffer,
    _In_ UINT32 InBufferSize,
    _Out_opt_ VOID *OutBuffer,
    _In_ UINT32 OutputBufferSize,
    _Out_opt_ UINT32 *BytesReturned,
    _In_opt_ OVERLAPPED *Overlapped
    )
{
    NTSTATUS Status;
    IO_STATUS_BLOCK LocalIoStatusBlock = {0};
    IO_STATUS_BLOCK *IoStatusBlock;
    HANDLE LocalEvent = NULL;
    HANDLE *Event;

    if (BytesReturned != NULL) {
        *BytesReturned = 0;
    }

    if (Overlapped == NULL) {
        IoStatusBlock = &LocalIoStatusBlock;
        Event = &LocalEvent;
        LocalEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
        if (LocalEvent == NULL) {
            Status = STATUS_NO_MEMORY;
            goto Exit;
        }
    } else {
        IoStatusBlock = (IO_STATUS_BLOCK *)&Overlapped->Internal;
        Event = &Overlapped->hEvent;
    }

    IoStatusBlock->Status = STATUS_PENDING;

    Status =
        NtDeviceIoControlFile(
            Handle, *Event, NULL, NULL, IoStatusBlock, Operation, InBuffer,
            InBufferSize, OutBuffer, OutputBufferSize);

    if (Event == &LocalEvent && Status == STATUS_PENDING) {
        DWORD WaitResult = WaitForSingleObject(*Event, INFINITE);
        if (WaitResult != WAIT_OBJECT_0) {
            if (WaitResult != WAIT_FAILED) {
                Status = STATUS_UNSUCCESSFUL;
            }
            goto Exit;
        }

        Status = IoStatusBlock->Status;
    }

    if (BytesReturned != NULL) {
        *BytesReturned = (UINT32)IoStatusBlock->Information;
    }

Exit:

    if (LocalEvent != NULL) {
        CloseHandle(LocalEvent);
    }

    return HRESULT_FROM_WIN32(RtlNtStatusToDosError(Status));
}

EXTERN_C_END
