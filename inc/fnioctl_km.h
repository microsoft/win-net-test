//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSYSAPI
NTSTATUS
NTAPI
ZwCreateEvent (
    _Out_ PHANDLE EventHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ EVENT_TYPE EventType,
    _In_ BOOLEAN InitialState
    );

_When_(Timeout == NULL, _IRQL_requires_max_(APC_LEVEL))
_When_(Timeout->QuadPart != 0, _IRQL_requires_max_(APC_LEVEL))
_When_(Timeout->QuadPart == 0, _IRQL_requires_max_(DISPATCH_LEVEL))
NTSYSAPI
NTSTATUS
NTAPI
ZwWaitForSingleObject(
    _In_ HANDLE Handle,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER Timeout
    );

typedef VOID* FNIOCTL_HANDLE;

inline
NTSTATUS
FnIoctlOpen(
    _In_ const WCHAR *DeviceName,
    _In_ UINT32 Disposition,
    _In_opt_ VOID *EaBuffer,
    _In_ UINT32 EaLength,
    _Out_ FNIOCTL_HANDLE *Handle
    )
{
    NTSTATUS Status;
    IO_STATUS_BLOCK IoStatusBlock;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING UnicodeDeviceName;

    RtlInitUnicodeString(&UnicodeDeviceName, DeviceName);
    InitializeObjectAttributes(
        &ObjectAttributes, &UnicodeDeviceName,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    Status =
        ZwCreateFile(
            Handle,
            GENERIC_READ | GENERIC_WRITE,
            &ObjectAttributes,
            &IoStatusBlock,
            NULL,
            FILE_ATTRIBUTE_NORMAL,
            FILE_SHARE_WRITE,
            Disposition,
            0,
            EaBuffer,
            EaLength);


    return Status;
}

inline
VOID
FnIoctlClose(
    _In_ FNIOCTL_HANDLE Handle
    )
{
    ZwClose(Handle);
}

inline
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
    NTSTATUS Status;
    IO_STATUS_BLOCK IoStatusBlock;
    OBJECT_ATTRIBUTES EventAttributes;
    HANDLE Event = NULL;

    UNREFERENCED_PARAMETER(Overlapped);

    if (BytesReturned != NULL) {
        *BytesReturned = 0;
    }

    InitializeObjectAttributes(
        &EventAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);

    Status =
        ZwCreateEvent(
            &Event,
            EVENT_ALL_ACCESS,
            &EventAttributes,
            SynchronizationEvent,
            FALSE);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status =
        ZwDeviceIoControlFile(
            Handle,
            Event,
            NULL,
            NULL,
            &IoStatusBlock,
            Operation,
            InBuffer,
            InBufferSize,
            OutBuffer,
            OutputBufferSize);
    if (Status == STATUS_PENDING) {
        Status = ZwWaitForSingleObject(Event, FALSE, NULL);
        ASSERT(Status == STATUS_SUCCESS);
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }

        Status = IoStatusBlock.Status;
    }

    if (BytesReturned != NULL) {
        *BytesReturned = (UINT32)IoStatusBlock.Information;
    }

Exit:

    if (Event != NULL) {
        ZwClose(Event);
    }

    return Status;
}

EXTERN_C_END
