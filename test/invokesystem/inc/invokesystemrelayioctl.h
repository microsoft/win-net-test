//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

#define ISR_DEVICE_NAME L"\\Device\\invokesystemrelaydrv"

#define ISR_OPEN_PACKET_NAME "IsrOpenPacket0"

//
// Type of object to create or open.
//
typedef enum _ISR_FILE_TYPE {
    ISR_FILE_TYPE_CLIENT,
    ISR_FILE_TYPE_SERVICE,
} ISR_FILE_TYPE;

//
// Open packet, the common header for NtCreateFile extended attributes.
//
typedef struct _ISR_OPEN_PACKET {
    ISR_FILE_TYPE ObjectType;
} ISR_OPEN_PACKET;

typedef struct _ISR_OPEN_CLIENT {
    UINT32 Reserved;
} ISR_OPEN_CLIENT;

typedef struct _ISR_OPEN_SERVICE {
    UINT32 Reserved;
} ISR_OPEN_SERVICE;

//
// IOCTL interface.
//

typedef struct _ISR_GET_OUTPUT {
    UINT64 Id;
    CHAR Command[256];
} ISR_GET_OUTPUT;

typedef struct _ISR_POST_INPUT {
    UINT64 Id;
    INT Result;
} ISR_POST_INPUT;

//
// Issued by kernel mode clients to enqueue an invoke system request.
// The request is processed asynchronously and the result is returned upon completion.
// Asynchronous.
//
// Input: char *Command
// Output: int Result
//
// TODO: add timeout so that these requests cannot pend forever?
//
#define ISR_IOCTL_INVOKE_SYSTEM_SUBMIT \
    CTL_CODE(FILE_DEVICE_NETWORK, 1, METHOD_BUFFERED, FILE_WRITE_ACCESS)

//
// Issued by invokesystemrelay user mode service to fetch an invoke system request.
// This will pend indefinitely until a request is ready to be processed.
// Asynchronous.
//
// Input: None
// Output: ISR_GET_OUTPUT
//
#define ISR_IOCTL_INVOKE_SYSTEM_GET \
    CTL_CODE(FILE_DEVICE_NETWORK, 2, METHOD_BUFFERED, FILE_WRITE_ACCESS)

//
// Issued by invokesystemrelay user mode service to return the result of an invoke system request.
// Synchronous.
//
// Input: ISR_POST_INPUT
// Output: None
//
#define ISR_IOCTL_INVOKE_SYSTEM_POST \
    CTL_CODE(FILE_DEVICE_NETWORK, 3, METHOD_BUFFERED, FILE_WRITE_ACCESS)

//
// Helpers for opening handles.
//

#define ISR_OPEN_EA_LENGTH \
    (sizeof(FILE_FULL_EA_INFORMATION) + \
        sizeof(ISR_OPEN_PACKET_NAME) + \
        sizeof(ISR_OPEN_PACKET))

inline
VOID *
IsrInitializeEa(
    _In_ ISR_FILE_TYPE FileType,
    _Out_ VOID *EaBuffer,
    _In_ UINT32 EaLength
    )
{
    FILE_FULL_EA_INFORMATION *EaHeader = (FILE_FULL_EA_INFORMATION *)EaBuffer;
    ISR_OPEN_PACKET *OpenPacket;

    if (EaLength < ISR_OPEN_EA_LENGTH) {
        __fastfail(FAST_FAIL_INVALID_ARG);
    }

    RtlZeroMemory(EaHeader, sizeof(*EaHeader));
    EaHeader->EaNameLength = sizeof(ISR_OPEN_PACKET_NAME) - 1;
    RtlCopyMemory(EaHeader->EaName, ISR_OPEN_PACKET_NAME, sizeof(ISR_OPEN_PACKET_NAME));
    EaHeader->EaValueLength = (USHORT)(EaLength - sizeof(*EaHeader) - sizeof(ISR_OPEN_PACKET_NAME));

    OpenPacket = (ISR_OPEN_PACKET *)(EaHeader->EaName + sizeof(ISR_OPEN_PACKET_NAME));
    OpenPacket->ObjectType = FileType;

    return OpenPacket + 1;
}

EXTERN_C_END
