//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

#define FNBUS_IOCTL_CURRENT_VERSION 1

#define FNBUS_DEVICE_NAME L"\\Device\\FNBUS"

//
// Hardware ID reported for each enumerated child. Matches the [Manufacturer]
// entry in fnmp.inf so FNMP loads as the function driver for bus children.
//
#define FNBUS_CHILD_HARDWARE_ID L"ms_fnmp"

//
// Enumerator name prefixed to child device IDs.
//
#define FNBUS_ENUMERATOR L"FNBUS"

//
// Creates a child PDO. The child is enumerated with power capabilities (unlike
// a software-enumerated device), so the function driver's stack can receive
// device power IRPs.
//
typedef struct _FNBUS_CREATE_NIC {
    //
    // Caller-assigned identifier, unique across live children. Also used as the
    // PDO's UINumber/Address and as the instance ID suffix.
    //
    UINT32 SerialNumber;
} FNBUS_CREATE_NIC;

typedef struct _FNBUS_DELETE_NIC {
    UINT32 SerialNumber;
} FNBUS_DELETE_NIC;

//
// Device power states usable with FNBUS_IOCTL_SET_NIC_POWER. These mirror
// DEVICE_POWER_STATE so user mode does not need wdm.h.
//
typedef enum _FNBUS_DEVICE_POWER_STATE {
    FnBusDevicePowerD0 = 1,
    FnBusDevicePowerD1 = 2,
    FnBusDevicePowerD2 = 3,
    FnBusDevicePowerD3 = 4,
} FNBUS_DEVICE_POWER_STATE;

//
// Requests a device power IRP on a child's stack. This is the deterministic
// trigger for D-state transitions: the request is issued through the power
// manager, so the target stack sees a genuine device power IRP.
//
typedef struct _FNBUS_SET_NIC_POWER {
    UINT32 SerialNumber;
    FNBUS_DEVICE_POWER_STATE DevicePowerState;
    //
    // When TRUE the IOCTL completes only once the power IRP has completed.
    // When FALSE it returns as soon as the IRP is requested, which is what a
    // test wants when the transition is expected to block.
    //
    BOOLEAN Wait;
} FNBUS_SET_NIC_POWER;

#define FNBUS_IOCTL_CREATE_NIC \
    CTL_CODE(FILE_DEVICE_BUS_EXTENDER, 0, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define FNBUS_IOCTL_DELETE_NIC \
    CTL_CODE(FILE_DEVICE_BUS_EXTENDER, 1, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define FNBUS_IOCTL_SET_NIC_POWER \
    CTL_CODE(FILE_DEVICE_BUS_EXTENDER, 2, METHOD_BUFFERED, FILE_WRITE_ACCESS)

EXTERN_C_END
