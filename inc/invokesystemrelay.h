//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include <fnioctl_km.h>
#include <invokesystemrelayioctl.h>

EXTERN_C_START

inline
INT
InvokeSystemRelay(
    _In_z_ const CHAR *Command
    )
{
    INT Result = -1;
    NTSTATUS Status;
    FNIOCTL_HANDLE Handle = NULL;
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
            Handle, ISR_IOCTL_INVOKE_SYSTEM_SUBMIT, (VOID *)Command,
            (UINT32)strlen(Command) + 1, &Result, sizeof(Result), NULL, NULL);

Exit:

    if (Handle != NULL) {
        FnIoctlClose(Handle);
    }

    return Result;
}

EXTERN_C_END
