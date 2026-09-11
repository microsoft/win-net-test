//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include <fniotypes.h>
#if defined(_KERNEL_MODE)
#include <fnioctl_km.h>
#include <fnmpapistatus_km.h>
#else
#include <fnioctl_um.h>
#include <fnmpapistatus_um.h>
#endif
#include <fnbusioctl.h>

EXTERN_C_START

#define FNBUSAPI inline

DECLARE_HANDLE(FNBUS_HANDLE);

FNBUSAPI
FNMPAPI_STATUS
FnBusOpen(
    _Out_ FNBUS_HANDLE *Handle
    )
{
    return
        FnIoctlOpen(
            FNBUS_DEVICE_NAME, FILE_OPEN, NULL, 0, (FNIOCTL_HANDLE *)Handle);
}

FNBUSAPI
VOID
FnBusClose(
    _In_ FNBUS_HANDLE Handle
    )
{
    FnIoctlClose((FNIOCTL_HANDLE)Handle);
}

//
// Enumerates a new child. The adapter appears asynchronously as the function
// driver loads, so callers must poll for it.
//
FNBUSAPI
FNMPAPI_STATUS
FnBusCreateNic(
    _In_ FNBUS_HANDLE Handle,
    _In_ UINT32 SerialNumber
    )
{
    FNBUS_CREATE_NIC CreateNic;

    CreateNic.SerialNumber = SerialNumber;

    return
        FnIoctl(
            (FNIOCTL_HANDLE)Handle, FNBUS_IOCTL_CREATE_NIC, &CreateNic,
            sizeof(CreateNic), NULL, 0, NULL, NULL);
}

FNBUSAPI
FNMPAPI_STATUS
FnBusDeleteNic(
    _In_ FNBUS_HANDLE Handle,
    _In_ UINT32 SerialNumber
    )
{
    FNBUS_DELETE_NIC DeleteNic;

    DeleteNic.SerialNumber = SerialNumber;

    return
        FnIoctl(
            (FNIOCTL_HANDLE)Handle, FNBUS_IOCTL_DELETE_NIC, &DeleteNic,
            sizeof(DeleteNic), NULL, 0, NULL, NULL);
}

//
// Drives a child to the requested device power state. With Wait set the call
// does not return until the power request has completed, which is what a test
// wants unless it is deliberately exercising a transition that blocks.
//
FNBUSAPI
FNMPAPI_STATUS
FnBusSetNicPower(
    _In_ FNBUS_HANDLE Handle,
    _In_ UINT32 SerialNumber,
    _In_ FNBUS_DEVICE_POWER_STATE DevicePowerState,
    _In_ BOOLEAN Wait
    )
{
    FNBUS_SET_NIC_POWER SetNicPower;

    SetNicPower.SerialNumber = SerialNumber;
    SetNicPower.DevicePowerState = DevicePowerState;
    SetNicPower.Wait = Wait;

    return
        FnIoctl(
            (FNIOCTL_HANDLE)Handle, FNBUS_IOCTL_SET_NIC_POWER, &SetNicPower,
            sizeof(SetNicPower), NULL, 0, NULL, NULL);
}

EXTERN_C_END
