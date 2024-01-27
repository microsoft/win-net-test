//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#pragma warning(push)
#pragma warning(disable:4201) // nonstandard extension used: nameless struct/union

EXTERN_C_START

typedef struct _NDIS_TCP_IP_CHECKSUM_NET_BUFFER_LIST_INFO
{
    union
    {
        struct
        {
            ULONG IsIPv4 :1;
            ULONG IsIPv6 :1;
            ULONG TcpChecksum :1;
            ULONG UdpChecksum :1;
            ULONG IpHeaderChecksum :1;
            ULONG Reserved :11;
            ULONG TcpHeaderOffset :10;
        } Transmit;

        struct
        {
            ULONG TcpChecksumFailed :1;
            ULONG UdpChecksumFailed :1;
            ULONG IpChecksumFailed :1;
            ULONG TcpChecksumSucceeded :1;
            ULONG UdpChecksumSucceeded :1;
            ULONG IpChecksumSucceeded :1;
            ULONG Loopback :1;
#if NDIS_SUPPORT_NDIS630
            ULONG TcpChecksumValueInvalid :1;
            ULONG IpChecksumValueInvalid :1;
#endif // NDIS_SUPPORT_NDIS630
        } Receive;

        PVOID Value;
    };
} NDIS_TCP_IP_CHECKSUM_NET_BUFFER_LIST_INFO, *PNDIS_TCP_IP_CHECKSUM_NET_BUFFER_LIST_INFO;

//
// NDIS_TCP_LARGE_SEND_OFFLOAD_NET_BUFFER_LIST_INFO.Transmit.Type
//
#define NDIS_TCP_LARGE_SEND_OFFLOAD_V1_TYPE     0
#define NDIS_TCP_LARGE_SEND_OFFLOAD_V2_TYPE     1

//
// NDIS_TCP_LARGE_SEND_OFFLOAD_NET_BUFFER_LIST_INFO.LsoV2Transmit.IPVersion
//
#define NDIS_TCP_LARGE_SEND_OFFLOAD_IPv4        0
#define NDIS_TCP_LARGE_SEND_OFFLOAD_IPv6        1

//
// The maximum length of the headers (MAC+IP+IP option or extension
// headers+TCP+TCP options) when the protocol does large send offload.  If
// header is bigger than this value, it will not do LSO.
//
#define NDIS_LARGE_SEND_OFFLOAD_MAX_HEADER_LENGTH 128

//
// Per-NetBufferList information for TcpLargeSendNetBufferListInfo.
//
typedef struct _NDIS_TCP_LARGE_SEND_OFFLOAD_NET_BUFFER_LIST_INFO
{
    union
    {
        struct
        {
            ULONG Unused :30;
            ULONG Type :1;
            ULONG Reserved2 :1;
        } Transmit;

        struct
        {
            ULONG MSS :20;
            ULONG TcpHeaderOffset :10;
            ULONG Type :1;
            ULONG Reserved2 :1;
        } LsoV1Transmit;

        struct
        {
            ULONG TcpPayload :30;
            ULONG Type :1;
            ULONG Reserved2 :1;
        } LsoV1TransmitComplete;

        struct
        {
            ULONG MSS :20;
            ULONG TcpHeaderOffset :10;
            ULONG Type :1;
            ULONG IPVersion :1;
        } LsoV2Transmit;

        struct
        {
            ULONG Reserved :30;
            ULONG Type :1;
            ULONG Reserved2 :1;
        } LsoV2TransmitComplete;

        PVOID Value;
    };
} NDIS_TCP_LARGE_SEND_OFFLOAD_NET_BUFFER_LIST_INFO, *PNDIS_TCP_LARGE_SEND_OFFLOAD_NET_BUFFER_LIST_INFO;

//
// IP protocol version encoded in IPVersion field of
// NDIS_UDP_SEGMENTATION_OFFLOAD_NET_BUFFER_LIST_INFO->Transmit.IPVersion
//
#define NDIS_UDP_SEGMENTATION_OFFLOAD_IPV4      0
#define NDIS_UDP_SEGMENTATION_OFFLOAD_IPV6      1

//
// Per-NetBufferList information for UdpSegmentationOffloadInfo.
//
typedef struct _NDIS_UDP_SEGMENTATION_OFFLOAD_NET_BUFFER_LIST_INFO
{
    union
    {
        struct
        {
            ULONG MSS :20;
            ULONG UdpHeaderOffset :10;
            ULONG Reserved :1;
            ULONG IPVersion :1;
        } Transmit;

        PVOID Value;
    };
} NDIS_UDP_SEGMENTATION_OFFLOAD_NET_BUFFER_LIST_INFO, *PNDIS_UDP_SEGMENTATION_OFFLOAD_NET_BUFFER_LIST_INFO;

//
// TcpRecvSegCoalesceInfo
//
typedef union _NDIS_RSC_NBL_INFO
{
    struct
    {
        USHORT CoalescedSegCount;
        USHORT DupAckCount;
    } Info;

    PVOID Value;
} NDIS_RSC_NBL_INFO, *PNDIS_RSC_NBL_INFO;

C_ASSERT(sizeof(NDIS_RSC_NBL_INFO) == sizeof(PVOID));

EXTERN_C_END

#pragma warning(pop)
