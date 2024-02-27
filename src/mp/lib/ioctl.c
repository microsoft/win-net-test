//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

//
// This file implements common file handle and IOCTL helpers.
//

VOID *
FnMpInitializeEa(
    _In_ FNMP_FILE_TYPE FileType,
    _Out_ VOID *EaBuffer,
    _In_ UINT32 EaLength
    )
{
    FILE_FULL_EA_INFORMATION *EaHeader = EaBuffer;
    FNMP_OPEN_PACKET *OpenPacket;

    if (EaLength < FNMP_OPEN_EA_LENGTH) {
        __fastfail(FAST_FAIL_INVALID_ARG);
    }

    RtlZeroMemory(EaHeader, sizeof(*EaHeader));
    EaHeader->EaNameLength = sizeof(FNMP_OPEN_PACKET_NAME) - 1;
    RtlCopyMemory(EaHeader->EaName, FNMP_OPEN_PACKET_NAME, sizeof(FNMP_OPEN_PACKET_NAME));
    EaHeader->EaValueLength = (USHORT)(EaLength - sizeof(*EaHeader) - sizeof(FNMP_OPEN_PACKET_NAME));

    OpenPacket = (FNMP_OPEN_PACKET *)(EaHeader->EaName + sizeof(FNMP_OPEN_PACKET_NAME));
    OpenPacket->ObjectType = FileType;

    return OpenPacket + 1;
}

HRESULT
FnMpOpen(
    _In_ UINT32 Disposition,
    _In_opt_ VOID *EaBuffer,
    _In_ UINT32 EaLength,
    _Out_ HANDLE *Handle
    )
{
    UNICODE_STRING DeviceName;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS Status;

    //
    // Open a handle to the FNMP device.
    //
    RtlInitUnicodeString(&DeviceName, FNMP_DEVICE_NAME);
    InitializeObjectAttributes(
        &ObjectAttributes, &DeviceName, OBJ_CASE_INSENSITIVE, NULL, NULL);

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

HRESULT
FnMpIoctl(
    _In_ HANDLE Handle,
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
