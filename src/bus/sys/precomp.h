//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include <ntddk.h>
#include <ntintsafe.h>
#include <ntstrsafe.h>
#include <wdf.h>

#include <fnbusioctl.h>

#include "trace.h"

//
// Per-child identification description. WDF copies this into its child list, so
// it must be blittable.
//
typedef struct _FNBUS_PDO_IDENTIFICATION {
    WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER Header;
    UINT32 SerialNumber;
} FNBUS_PDO_IDENTIFICATION;

typedef struct _FNBUS_PDO_CONTEXT {
    UINT32 SerialNumber;
} FNBUS_PDO_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(FNBUS_PDO_CONTEXT, FnBusGetPdoContext);

typedef struct _FNBUS_FDO_CONTEXT {
    WDFDEVICE Device;
} FNBUS_FDO_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(FNBUS_FDO_CONTEXT, FnBusGetFdoContext);

EVT_WDF_DRIVER_DEVICE_ADD FnBusEvtDeviceAdd;
EVT_WDF_CHILD_LIST_CREATE_DEVICE FnBusEvtChildListCreateDevice;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL FnBusEvtIoDeviceControl;

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
FnBusPdoCreate(
    _In_ WDFCHILDLIST ChildList,
    _In_ PWDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER IdentificationDescription,
    _In_ PWDFDEVICE_INIT ChildInit
    );

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
FnBusPdoSetPower(
    _In_ WDFDEVICE Fdo,
    _In_ UINT32 SerialNumber,
    _In_ DEVICE_POWER_STATE DevicePowerState,
    _In_ BOOLEAN Wait
    );
