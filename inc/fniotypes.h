//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

//
// This header contains definitions for data IO related IOCTLs supported by the
// functional test miniport (fnmp).
//

EXTERN_C_START

#ifndef KERNEL_MODE
//
// This header depends on the following headers included in the following order.
// However, it is most likely too late to include these headers by the time this
// header is included.
//
#include <windows.h>
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <ndisoobtypes.h>
#endif

typedef struct _DATA_BUFFER {
    CONST UCHAR *VirtualAddress;
    UINT32 DataOffset;
    UINT32 DataLength;
    UINT32 BufferLength;
} DATA_BUFFER;

typedef struct _DATA_FRAME {
    DATA_BUFFER *Buffers;
    UINT16 BufferCount;

#pragma warning(push)
#pragma warning(disable:4201)  // nonstandard extension used: nameless struct/union
    union {
        //
        // Used when submitting IO.
        //
        struct {
            UINT32 RssHashQueueId;
            NDIS_TCP_IP_CHECKSUM_NET_BUFFER_LIST_INFO Checksum;
            NDIS_RSC_NBL_INFO Rsc;
        } Input;
        //
        // Used when retrieving filtered IO.
        //
        struct {
            PROCESSOR_NUMBER ProcessorNumber;
            UINT32 RssHash;
            NDIS_TCP_IP_CHECKSUM_NET_BUFFER_LIST_INFO Checksum;
            NDIS_TCP_LARGE_SEND_OFFLOAD_NET_BUFFER_LIST_INFO Lso;
            NDIS_UDP_SEGMENTATION_OFFLOAD_NET_BUFFER_LIST_INFO Uso;
        } Output;
    };
#pragma warning(pop)
} DATA_FRAME;

typedef struct _DATA_FLUSH_OPTIONS {
    struct {
        UINT32 DpcLevel : 1;
        UINT32 LowResources : 1;
        UINT32 RssCpu : 1;
    } Flags;

    UINT32 RssCpuQueueId;
} DATA_FLUSH_OPTIONS;

//
// Parameters for IOCTL_[RX|TX]_ENQUEUE.
//

typedef struct _DATA_ENQUEUE_IN {
    DATA_FRAME Frame;
} DATA_ENQUEUE_IN;

//
// Parameters for IOCTL_[RX|TX]_FLUSH.
//

typedef struct _DATA_FLUSH_IN {
    DATA_FLUSH_OPTIONS Options;
} DATA_FLUSH_IN;

//
// Parameters for IOCTL_[RX|TX]_FILTER.
//

typedef struct _DATA_FILTER_IN {
    const UCHAR *Pattern;
    const UCHAR *Mask;
    UINT32 Length;
} DATA_FILTER_IN;

//
// Parameters for IOCTL_[RX|TX]_GET_FRAME.
//

typedef struct _DATA_GET_FRAME_IN {
    UINT32 Index;
    UINT32 SubIndex;
} DATA_GET_FRAME_IN;

//
// Parameters for IOCTL_[RX|TX]_DEQUEUE_FRAME.
//

typedef struct _DATA_DEQUEUE_FRAME_IN {
    UINT32 Index;
} DATA_DEQUEUE_FRAME_IN;

//
// The OID request interface.
//

typedef enum _FNIO_OID_REQUEST_INTERFACE {
    //
    // The regular, NDIS-serialized OID request interface.
    //
    OID_REQUEST_INTERFACE_REGULAR,

    //
    // The direct OID request interface. These requests are not serialized and
    // can be pended.
    //
    OID_REQUEST_INTERFACE_DIRECT,

    OID_REQUEST_INTERFACE_MAX
} OID_REQUEST_INTERFACE;

//
// Helper type used by parameters for OID related IOCTLs.
//

typedef struct _OID_KEY {
    NDIS_OID Oid;
    NDIS_REQUEST_TYPE RequestType;
    OID_REQUEST_INTERFACE RequestInterface;
} OID_KEY;

//
// Offload interface.
//

typedef enum _FN_OFFLOAD_TYPE {
    FnOffloadCurrentConfig,
    FnOffloadHardwareCapabilities,
} FN_OFFLOAD_TYPE;

EXTERN_C_END
