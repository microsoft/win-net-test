//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#define ETH_HDR_LEN 14
#define MAC_ADDR_LEN 6
#define MAX_MULTICAST_ADDRESSES 16

#define MAX_RSS_QUEUES 64

#define TRY_READ_INT_CONFIGURATION(hConfig, Keyword, pValue) \
    { \
        NDIS_STATUS _Status; \
        PNDIS_CONFIGURATION_PARAMETER Parameter; \
        NdisReadConfiguration(&_Status, &Parameter, (hConfig), RTL_CONST_CAST(PNDIS_STRING)(Keyword), NdisParameterInteger); \
        if (_Status == NDIS_STATUS_SUCCESS) \
        { \
            *(pValue) = Parameter->ParameterData.IntegerData; \
        } \
    }

typedef struct _ADAPTER_SHARED ADAPTER_SHARED;
typedef struct _EXCLUSIVE_USER_CONTEXT EXCLUSIVE_USER_CONTEXT;

typedef struct DECLSPEC_CACHEALIGN {
    UINT32 QueueId;
    UINT32 RssHash;
    UINT32 ProcessorIndex;
} ADAPTER_QUEUE;

typedef enum _CHECKSUM_OFFLOAD_STATE {
    ChecksumOffloadDisabled = 0,
    ChecksumOffloadTx = 1,
    ChecksumOffloadRx = 2,
    ChecksumOffloadRxTx = 3,
} CHECKSUM_OFFLOAD_STATE;

C_ASSERT((ChecksumOffloadTx & ChecksumOffloadRx) == 0);
C_ASSERT((ChecksumOffloadTx | ChecksumOffloadRx) == ChecksumOffloadRxTx);

typedef struct _ADAPTER_OFFLOAD {
    CHECKSUM_OFFLOAD_STATE IPChecksumOffloadIPv4;
    CHECKSUM_OFFLOAD_STATE TCPChecksumOffloadIPv4;
    CHECKSUM_OFFLOAD_STATE TCPChecksumOffloadIPv6;
    CHECKSUM_OFFLOAD_STATE UDPChecksumOffloadIPv4;
    CHECKSUM_OFFLOAD_STATE UDPChecksumOffloadIPv6;
    UINT32 LsoV2IPv4;
    UINT32 LsoV2IPv6;
    UINT32 UsoIPv4;
    UINT32 UsoIPv6;
    UINT32 RscIPv4;
    UINT32 RscIPv6;
    UINT32 UdpRsc;
    UINT32 GsoMaxOffloadSize;
} ADAPTER_OFFLOAD;

typedef struct _ADAPTER_CONTEXT {
    LIST_ENTRY AdapterListLink;
    NDIS_HANDLE MiniportHandle;
    NET_IFINDEX IfIndex;

    INT64 ReferenceCount;

    UCHAR MACAddress[MAC_ADDR_LEN];
    ULONG MtuSize;
    ULONG CurrentPacketFilter;
    ULONG CurrentLookAhead;
    UCHAR MulticastAddressList[MAC_ADDR_LEN * MAX_MULTICAST_ADDRESSES];
    ULONG NumMulticastAddresses;
    LARGE_INTEGER LastPauseTimestamp;

    ADAPTER_QUEUE *RssQueues;
    ULONG RssEnabled;
    ULONG NumRssProcs;
    ULONG NumRssQueues;

    UINT32 Encapsulation;
    ADAPTER_OFFLOAD OffloadConfig;
    ADAPTER_OFFLOAD OffloadCapabilities;

    KSPIN_LOCK Lock;
    EX_PUSH_LOCK PushLock;
    const OID_KEY *OidFilterKeys;
    UINT32 OidFilterKeyCount;
    LIST_ENTRY FilteredOidRequestLists[OID_REQUEST_INTERFACE_MAX];
    LIST_ENTRY PortList;
    FN_TIMER_HANDLE WatchdogTimer;

    //
    // Context for an exclusive user mode handle for configuring the adapter.
    //
    EXCLUSIVE_USER_CONTEXT *UserContext;

    ADAPTER_SHARED *Shared;
} ADAPTER_CONTEXT;

typedef struct _GLOBAL_CONTEXT {
    EX_PUSH_LOCK Lock;
    LIST_ENTRY AdapterList;
    HANDLE NdisMiniportDriverHandle;
    UINT32 NdisVersion;

    NDIS_MEDIUM Medium;
    ULONG PacketFilter;
    ULONG LinkSpeed;
    ULONG64 MaxXmitLinkSpeed;
    ULONG64 XmitLinkSpeed;
    ULONG64 MaxRecvLinkSpeed;
    ULONG64 RecvLinkSpeed;
} GLOBAL_CONTEXT;

extern GLOBAL_CONTEXT MpGlobalContext;

VOID
MpDereferenceAdapter(
    _In_ ADAPTER_CONTEXT *Adapter
    );

ADAPTER_CONTEXT *
MpFindAdapter(
    _In_ UINT32 IfIndex
    );

VOID
MpIndicateStatus(
    _In_ CONST ADAPTER_CONTEXT *Adapter,
    _In_ VOID *Buffer,
    _In_ UINT32 BufferSize,
    _In_ UINT32 StatusCode
    );

NDIS_STATUS
MpSetOffloadParameters(
    _Inout_ ADAPTER_CONTEXT *Adapter,
    _Inout_ ADAPTER_OFFLOAD *AdapterOffload,
    _In_ CONST NDIS_OFFLOAD_PARAMETERS *OffloadParameters,
    _In_ UINT32 OffloadParametersLength,
    _In_ UINT32 StatusCode
    );

NDIS_STATUS
MpReadOffload(
    _Inout_ ADAPTER_CONTEXT *Adapter,
    _In_ NDIS_HANDLE ConfigHandle,
    _In_ FN_OFFLOAD_TYPE Store
    );

NDIS_STATUS
MpOpenConfiguration(
    _Out_ NDIS_HANDLE *ConfigHandle,
    _In_ ADAPTER_CONTEXT *Adapter
    );

ADAPTER_OFFLOAD *
MpGetOffload(
    _In_ ADAPTER_CONTEXT *Adapter,
    _In_ FN_OFFLOAD_TYPE Store
    );

VOID
MpFillOffload(
    _Out_ NDIS_OFFLOAD *Offload,
    _In_ ADAPTER_CONTEXT *Adapter,
    _In_ ADAPTER_OFFLOAD *AdapterOffload
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
MpWatchdogFailure(
    _In_ ADAPTER_CONTEXT *Adapter,
    _In_z_ CONST CHAR *WatchdogType
    );
