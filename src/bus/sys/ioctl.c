//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "ioctl.tmh"

static
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
FnBusIoctlCreateNic(
    _In_ WDFDEVICE Device,
    _In_ FNBUS_CREATE_NIC *CreateNic
    )
{
    FNBUS_PDO_IDENTIFICATION Identification;

    WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER_INIT(
        &Identification.Header, sizeof(Identification));
    Identification.SerialNumber = CreateNic->SerialNumber;

    return
        WdfChildListAddOrUpdateChildDescriptionAsPresent(
            WdfFdoGetDefaultChildList(Device), &Identification.Header, NULL);
}

static
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
FnBusIoctlDeleteNic(
    _In_ WDFDEVICE Device,
    _In_ FNBUS_DELETE_NIC *DeleteNic
    )
{
    FNBUS_PDO_IDENTIFICATION Identification;

    WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER_INIT(
        &Identification.Header, sizeof(Identification));
    Identification.SerialNumber = DeleteNic->SerialNumber;

    return
        WdfChildListUpdateChildDescriptionAsMissing(
            WdfFdoGetDefaultChildList(Device), &Identification.Header);
}

static
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
FnBusIoctlSetNicPower(
    _In_ WDFDEVICE Device,
    _In_ FNBUS_SET_NIC_POWER *SetNicPower
    )
{
    if (SetNicPower->DevicePowerState < FnBusDevicePowerD0 ||
        SetNicPower->DevicePowerState > FnBusDevicePowerD3) {
        return STATUS_INVALID_PARAMETER;
    }

    return
        FnBusPdoSetPower(
            Device,
            SetNicPower->SerialNumber,
            (DEVICE_POWER_STATE)SetNicPower->DevicePowerState,
            SetNicPower->Wait);
}

_Use_decl_annotations_
VOID
FnBusEvtIoDeviceControl(
    WDFQUEUE Queue,
    WDFREQUEST Request,
    size_t OutputBufferLength,
    size_t InputBufferLength,
    ULONG IoControlCode
    )
{
    NTSTATUS Status;
    WDFDEVICE Device = WdfIoQueueGetDevice(Queue);
    VOID *InputBuffer = NULL;
    size_t RequiredLength;

    UNREFERENCED_PARAMETER(OutputBufferLength);

    switch (IoControlCode) {
    case FNBUS_IOCTL_CREATE_NIC:
        RequiredLength = sizeof(FNBUS_CREATE_NIC);
        break;
    case FNBUS_IOCTL_DELETE_NIC:
        RequiredLength = sizeof(FNBUS_DELETE_NIC);
        break;
    case FNBUS_IOCTL_SET_NIC_POWER:
        RequiredLength = sizeof(FNBUS_SET_NIC_POWER);
        break;
    default:
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    if (InputBufferLength < RequiredLength) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    Status = WdfRequestRetrieveInputBuffer(Request, RequiredLength, &InputBuffer, NULL);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    switch (IoControlCode) {
    case FNBUS_IOCTL_CREATE_NIC:
        Status = FnBusIoctlCreateNic(Device, (FNBUS_CREATE_NIC *)InputBuffer);
        break;
    case FNBUS_IOCTL_DELETE_NIC:
        Status = FnBusIoctlDeleteNic(Device, (FNBUS_DELETE_NIC *)InputBuffer);
        break;
    case FNBUS_IOCTL_SET_NIC_POWER:
        Status = FnBusIoctlSetNicPower(Device, (FNBUS_SET_NIC_POWER *)InputBuffer);
        break;
    default:
        Status = STATUS_NOT_SUPPORTED;
        break;
    }

Exit:

    TraceInfo(
        TRACE_CONTROL, "Ioctl=0x%x Status=%!STATUS!", IoControlCode, Status);

    WdfRequestComplete(Request, Status);
}
