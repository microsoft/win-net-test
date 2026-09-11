//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

#define FNLWF_IOCTL_CURRENT_VERSION 2

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
#define FNLWF_IOCTL_PNP_EVENT_SET_FILTER \
    CTL_CODE(FILE_DEVICE_NETWORK, 11, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define FNLWF_IOCTL_PNP_EVENT_GET \
    CTL_CODE(FILE_DEVICE_NETWORK, 12, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define FNLWF_IOCTL_PNP_EVENT_COMPLETE \
    CTL_CODE(FILE_DEVICE_NETWORK, 13, METHOD_BUFFERED, FILE_WRITE_ACCESS)


//
// Parameters for FNLWF_IOCTL_OID_SUBMIT_REQUEST.
//

typedef struct _OID_SUBMIT_REQUEST_IN_V0 {
    OID_KEY_V0 Key;
    VOID *InformationBuffer;
    UINT32 InformationBufferLength;
} OID_SUBMIT_REQUEST_IN_V0;

typedef struct _OID_SUBMIT_REQUEST_IN {
    const OID_KEY *Key;
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

//
// Mirrors of the public netpnp.h enums, which are unavailable to user mode.
// fnlwf asserts these match the real values at compile time.
//

typedef enum _FNLWF_NET_PNP_EVENT_CODE {
    FnLwfNetEventSetPower = 0,
    FnLwfNetEventQueryPower = 1,
    FnLwfNetEventQueryRemoveDevice = 2,
    FnLwfNetEventCancelRemoveDevice = 3,
    FnLwfNetEventReconfigure = 4,
    FnLwfNetEventBindList = 5,
    FnLwfNetEventBindsComplete = 6,
    FnLwfNetEventPnPCapabilities = 7,
    FnLwfNetEventPause = 8,
    FnLwfNetEventRestart = 9,
    FnLwfNetEventPortActivation = 10,
    FnLwfNetEventPortDeactivation = 11,
    FnLwfNetEventIMReEnableDevice = 12,
    FnLwfNetEventNDKEnable = 13,
    FnLwfNetEventNDKDisable = 14,
    FnLwfNetEventFilterPreDetach = 15,
    FnLwfNetEventBindFailed = 16,
    FnLwfNetEventSwitchActivate = 17,
    FnLwfNetEventAllowBindsAbove = 18,
    FnLwfNetEventInhibitBindsAbove = 19,
    FnLwfNetEventAllowStart = 20,
    FnLwfNetEventRequirePause = 21,
    FnLwfNetEventUploadGftFlowEntries = 22,
} FNLWF_NET_PNP_EVENT_CODE;

//
// Buffer contents for FnLwfNetEventSetPower.
//
typedef enum _FNLWF_NET_DEVICE_POWER_STATE {
    FnLwfNetDeviceStateUnspecified = 0,
    FnLwfNetDeviceStateD0 = 1,
    FnLwfNetDeviceStateD1 = 2,
    FnLwfNetDeviceStateD2 = 3,
    FnLwfNetDeviceStateD3 = 4,
} FNLWF_NET_DEVICE_POWER_STATE;

#define FNLWF_PNP_EVENT_SUCCESS ((NDIS_STATUS)0)

//
// Parameters for FNLWF_IOCTL_PNP_EVENT_SET_FILTER.
//

typedef struct _PNP_EVENT_FILTER_IN {
    UINT32 NetEvent;
    BOOLEAN PendEvents;
    BOOLEAN QueueEvents;
} PNP_EVENT_FILTER_IN;

//
// Output for FNLWF_IOCTL_PNP_EVENT_GET_NOTIFICATION. The event buffer, whose
// contents depend on NetEvent, immediately follows this header.
//

typedef struct _PNP_EVENT_NOTIFICATION_OUT {
    UINT32 NetEvent;
    UINT32 BufferLength;
} PNP_EVENT_NOTIFICATION_OUT;

//
// Parameters for FNLWF_IOCTL_PNP_EVENT_COMPLETE. A non-success status fails the
// event instead of forwarding it.
//

typedef struct _PNP_EVENT_COMPLETE_IN {
    NDIS_STATUS CompletionStatus;
} PNP_EVENT_COMPLETE_IN;

EXTERN_C_END
