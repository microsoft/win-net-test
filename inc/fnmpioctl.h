//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

#define FNMP_IOCTL_CURRENT_VERSION 2

#define FNMP_DEVICE_NAME L"\\Device\\FNMP"

#define FNMP_OPEN_PACKET_NAME "FnMpOpenPacket0"

//
// Type of functional test miniport object to create or open.
//
typedef enum _FNMP_FILE_TYPE {
    FNMP_FILE_TYPE_SHARED,
    FNMP_FILE_TYPE_EXCLUSIVE,
} FNMP_FILE_TYPE;

//
// Open packet, the common header for NtCreateFile extended attributes.
//
typedef struct _FNMP_OPEN_PACKET {
    UINT32 ApiVersion;
    FNMP_FILE_TYPE ObjectType;
} FNMP_OPEN_PACKET;

typedef struct _FNMP_OPEN_SHARED {
    UINT32 IfIndex;
} FNMP_OPEN_SHARED;

typedef struct _FNMP_OPEN_EXCLUSIVE {
    UINT32 IfIndex;
} FNMP_OPEN_EXCLUSIVE;

#define FNMP_IOCTL_RX_ENQUEUE \
    CTL_CODE(FILE_DEVICE_NETWORK, 0, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define FNMP_IOCTL_RX_FLUSH \
    CTL_CODE(FILE_DEVICE_NETWORK, 1, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define FNMP_IOCTL_TX_FILTER \
    CTL_CODE(FILE_DEVICE_NETWORK, 4, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define FNMP_IOCTL_TX_GET_FRAME \
    CTL_CODE(FILE_DEVICE_NETWORK, 5, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define FNMP_IOCTL_TX_DEQUEUE_FRAME \
    CTL_CODE(FILE_DEVICE_NETWORK, 6, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define FNMP_IOCTL_TX_FLUSH \
    CTL_CODE(FILE_DEVICE_NETWORK, 7, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define FNMP_IOCTL_MINIPORT_PAUSE_TIMESTAMP \
    CTL_CODE(FILE_DEVICE_NETWORK, 8, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define FNMP_IOCTL_MINIPORT_SET_MTU \
    CTL_CODE(FILE_DEVICE_NETWORK, 9, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define FNMP_IOCTL_OID_FILTER \
    CTL_CODE(FILE_DEVICE_NETWORK, 10, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define FNMP_IOCTL_OID_GET_REQUEST \
    CTL_CODE(FILE_DEVICE_NETWORK, 11, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define FNMP_IOCTL_OID_COMPLETE_REQUEST \
    CTL_CODE(FILE_DEVICE_NETWORK, 12, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define FNMP_IOCTL_MINIPORT_UPDATE_TASK_OFFLOAD \
    CTL_CODE(FILE_DEVICE_NETWORK, 13, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define FNMP_IOCTL_MINIPORT_ALLOCATE_PORT \
    CTL_CODE(FILE_DEVICE_NETWORK, 14, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define FNMP_IOCTL_MINIPORT_FREE_PORT \
    CTL_CODE(FILE_DEVICE_NETWORK, 15, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define FNMP_IOCTL_MINIPORT_ACTIVATE_PORT \
    CTL_CODE(FILE_DEVICE_NETWORK, 16, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define FNMP_IOCTL_MINIPORT_DEACTIVATE_PORT \
    CTL_CODE(FILE_DEVICE_NETWORK, 17, METHOD_BUFFERED, FILE_WRITE_ACCESS)

//
// Parameters for FNMP_IOCTL_MINIPORT_MTU.
//

typedef struct _MINIPORT_SET_MTU_IN {
    UINT32 Mtu;
} MINIPORT_SET_MTU_IN;

//
// Parameters for FNMP_IOCTL_OID_FILTER.
//
// InputBuffer: OID_KEY[]
// InputBufferLength: sizeof(OID_KEY[])
//

//
// Parameters for FNMP_IOCTL_OID_GET_REQUEST.
//
// InputBuffer: OID_KEY
// InputBufferLength: sizeof(OID_KEY)
//

//
// Parameters for FNMP_IOCTL_OID_COMPLETE_REQUEST.
//

typedef struct _OID_COMPLETE_REQUEST_IN_V0 {
    OID_KEY_V0 Key;
    NDIS_STATUS Status;
    UINT32 InformationBufferLength;
    const VOID *InformationBuffer;
} OID_COMPLETE_REQUEST_IN_V0;

typedef struct _OID_COMPLETE_REQUEST_IN {
    const OID_KEY *Key;
    NDIS_STATUS Status;
    UINT32 InformationBufferLength;
    const VOID *InformationBuffer;
} OID_COMPLETE_REQUEST_IN;

//
// Parameters for FNMP_IOCTL_MINIPORT_UPDATE_TASK_OFFLOAD.
//

typedef struct _MINIPORT_UPDATE_TASK_OFFLOAD_IN {
    FN_OFFLOAD_TYPE OffloadType;
    const NDIS_OFFLOAD_PARAMETERS *OffloadParameters;
    UINT32 OffloadParametersLength;
    const FN_OFFLOAD_OPTIONS *OffloadOptions;
} MINIPORT_UPDATE_TASK_OFFLOAD_IN;

//
// Parameters for FNMP_IOCTL_MINIPORT_ALLOCATE_PORT.
//
// OutputBuffer: NDIS_PORT_NUMBER
// OutputBufferLength: sizeof(NDIS_PORT_NUMBER)
//

//
// Parameters for FNMP_IOCTL_MINIPORT_FREE_PORT.
//
// InputBuffer: NDIS_PORT_NUMBER
// InputBufferLength: sizeof(NDIS_PORT_NUMBER)
//

//
// Parameters for FNMP_IOCTL_MINIPORT_ACTIVATE_PORT.
//
// InputBuffer: NDIS_PORT_NUMBER
// InputBufferLength: sizeof(NDIS_PORT_NUMBER)
//

//
// Parameters for FNMP_IOCTL_MINIPORT_DEACTIVATE_PORT.
//
// InputBuffer: NDIS_PORT_NUMBER
// InputBufferLength: sizeof(NDIS_PORT_NUMBER)
//

EXTERN_C_END
