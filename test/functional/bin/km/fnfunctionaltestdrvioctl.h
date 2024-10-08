//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

//
// Kernel Mode Driver Interface
//

#define FUNCTIONAL_TEST_DRIVER_NAME "fnfunctionaltestdrv"

#define IoGetFunctionCodeFromCtlCode( ControlCode ) (\
    ( ControlCode >> 2) & 0x00000FFF )

//
// IOCTL Interface
//

#define IOCTL_MP_BASIC_RX \
    CTL_CODE(FILE_DEVICE_NETWORK, 1, METHOD_BUFFERED, FILE_WRITE_DATA)

#define IOCTL_MP_BASIC_TX \
    CTL_CODE(FILE_DEVICE_NETWORK, 2, METHOD_BUFFERED, FILE_WRITE_DATA)

#define IOCTL_MP_BASIC_RX_OFFLOAD \
    CTL_CODE(FILE_DEVICE_NETWORK, 3, METHOD_BUFFERED, FILE_WRITE_DATA)

#define IOCTL_MP_BASIC_TX_OFFLOAD \
    CTL_CODE(FILE_DEVICE_NETWORK, 4, METHOD_BUFFERED, FILE_WRITE_DATA)

#define IOCTL_LWF_BASIC_RX \
    CTL_CODE(FILE_DEVICE_NETWORK, 5, METHOD_BUFFERED, FILE_WRITE_DATA)

#define IOCTL_LWF_BASIC_TX \
    CTL_CODE(FILE_DEVICE_NETWORK, 6, METHOD_BUFFERED, FILE_WRITE_DATA)

#define IOCTL_LWF_BASIC_OID \
    CTL_CODE(FILE_DEVICE_NETWORK, 7, METHOD_BUFFERED, FILE_WRITE_DATA)

#define IOCTL_SOCK_BASIC_TCP \
    CTL_CODE(FILE_DEVICE_NETWORK, 8, METHOD_BUFFERED, FILE_WRITE_DATA)

#define IOCTL_SOCK_BASIC_RAW \
    CTL_CODE(FILE_DEVICE_NETWORK, 9, METHOD_BUFFERED, FILE_WRITE_DATA)

#define IOCTL_MP_BASIC_WATCHDOG \
    CTL_CODE(FILE_DEVICE_NETWORK, 10, METHOD_BUFFERED, FILE_WRITE_DATA)

#define IOCTL_MP_BASIC_PORT \
    CTL_CODE(FILE_DEVICE_NETWORK, 11, METHOD_BUFFERED, FILE_WRITE_DATA)

#define MAX_IOCTL_FUNC_CODE 11

EXTERN_C_END
