//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

#define FNLWF_IOCTL_CURRENT_VERSION 1

#define FNLWF_DEVICE_NAME L"\\Device\\fnlwf"

#define FNLWF_OPEN_PACKET_NAME "FnLwfOpenPacket0"

//
// Type of functional test LWF object to create or open.
//
typedef enum _FNLWF_FILE_TYPE {
    FNLWF_FILE_TYPE_DEFAULT,
} FNLWF_FILE_TYPE;

//
// Open packet, the common header for NtCreateFile extended attributes.
//
typedef struct _FNLWF_OPEN_PACKET {
    UINT32 ApiVersion;
    FNLWF_FILE_TYPE ObjectType;
} FNLWF_OPEN_PACKET;

typedef struct _FNLWF_OPEN_DEFAULT {
    UINT32 IfIndex;
} FNLWF_OPEN_DEFAULT;

#define FNLWF_IOCTL_RX_FILTER \
    CTL_CODE(FILE_DEVICE_NETWORK, 1, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define FNLWF_IOCTL_RX_GET_FRAME \
    CTL_CODE(FILE_DEVICE_NETWORK, 2, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define FNLWF_IOCTL_RX_DEQUEUE_FRAME \
    CTL_CODE(FILE_DEVICE_NETWORK, 3, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define FNLWF_IOCTL_RX_FLUSH \
    CTL_CODE(FILE_DEVICE_NETWORK, 4, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define FNLWF_IOCTL_TX_ENQUEUE \
    CTL_CODE(FILE_DEVICE_NETWORK, 5, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define FNLWF_IOCTL_TX_FLUSH \
    CTL_CODE(FILE_DEVICE_NETWORK, 6, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define FNLWF_IOCTL_OID_SUBMIT_REQUEST \
    CTL_CODE(FILE_DEVICE_NETWORK, 7, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define FNLWF_IOCTL_STATUS_SET_FILTER \
    CTL_CODE(FILE_DEVICE_NETWORK, 8, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define FNLWF_IOCTL_STATUS_GET_INDICATION \
    CTL_CODE(FILE_DEVICE_NETWORK, 9, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define FNLWF_IOCTL_DATAPATH_GET_STATE \
    CTL_CODE(FILE_DEVICE_NETWORK, 10, METHOD_BUFFERED, FILE_WRITE_ACCESS)


//
// Parameters for FNLWF_IOCTL_OID_SUBMIT_REQUEST.
//

typedef struct _OID_SUBMIT_REQUEST_IN {
    OID_KEY Key;
    VOID *InformationBuffer;
    UINT32 InformationBufferLength;
} OID_SUBMIT_REQUEST_IN;

//
// Parameters for FNLWF_IOCTL_STATUS_SET_FILTER.
//

typedef struct _STATUS_FILTER_IN {
    NDIS_STATUS StatusCode;
    BOOLEAN BlockIndications;
    BOOLEAN QueueIndications;
} STATUS_FILTER_IN;

EXTERN_C_END
