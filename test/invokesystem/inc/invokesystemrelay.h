//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include <fnioctl_km.h>
#include <invokesystemrelayioctl.h>

EXTERN_C_START

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

inline
INT
InvokeSystemRelay(
    _In_z_ const CHAR *Command
    )
{
    INT Result = -1;
    NTSTATUS Status;
    FNIOCTL_HANDLE Handle;
    ISR_OPEN_CLIENT *OpenClient;
    CHAR EaBuffer[ISR_OPEN_EA_LENGTH + sizeof(*OpenClient)];

    OpenClient =
        (ISR_OPEN_CLIENT *)
            IsrInitializeEa(ISR_FILE_TYPE_CLIENT, EaBuffer, sizeof(EaBuffer));

    Status =
        FnIoctlOpen(
            ISR_DEVICE_NAME, FILE_CREATE, EaBuffer, sizeof(EaBuffer), &Handle);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status =
        FnIoctl(
            Handle, ISR_IOCTL_INVOKE_SYSTEM_SUBMIT, (VOID *)Command, (UINT32)strlen(Command),
            &Result, sizeof(Result), NULL, NULL);

Exit:

    if (Handle != NULL) {
        FnIoctlClose(Handle);
    }

    return Result;
}

EXTERN_C_END
