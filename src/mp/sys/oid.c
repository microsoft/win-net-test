//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "oid.tmh"

CONST NDIS_OID MpSupportedOidArray[] =
{
    OID_GEN_SUPPORTED_LIST,
    OID_GEN_HARDWARE_STATUS,
    OID_GEN_MEDIA_SUPPORTED,
    OID_GEN_MEDIA_IN_USE,
    OID_GEN_MAXIMUM_LOOKAHEAD,
    OID_GEN_MAXIMUM_FRAME_SIZE,
    OID_GEN_LINK_SPEED,
    OID_GEN_TRANSMIT_BUFFER_SPACE,
    OID_GEN_RECEIVE_BUFFER_SPACE,
    OID_GEN_TRANSMIT_BLOCK_SIZE,
    OID_GEN_RECEIVE_BLOCK_SIZE,
    OID_GEN_VENDOR_ID,
    OID_GEN_VENDOR_DESCRIPTION,
    OID_GEN_CURRENT_PACKET_FILTER,
    OID_GEN_CURRENT_LOOKAHEAD,
    OID_GEN_MAXIMUM_TOTAL_SIZE,
    OID_GEN_MAC_OPTIONS,
    OID_GEN_MEDIA_CONNECT_STATUS,
    OID_GEN_LINK_SPEED_EX,
    OID_GEN_MAX_LINK_SPEED,
    OID_GEN_MEDIA_CONNECT_STATUS_EX,
    OID_GEN_MEDIA_DUPLEX_STATE,
    OID_GEN_LINK_STATE,
    OID_GEN_STATISTICS,
    OID_802_3_PERMANENT_ADDRESS,
    OID_802_3_CURRENT_ADDRESS,
    OID_802_3_MULTICAST_LIST,
    OID_802_3_MAXIMUM_LIST_SIZE,
    OID_PNP_SET_POWER,
    OID_PNP_QUERY_POWER,
    OID_OFFLOAD_ENCAPSULATION,
    OID_GEN_RECEIVE_SCALE_PARAMETERS,
    OID_TCP_OFFLOAD_PARAMETERS,
    OID_TCP_OFFLOAD_HW_PARAMETERS,
    OID_QUIC_CONNECTION_ENCRYPTION,
    OID_QUIC_CONNECTION_ENCRYPTION_PROTOTYPE,
};

CONST UINT32 MpSupportedOidArraySize = sizeof(MpSupportedOidArray);

static PCSTR MpDriverFriendlyName = "FNMP";

typedef struct _FN_OID_REQUEST_ENTRY {
    NDIS_OID_REQUEST *NdisRequest;
    LIST_ENTRY Entry;
    ULONGLONG Timestamp;
} FN_OID_REQUEST_ENTRY;

static
VOID
MpFreeOidRequestEntry(
    _In_ FN_OID_REQUEST_ENTRY *Entry
    )
{
    ExFreePoolWithTag(Entry, POOLTAG_MP_OID);
}

static
FN_OID_REQUEST_ENTRY *
MpCreateOidRequestEntry(
    _In_ NDIS_OID_REQUEST *NdisRequest
    )
{
    FN_OID_REQUEST_ENTRY *Entry =
        ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Entry), POOLTAG_MP_OID);

    if (Entry != NULL) {
        InitializeListHead(&Entry->Entry);
        Entry->NdisRequest = NdisRequest;
        Entry->Timestamp = KeQueryUnbiasedInterruptTime();
    }

    return Entry;
}

static
FN_OID_REQUEST_ENTRY *
MpListEntryToOidRequestEntry(
    _In_ LIST_ENTRY *ListEntry
    )
{
    return CONTAINING_RECORD(ListEntry, FN_OID_REQUEST_ENTRY, Entry);
}

static
NDIS_STATUS
MpFilterOid(
    _In_ ADAPTER_CONTEXT *Adapter,
    _In_ OID_REQUEST_INTERFACE RequestInterface,
    _In_ NDIS_OID_REQUEST *NdisRequest
    )
{
    NTSTATUS Status = NDIS_STATUS_SUCCESS;
    KIRQL OldIrql;

    //
    // Pend the OID if we have a matching filter.
    //

    KeAcquireSpinLock(&Adapter->Lock, &OldIrql);

    for (UINT32 Index = 0; Index < Adapter->OidFilterKeyCount; Index++) {
        const OID_KEY *Filter = &Adapter->OidFilterKeys[Index];
        if (NdisRequest->DATA.Oid == Filter->Oid &&
            NdisRequest->RequestType == Filter->RequestType &&
            RequestInterface == Filter->RequestInterface &&
            NdisRequest->PortNumber == Filter->PortNumber) {
            FN_OID_REQUEST_ENTRY *OidEntry;

            OidEntry = MpCreateOidRequestEntry(NdisRequest);
            if (OidEntry == NULL) {
                Status = NDIS_STATUS_RESOURCES;
                break;
            }

            InsertTailList(
                &Adapter->FilteredOidRequestLists[RequestInterface],
                &OidEntry->Entry);
            Status = NDIS_STATUS_PENDING;
            break;
        }
    }

    KeReleaseSpinLock(&Adapter->Lock, OldIrql);

    return Status;
}

static
NDIS_STATUS
MpProcessQueryOid(
   _In_ ADAPTER_CONTEXT *Adapter,
    _In_ OID_REQUEST_INTERFACE RequestInterface,
   _Inout_ NDIS_OID_REQUEST *NdisRequest,
   _In_ BOOLEAN ShouldFilter
   )
{
    NDIS_STATUS Status;
    NDIS_OID Oid = NdisRequest->DATA.QUERY_INFORMATION.Oid;
    VOID *InformationBuffer = NdisRequest->DATA.QUERY_INFORMATION.InformationBuffer;
    ULONG InformationBufferLength = NdisRequest->DATA.QUERY_INFORMATION.InformationBufferLength;
    PUINT BytesWritten = &NdisRequest->DATA.QUERY_INFORMATION.BytesWritten;
    PUINT BytesNeeded = &NdisRequest->DATA.QUERY_INFORMATION.BytesNeeded;
    BOOLEAN DoCopy = TRUE;
    ULONG LocalData = 0;
    ULONG DataLength = sizeof(LocalData);
    VOID *Data = &LocalData;
    NDIS_LINK_SPEED LinkSpeed;
    NDIS_LINK_STATE LinkState;
    NDIS_STATISTICS_INFO StatisticsInfo;

    *BytesWritten = 0;

    Status = NDIS_STATUS_SUCCESS;

    if (ShouldFilter) {
        Status = MpFilterOid(Adapter, RequestInterface, NdisRequest);
        if (Status != NDIS_STATUS_SUCCESS) {
            return Status;
        }
    }

    switch (Oid)
    {
        case OID_GEN_SUPPORTED_LIST:
            DoCopy = FALSE;
            DataLength = sizeof(MpSupportedOidArray);
            if (InformationBufferLength < DataLength)
            {
                *BytesNeeded = DataLength;
                Status = NDIS_STATUS_BUFFER_TOO_SHORT;
                break;
            }
            NdisMoveMemory(InformationBuffer,
                            MpSupportedOidArray,
                            sizeof(MpSupportedOidArray));

            *BytesWritten = DataLength;
            break;

        case OID_GEN_HARDWARE_STATUS:
            LocalData = NdisHardwareStatusReady;
            break;

        case OID_GEN_MEDIA_SUPPORTED:
        case OID_GEN_MEDIA_IN_USE:
            Data = &MpGlobalContext.Medium;
            break;

        case OID_GEN_MAXIMUM_LOOKAHEAD:
            Data = &Adapter->MtuSize;
            break;

        case OID_GEN_CURRENT_LOOKAHEAD:
            Data = &Adapter->CurrentLookAhead;
            break;

        case OID_GEN_MAXIMUM_FRAME_SIZE:
            Data = &Adapter->MtuSize;
            break;

        case OID_GEN_LINK_SPEED:
            Data = &MpGlobalContext.LinkSpeed;
            break;

        case OID_GEN_TRANSMIT_BUFFER_SPACE:
        case OID_GEN_RECEIVE_BUFFER_SPACE:
        case OID_GEN_MAXIMUM_TOTAL_SIZE:
        case OID_GEN_TRANSMIT_BLOCK_SIZE:
        case OID_GEN_RECEIVE_BLOCK_SIZE:
            LocalData = Adapter->MtuSize + ETH_HDR_LEN;
            break;

        case OID_GEN_VENDOR_ID:
            LocalData = 0x00FFFFFF;
            break;

        case OID_GEN_VENDOR_DESCRIPTION:
            DataLength = (ULONG)(sizeof(VOID*));
            Data = (VOID *)MpDriverFriendlyName;
            break;

        case OID_GEN_CURRENT_PACKET_FILTER:
            Data = &Adapter->CurrentPacketFilter;
            break;

        case OID_GEN_MAC_OPTIONS:
            LocalData  = (ULONG)(NDIS_MAC_OPTION_NO_LOOPBACK |
                         NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
                         NDIS_MAC_OPTION_TRANSFERS_NOT_PEND);
            break;

        case OID_GEN_LINK_STATE:
            LinkState.XmitLinkSpeed = MpGlobalContext.XmitLinkSpeed;
            LinkState.RcvLinkSpeed  = MpGlobalContext.RecvLinkSpeed;
            LinkState.MediaConnectState = MediaConnectStateConnected;
            DataLength = sizeof(NDIS_LINK_STATE);
            Data = &LinkState;
            break;

        case OID_GEN_MEDIA_CONNECT_STATUS:
            LocalData = NdisMediaStateConnected;
            break;

        case OID_802_3_PERMANENT_ADDRESS:
            DataLength  = MAC_ADDR_LEN;
            Data = Adapter->MACAddress;
            break;

        case OID_802_3_CURRENT_ADDRESS:
            DataLength = MAC_ADDR_LEN;
            Data = Adapter->MACAddress;
            break;

        case OID_802_3_MAXIMUM_LIST_SIZE:
            LocalData = MAX_MULTICAST_ADDRESSES;
            break;

        case OID_802_3_MULTICAST_LIST:
            DataLength = Adapter->NumMulticastAddresses * MAC_ADDR_LEN;
            if (MpGlobalContext.Medium != NdisMedium802_3)
            {
                Status = NDIS_STATUS_INVALID_OID;
                break;
            }
            else if ((InformationBufferLength % ETH_LENGTH_OF_ADDRESS) != 0)
            {
                Status = NDIS_STATUS_INVALID_LENGTH;
                break;
            }
            Data = Adapter->MulticastAddressList;
            break;

        case OID_PNP_QUERY_POWER:
            break;

        case OID_GEN_LINK_SPEED_EX:
            LinkSpeed.XmitLinkSpeed = MpGlobalContext.XmitLinkSpeed;
            LinkSpeed.RcvLinkSpeed  = MpGlobalContext.RecvLinkSpeed;
            DataLength = sizeof(NDIS_LINK_SPEED);
            Data = &LinkSpeed;
            break;

        case OID_GEN_MAX_LINK_SPEED:
            LinkSpeed.XmitLinkSpeed = MpGlobalContext.MaxXmitLinkSpeed;
            LinkSpeed.RcvLinkSpeed  = MpGlobalContext.MaxRecvLinkSpeed;
            DataLength = sizeof(NDIS_LINK_SPEED);
            Data = &LinkSpeed;
            break;

        case OID_GEN_MEDIA_CONNECT_STATUS_EX:
            LocalData = MediaConnectStateConnected;
            break;

        case OID_GEN_MEDIA_DUPLEX_STATE:
            LocalData = MediaDuplexStateFull;
            break;

        case OID_GEN_STATISTICS:
            RtlZeroMemory(&StatisticsInfo, sizeof(StatisticsInfo));
            StatisticsInfo.Header.Revision = NDIS_OBJECT_REVISION_1;
            StatisticsInfo.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            StatisticsInfo.Header.Size = sizeof(NDIS_STATISTICS_INFO);
            StatisticsInfo.SupportedStatistics =
                            NDIS_STATISTICS_FLAGS_VALID_RCV_DISCARDS |
                            NDIS_STATISTICS_FLAGS_VALID_BYTES_RCV |
                            NDIS_STATISTICS_FLAGS_VALID_XMIT_DISCARDS |
                            NDIS_STATISTICS_FLAGS_VALID_BYTES_XMIT |
                            NDIS_STATISTICS_FLAGS_VALID_DIRECTED_FRAMES_RCV |
                            NDIS_STATISTICS_FLAGS_VALID_DIRECTED_FRAMES_XMIT |
                            NDIS_STATISTICS_FLAGS_VALID_BROADCAST_FRAMES_RCV |
                            NDIS_STATISTICS_FLAGS_VALID_BROADCAST_FRAMES_XMIT;
#if 0
            StatisticsInfo.ifInDiscards = enlStats.RxDrops;
            StatisticsInfo.ifHCInOctets = enlStats.RxBytes;
            StatisticsInfo.ifHCInUcastPkts = enlStats.RxPkts;
            StatisticsInfo.ifHCInBroadcastPkts = enlStats.EmptyTicks;
            StatisticsInfo.ifHCOutOctets = enlStats.TxBytes;
            StatisticsInfo.ifHCOutUcastPkts = enlStats.TxPkts;
            StatisticsInfo.ifHCOutBroadcastPkts = enlStats.BusyTicks;
            StatisticsInfo.ifOutDiscards = enlStats.TxDrops + pAdapter->EnlTxDrops;
#endif // TODO

            Data = &StatisticsInfo;
            DataLength = sizeof(NDIS_STATISTICS_INFO);
            break;

        default:
            DoCopy = FALSE;
            Status = NDIS_STATUS_NOT_SUPPORTED;
            break;
    }

    if (DoCopy)
    {
        if (InformationBufferLength < DataLength)
        {
            *BytesNeeded = DataLength;
            Status = NDIS_STATUS_BUFFER_TOO_SHORT;
        }
        else
        {
            NdisMoveMemory(InformationBuffer, Data, DataLength);
            *BytesWritten = DataLength;
        }
    }

    return Status;
}

static
NDIS_STATUS
MpProcessSetOid(
    _In_ ADAPTER_CONTEXT *Adapter,
    _In_ OID_REQUEST_INTERFACE RequestInterface,
    _Inout_ NDIS_OID_REQUEST *NdisRequest,
    _In_ BOOLEAN ShouldFilter
    )
{
    NDIS_STATUS Status;
    NDIS_OID Oid = NdisRequest->DATA.SET_INFORMATION.Oid;
    VOID *InformationBuffer = NdisRequest->DATA.SET_INFORMATION.InformationBuffer;
    ULONG InformationBufferLength = NdisRequest->DATA.SET_INFORMATION.InformationBufferLength;
    PUINT BytesRead = &NdisRequest->DATA.SET_INFORMATION.BytesRead;

    Status = NDIS_STATUS_SUCCESS;

    if (ShouldFilter) {
        Status = MpFilterOid(Adapter, RequestInterface, NdisRequest);
        if (Status != NDIS_STATUS_SUCCESS) {
            return Status;
        }
    }

    switch (Oid) {
        case OID_OFFLOAD_ENCAPSULATION:

            if (InformationBufferLength < NDIS_SIZEOF_OFFLOAD_ENCAPSULATION_REVISION_1) {
                Status = NDIS_STATUS_INVALID_LENGTH;
                break;
            }

            NDIS_OFFLOAD_ENCAPSULATION *OffloadEncap;
            OffloadEncap = (NDIS_OFFLOAD_ENCAPSULATION *)InformationBuffer;

            if ((OffloadEncap->Header.Type != NDIS_OBJECT_TYPE_OFFLOAD_ENCAPSULATION) ||
                (OffloadEncap->Header.Revision < NDIS_OFFLOAD_ENCAPSULATION_REVISION_1) ||
                (OffloadEncap->Header.Size < NDIS_SIZEOF_OFFLOAD_ENCAPSULATION_REVISION_1)) {
                Status = NDIS_STATUS_INVALID_PARAMETER;
                break;
            }

            if (OffloadEncap->IPv6.Enabled == NDIS_OFFLOAD_SET_ON) {
                if (OffloadEncap->IPv6.EncapsulationType != NDIS_ENCAPSULATION_IEEE_802_3 ||
                    OffloadEncap->IPv6.HeaderSize != ETH_HDR_LEN) {
                    Status = NDIS_STATUS_NOT_SUPPORTED;
                    break;
                }
            }

            if (OffloadEncap->IPv4.Enabled == NDIS_OFFLOAD_SET_ON) {
                if (OffloadEncap->IPv4.EncapsulationType != NDIS_ENCAPSULATION_IEEE_802_3 ||
                    OffloadEncap->IPv4.HeaderSize != ETH_HDR_LEN) {
                    Status = NDIS_STATUS_NOT_SUPPORTED;
                    break;
                }
            }

            break;

        case OID_GEN_CURRENT_PACKET_FILTER:

            if (InformationBufferLength < sizeof(ULONG)) {
                Status = NDIS_STATUS_INVALID_LENGTH;
                break;
            }

            ULONG PacketFilter = *(UNALIGNED ULONG *)InformationBuffer;
            Adapter->CurrentPacketFilter = PacketFilter;
            *BytesRead = InformationBufferLength;

            break;

        case OID_GEN_CURRENT_LOOKAHEAD:

            if (InformationBufferLength < sizeof(ULONG)) {
                Status = NDIS_STATUS_INVALID_LENGTH;
                break;
            }

            ULONG CurrentLookahead = *(UNALIGNED ULONG *)InformationBuffer;
            if (CurrentLookahead > Adapter->MtuSize) {
                Status = NDIS_STATUS_INVALID_LENGTH;
            } else if (CurrentLookahead >= Adapter->CurrentLookAhead) {
                Adapter->CurrentLookAhead = CurrentLookahead;
                *BytesRead = sizeof(ULONG);
            }

            break;

        case OID_802_3_MULTICAST_LIST:

            if (MpGlobalContext.Medium != NdisMedium802_3) {
                Status = NDIS_STATUS_INVALID_OID;
                break;
            }

            if ((InformationBufferLength % ETH_LENGTH_OF_ADDRESS) != 0 ||
                InformationBufferLength  > (MAX_MULTICAST_ADDRESSES * MAC_ADDR_LEN)) {
                Status = NDIS_STATUS_INVALID_LENGTH;
                break;
            }

            NdisMoveMemory(Adapter->MulticastAddressList,
                        InformationBuffer,
                        InformationBufferLength);
            Adapter->NumMulticastAddresses = InformationBufferLength / MAC_ADDR_LEN;

            break;

        case OID_PNP_SET_POWER:

            if (InformationBufferLength < sizeof(NDIS_DEVICE_POWER_STATE)) {
                Status = NDIS_STATUS_INVALID_LENGTH;
                break;
            }

            *BytesRead = sizeof(NDIS_DEVICE_POWER_STATE);

            break;

        case OID_GEN_RECEIVE_SCALE_PARAMETERS:

            if (InformationBufferLength < NDIS_SIZEOF_RECEIVE_SCALE_PARAMETERS_REVISION_2) {
                Status = NDIS_STATUS_INVALID_LENGTH;
                break;
            }

            MpSetRss(Adapter, InformationBuffer, InformationBufferLength);
            Status = NDIS_STATUS_SUCCESS;

            break;

        case OID_TCP_OFFLOAD_PARAMETERS:
            Status =
                MpSetOffloadParameters(
                    Adapter, &Adapter->OffloadConfig, InformationBuffer, InformationBufferLength,
                    NDIS_STATUS_TASK_OFFLOAD_CURRENT_CONFIG);
            break;

        case OID_TCP_OFFLOAD_HW_PARAMETERS:
            Status =
                MpSetOffloadParameters(
                    Adapter, &Adapter->OffloadCapabilities, InformationBuffer,
                    InformationBufferLength, NDIS_STATUS_TASK_OFFLOAD_HARDWARE_CAPABILITIES);

            break;

        default:

            Status = NDIS_STATUS_NOT_SUPPORTED;
            break;

    }

    return Status;
}

static
NDIS_STATUS
MpProcessMethodOid(
    _In_ ADAPTER_CONTEXT *Adapter,
    _In_ OID_REQUEST_INTERFACE RequestInterface,
    _Inout_ NDIS_OID_REQUEST *NdisRequest,
    _In_ BOOLEAN ShouldFilter
    )
{
    NDIS_STATUS Status;
    NDIS_OID Oid = NdisRequest->DATA.METHOD_INFORMATION.Oid;
    VOID *DataBuffer = NdisRequest->DATA.METHOD_INFORMATION.InformationBuffer;
    UINT32 InputBufferLength = NdisRequest->DATA.METHOD_INFORMATION.InputBufferLength;
    UINT32 OutputBufferLength = NdisRequest->DATA.METHOD_INFORMATION.OutputBufferLength;
    PUINT BytesRead = &NdisRequest->DATA.METHOD_INFORMATION.BytesRead;
    PUINT BytesWritten = &NdisRequest->DATA.METHOD_INFORMATION.BytesWritten;

    if (ShouldFilter) {
        Status = MpFilterOid(Adapter, RequestInterface, NdisRequest);
        if (Status != NDIS_STATUS_SUCCESS) {
            return Status;
        }
    }

    switch (Oid) {
    case OID_QUIC_CONNECTION_ENCRYPTION:
    case OID_QUIC_CONNECTION_ENCRYPTION_PROTOTYPE:
    {
        NDIS_QUIC_CONNECTION *Connection = DataBuffer;

        if (InputBufferLength == 0 ||
            InputBufferLength % sizeof(*Connection) != 0 ||
            InputBufferLength != OutputBufferLength) {
            Status = NDIS_STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        while (InputBufferLength > 0) {
            Connection->Status = NDIS_STATUS_SUCCESS;
            Connection++;
            InputBufferLength -= sizeof(*Connection);
        }

        *BytesRead = InputBufferLength;
        *BytesWritten = OutputBufferLength;
        Status = NDIS_STATUS_SUCCESS;
        break;
    }

    default:
        Status = STATUS_NOT_SUPPORTED;
        break;
    }

Exit:

    return Status;
}

VOID
MiniportCancelRequestHandler(
   _In_ NDIS_HANDLE MiniportAdapterContext,
   _In_ VOID *RequestId
   )
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(RequestId);
}

_IRQL_requires_(PASSIVE_LEVEL)
_Function_class_(MINIPORT_OID_REQUEST)
NDIS_STATUS
MiniportRequestHandler(
   _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_OID_REQUEST *NdisRequest
   )
{
    ADAPTER_CONTEXT *Adapter = (ADAPTER_CONTEXT *)MiniportAdapterContext;
    NDIS_STATUS Status;

    TraceEnter(
        TRACE_CONTROL, "Adapter=%p Oid=%u RequestType=%u PortNumber=%u",
        Adapter, NdisRequest->DATA.Oid, NdisRequest->RequestType, NdisRequest->PortNumber);

    switch (NdisRequest->RequestType)
    {
        case NdisRequestQueryInformation:
        case NdisRequestQueryStatistics:
            Status =
                MpProcessQueryOid(Adapter, OID_REQUEST_INTERFACE_REGULAR, NdisRequest, TRUE);
            break;
        case NdisRequestSetInformation:
            Status =
                MpProcessSetOid(Adapter, OID_REQUEST_INTERFACE_REGULAR, NdisRequest, TRUE);
            break;
        case NdisRequestMethod:
            Status =
                MpProcessMethodOid(Adapter, OID_REQUEST_INTERFACE_REGULAR, NdisRequest, TRUE);
            break;
        default:
            Status = NDIS_STATUS_NOT_SUPPORTED;
            break;
    }

    TraceExitStatus(TRACE_CONTROL);

    return Status;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(MINIPORT_CANCEL_DIRECT_OID_REQUEST)
VOID
MiniportCancelDirectRequestHandler(
   _In_ NDIS_HANDLE MiniportAdapterContext,
   _In_ VOID *RequestId
   )
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(RequestId);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(MINIPORT_DIRECT_OID_REQUEST)
NDIS_STATUS
MiniportDirectRequestHandler(
   _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_OID_REQUEST *NdisRequest
   )
{
    ADAPTER_CONTEXT *Adapter = (ADAPTER_CONTEXT *)MiniportAdapterContext;
    NDIS_STATUS Status;

    TraceEnter(
        TRACE_CONTROL, "Adapter=%p Oid=%u RequestType=%u",
        Adapter, NdisRequest->DATA.Oid, NdisRequest->RequestType);

    switch (NdisRequest->RequestType)
    {
        case NdisRequestQueryInformation:
        case NdisRequestQueryStatistics:
            Status =
                MpProcessQueryOid(Adapter, OID_REQUEST_INTERFACE_DIRECT, NdisRequest, TRUE);
            break;
        case NdisRequestSetInformation:
            Status =
                MpProcessSetOid(Adapter, OID_REQUEST_INTERFACE_DIRECT, NdisRequest, TRUE);
            break;
        case NdisRequestMethod:
            Status =
                MpProcessMethodOid(Adapter, OID_REQUEST_INTERFACE_DIRECT, NdisRequest, TRUE);
            break;
        default:
            Status = NDIS_STATUS_NOT_SUPPORTED;
            break;
    }

    TraceExitStatus(TRACE_CONTROL);

    return Status;
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
MpOidCompleteRequest(
    _In_ ADAPTER_CONTEXT *Adapter,
    _In_ OID_REQUEST_INTERFACE RequestInterface,
    _In_ NDIS_STATUS Status,
    _In_ NDIS_OID_REQUEST *Request
    )
{
    TraceEnter(
        TRACE_CONTROL, "Adapter=%p Oid=%u RequestType=%u Status=%!STATUS!",
        Adapter, Request->DATA.Oid, Request->RequestType, Status);

    //
    // The caller can specify either a valid NDIS OID completion status or
    // NDIS_STATUS_PENDING. All valid completion statuses bypass the miniport
    // OID processing routines; the pending status causes regular processing to
    // continue.
    //
    if (Status != NDIS_STATUS_PENDING) {
        goto Exit;
    }

    switch (Request->RequestType)
    {
        case NdisRequestQueryInformation:
        case NdisRequestQueryStatistics:
            Status = MpProcessQueryOid(Adapter, RequestInterface, Request, FALSE);
            break;
        case NdisRequestSetInformation:
            Status = MpProcessSetOid(Adapter, RequestInterface, Request, FALSE);
            break;
        case NdisRequestMethod:
            Status = MpProcessMethodOid(Adapter, RequestInterface, Request, FALSE);
            break;
        default:
            Status = NDIS_STATUS_NOT_SUPPORTED;
            break;
    }

Exit:

    switch (RequestInterface) {
    case OID_REQUEST_INTERFACE_REGULAR:
        NdisMOidRequestComplete(Adapter->MiniportHandle, Request, Status);
        break;

    case OID_REQUEST_INTERFACE_DIRECT:
        NdisMDirectOidRequestComplete(Adapter->MiniportHandle, Request, Status);
        break;

    default:
        FRE_ASSERT(FALSE);
        break;
    }

    TraceExitStatus(TRACE_CONTROL);
}

static
_Requires_lock_held_(Adapter->Lock)
VOID
MpOidClearFilter(
    _In_ ADAPTER_CONTEXT *Adapter
    )
{
    if (Adapter->OidFilterKeys != NULL) {
        #pragma warning(suppress:4090) // freeing const pointer.
        ExFreePoolWithTag(Adapter->OidFilterKeys, POOLTAG_MP_OID);
        Adapter->OidFilterKeys = NULL;
        Adapter->OidFilterKeyCount = 0;
    }
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
MpOidClearFilterAndFlush(
    _In_ ADAPTER_CONTEXT *Adapter
    )
{
    KIRQL OldIrql;
    LIST_ENTRY RequestLists[OID_REQUEST_INTERFACE_MAX];

    //
    // Atomically clear the OID filter conditions and complete any pended OIDs.
    //

    KeAcquireSpinLock(&Adapter->Lock, &OldIrql);

    MpOidClearFilter(Adapter);

    for (OID_REQUEST_INTERFACE Interface = 0; Interface < OID_REQUEST_INTERFACE_MAX; Interface++) {
        InitializeListHead(&RequestLists[Interface]);
        AppendTailList(&RequestLists[Interface], &Adapter->FilteredOidRequestLists[Interface]);
        RemoveEntryList(&Adapter->FilteredOidRequestLists[Interface]);
        InitializeListHead(&Adapter->FilteredOidRequestLists[Interface]);
    }

    KeReleaseSpinLock(&Adapter->Lock, OldIrql);

    for (OID_REQUEST_INTERFACE Interface = 0; Interface < OID_REQUEST_INTERFACE_MAX; Interface++) {
        while (!IsListEmpty(&RequestLists[Interface])) {
            LIST_ENTRY *ListEntry = RemoveHeadList(&RequestLists[Interface]);
            FN_OID_REQUEST_ENTRY *RequestEntry = MpListEntryToOidRequestEntry(ListEntry);

            MpOidCompleteRequest(
                Adapter, Interface, NDIS_STATUS_PENDING, RequestEntry->NdisRequest);
            MpFreeOidRequestEntry(RequestEntry);
        }
    }
}

_IRQL_requires_max_(DISPATCH_LEVEL)
BOOLEAN
MpOidWatchdogIsExpired(
    _In_ ADAPTER_CONTEXT *Adapter
    )
{
    static const ULONGLONG WatchdogGracePeriod = RTL_SEC_TO_100NANOSEC(5);
    BOOLEAN Expired = FALSE;
    KIRQL OldIrql;
    ULONGLONG CurrentTime = KeQueryUnbiasedInterruptTime();

    KeAcquireSpinLock(&Adapter->Lock, &OldIrql);

    for (OID_REQUEST_INTERFACE Interface = 0; Interface < OID_REQUEST_INTERFACE_MAX; Interface++) {
        for (LIST_ENTRY *Entry = Adapter->FilteredOidRequestLists[Interface].Flink;
            Entry != &Adapter->FilteredOidRequestLists[Interface];
            Entry = Entry->Flink) {
            FN_OID_REQUEST_ENTRY *RequestEntry = MpListEntryToOidRequestEntry(Entry);

            if (CurrentTime > RequestEntry->Timestamp + WatchdogGracePeriod) {
                Expired = TRUE;
                break;
            }
        }
    }

    KeReleaseSpinLock(&Adapter->Lock, OldIrql);

    return Expired;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
MpIrpOidSetFilter(
    _In_ EXCLUSIVE_USER_CONTEXT *UserContext,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    ADAPTER_CONTEXT *Adapter = UserContext->Adapter;
    UINT32 InKeySize;
    const VOID *InKeys;
    OID_KEY *OidFilterKeys = NULL;
    UINT32 KeyCount;
    KIRQL OldIrql = PASSIVE_LEVEL;
    BOOLEAN IsLockHeld = FALSE;

    if (UserContext->Header.ApiVersion >= MP_APIVER(2)) {
        InKeySize = sizeof(OID_KEY);
    } else {
        InKeySize = sizeof(OID_KEY_V0);
    }

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < InKeySize) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength % InKeySize != 0) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    InKeys = Irp->AssociatedIrp.SystemBuffer;
    KeyCount = IrpSp->Parameters.DeviceIoControl.InputBufferLength / InKeySize;

    OidFilterKeys =
        ExAllocatePoolZero(
            NonPagedPoolNx, KeyCount * sizeof(*Adapter->OidFilterKeys), POOLTAG_MP_OID);
    if (OidFilterKeys == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    for (UINT32 Index = 0; Index < KeyCount; Index++) {
        #pragma warning(suppress:26451) // casting 4-byte multiplication to 8-byte type
        const VOID *InAnyKey = RTL_PTR_ADD(InKeys, Index * InKeySize);
        OID_KEY *FilterKey = &OidFilterKeys[Index];

        if (UserContext->Header.ApiVersion >= MP_APIVER(2)) {
            const OID_KEY *InKey = InAnyKey;
            *FilterKey = *InKey;
        } else {
            const OID_KEY_V0 *InKey = InAnyKey;
            FilterKey->Oid = InKey->Oid;
            FilterKey->RequestType = InKey->RequestType;
            FilterKey->RequestInterface = InKey->RequestInterface;
        }

        if (FilterKey->RequestType != NdisRequestQueryInformation &&
            FilterKey->RequestType != NdisRequestSetInformation &&
            FilterKey->RequestType != NdisRequestMethod) {
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        if ((UINT32)FilterKey->RequestInterface >= (UINT32)OID_REQUEST_INTERFACE_MAX) {
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }
    }

    KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
    IsLockHeld = TRUE;

    if (Adapter->OidFilterKeys != NULL) {
        //
        // Currently support setting a filter only once, for simplicity.
        //
        Status = STATUS_INVALID_DEVICE_REQUEST;
        goto Exit;
    }

    Adapter->OidFilterKeys = OidFilterKeys;
    OidFilterKeys = NULL;
    Adapter->OidFilterKeyCount = KeyCount;
    Adapter->UserContext->SetOidFilter = TRUE;
    Status = STATUS_SUCCESS;

Exit:

    if (IsLockHeld) {
        KeReleaseSpinLock(&Adapter->Lock, OldIrql);
    }

    if (OidFilterKeys != NULL) {
        ExFreePoolWithTag(OidFilterKeys, POOLTAG_MP_OID);
    }

    return Status;
}

static
FN_OID_REQUEST_ENTRY *
MpOidFindFilteredOidByKey(
    _In_ ADAPTER_CONTEXT *Adapter,
    _In_ const OID_KEY *Key
    )
{
    FN_OID_REQUEST_ENTRY *RequestEntry;
    NDIS_OID_REQUEST *Request;

    if (IsListEmpty(&Adapter->FilteredOidRequestLists[Key->RequestInterface])) {
        return NULL;
    }

    RequestEntry =
        MpListEntryToOidRequestEntry(Adapter->FilteredOidRequestLists[Key->RequestInterface].Flink);
    Request = RequestEntry->NdisRequest;

    if (Request->DATA.Oid != Key->Oid ||
        Request->RequestType != Key->RequestType ||
        Request->PortNumber != Key->PortNumber) {
        return NULL;
    }

    return RequestEntry;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
MpIrpOidGetRequest(
    _In_ EXCLUSIVE_USER_CONTEXT *UserContext,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    ADAPTER_CONTEXT *Adapter = UserContext->Adapter;
    FN_OID_REQUEST_ENTRY *RequestEntry;
    NDIS_OID_REQUEST *Request;
    OID_KEY Key = {0};
    VOID *InformationBuffer;
    UINT32 InformationBufferLength;
    KIRQL OldIrql = PASSIVE_LEVEL;
    BOOLEAN IsLockHeld = FALSE;
    UINT32 OutputBufferLength =
        IrpSp->Parameters.DeviceIoControl.OutputBufferLength;
    SIZE_T *BytesReturned = &Irp->IoStatus.Information;

    *BytesReturned = 0;

    if (UserContext->Header.ApiVersion >= MP_APIVER(2)) {
        const OID_KEY *InKey = Irp->AssociatedIrp.SystemBuffer;
        if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(*InKey)) {
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }
        Key = *InKey;
    } else {
        const OID_KEY_V0 *InKey = Irp->AssociatedIrp.SystemBuffer;
        if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(*InKey)) {
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }
        Key.Oid = InKey->Oid;
        Key.RequestType = InKey->RequestType;
        Key.RequestInterface = InKey->RequestInterface;
    }

    if ((UINT32)Key.RequestInterface >= (UINT32)OID_REQUEST_INTERFACE_MAX) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
    IsLockHeld = TRUE;

    RequestEntry = MpOidFindFilteredOidByKey(Adapter, &Key);
    if (RequestEntry == NULL) {
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

    Request = RequestEntry->NdisRequest;

    if (Request->RequestType == NdisRequestQueryInformation) {
        InformationBuffer = Request->DATA.QUERY_INFORMATION.InformationBuffer;
        InformationBufferLength = Request->DATA.QUERY_INFORMATION.InformationBufferLength;
    } else if (Request->RequestType == NdisRequestSetInformation) {
        InformationBuffer = Request->DATA.SET_INFORMATION.InformationBuffer;
        InformationBufferLength = Request->DATA.SET_INFORMATION.InformationBufferLength;
    } else if (Request->RequestType == NdisRequestMethod) {
        InformationBuffer = Request->DATA.METHOD_INFORMATION.InformationBuffer;
        InformationBufferLength = Request->DATA.METHOD_INFORMATION.InputBufferLength;
    } else {
        //
        // We only should have filtered get/set/method requests.
        //
        ASSERT(FALSE);
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    if ((OutputBufferLength == 0) && (Irp->Flags & IRP_INPUT_OPERATION) == 0) {
        *BytesReturned = InformationBufferLength;
        Status = STATUS_BUFFER_OVERFLOW;
        goto Exit;
    }

    if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < InformationBufferLength) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer, InformationBuffer, InformationBufferLength);
    Status = STATUS_SUCCESS;
    *BytesReturned = InformationBufferLength;

Exit:

    if (IsLockHeld) {
        KeReleaseSpinLock(&Adapter->Lock, OldIrql);
    }

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
MpIrpOidCompleteRequest(
    _In_ EXCLUSIVE_USER_CONTEXT *UserContext,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    ADAPTER_CONTEXT *Adapter = UserContext->Adapter;
    FN_OID_REQUEST_ENTRY *RequestEntry = NULL;
    NDIS_OID_REQUEST *Request = NULL;
    OID_KEY Key = {0};
    OID_COMPLETE_REQUEST_IN In = {0};
    BOUNCE_BUFFER KeyBuffer;
    BOUNCE_BUFFER InfoBuffer;
    VOID *InformationBuffer = NULL;
    UINT32 InformationBufferLength = 0;
    UINT *BytesWritten = NULL;
    UINT *BytesRead = NULL;
    KIRQL OldIrql = PASSIVE_LEVEL;
    BOOLEAN IsLockHeld = FALSE;

    BounceInitialize(&KeyBuffer);
    BounceInitialize(&InfoBuffer);

    if (UserContext->Header.ApiVersion >= MP_APIVER(2)) {
        const OID_COMPLETE_REQUEST_IN *InRequest = Irp->AssociatedIrp.SystemBuffer;

        if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(*InRequest)) {
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        In = *InRequest;

        Status =
            BounceBuffer(
                &KeyBuffer, Irp->RequestorMode, In.Key, sizeof(OID_KEY), __alignof(OID_KEY));
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }

        Key = *(const OID_KEY *)KeyBuffer.Buffer;
    } else {
        const OID_COMPLETE_REQUEST_IN_V0 *InRequest = Irp->AssociatedIrp.SystemBuffer;

        if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(*InRequest)) {
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        In.InformationBuffer = InRequest->InformationBuffer;
        In.InformationBufferLength = InRequest->InformationBufferLength;
        In.Status = InRequest->Status;
        Key.Oid = InRequest->Key.Oid;
        Key.RequestType = InRequest->Key.RequestType;
        Key.RequestInterface = InRequest->Key.RequestInterface;
    }

    In.Key = &Key;

    if ((UINT32)In.Key->RequestInterface >= (UINT32)OID_REQUEST_INTERFACE_MAX) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    if (In.Status == NDIS_STATUS_PENDING && In.InformationBufferLength > 0) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    if (In.Key->RequestType != NdisRequestSetInformation) {
        Status =
            BounceBuffer(
                &InfoBuffer, Irp->RequestorMode, In.InformationBuffer, In.InformationBufferLength,
                __alignof(UCHAR));
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }
    }

    KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
    IsLockHeld = TRUE;

    RequestEntry = MpOidFindFilteredOidByKey(Adapter, In.Key);
    if (RequestEntry == NULL) {
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

    Request = RequestEntry->NdisRequest;

    ASSERT(In.Key->RequestType == Request->RequestType);

    if (Request->RequestType == NdisRequestQueryInformation) {
        InformationBuffer = Request->DATA.QUERY_INFORMATION.InformationBuffer;
        InformationBufferLength = Request->DATA.QUERY_INFORMATION.InformationBufferLength;
        BytesWritten = &Request->DATA.QUERY_INFORMATION.BytesWritten;
    } else if (Request->RequestType == NdisRequestSetInformation) {
        InformationBuffer = Request->DATA.SET_INFORMATION.InformationBuffer;
        InformationBufferLength = Request->DATA.SET_INFORMATION.InformationBufferLength;
        BytesRead = &Request->DATA.SET_INFORMATION.BytesRead;
    } else if (Request->RequestType == NdisRequestMethod) {
        InformationBuffer = Request->DATA.METHOD_INFORMATION.InformationBuffer;
        InformationBufferLength = Request->DATA.METHOD_INFORMATION.OutputBufferLength;
        BytesWritten = &Request->DATA.METHOD_INFORMATION.BytesWritten;
    } else {
        //
        // We only should have filtered get/set/method requests.
        //
        ASSERT(FALSE);
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    if (InformationBufferLength < In.InformationBufferLength) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    RemoveEntryList(&RequestEntry->Entry);

    Status = STATUS_SUCCESS;

Exit:

    if (IsLockHeld) {
        KeReleaseSpinLock(&Adapter->Lock, OldIrql);
    }

    if (NT_SUCCESS(Status)) {
        ASSERT(RequestEntry != NULL);
        ASSERT(Request != NULL);

        if (In.InformationBufferLength > 0) {
            ASSERT(In.Status != NDIS_STATUS_PENDING);

            if (BytesWritten != NULL) {
                RtlCopyMemory(InformationBuffer, InfoBuffer.Buffer, In.InformationBufferLength);
                *BytesWritten = In.InformationBufferLength;
            }

            if (BytesRead != NULL) {
                *BytesRead = In.InformationBufferLength;
            }
        }

        MpOidCompleteRequest(Adapter, In.Key->RequestInterface, In.Status, Request);
        MpFreeOidRequestEntry(RequestEntry);
    }

    BounceCleanup(&InfoBuffer);
    BounceCleanup(&KeyBuffer);

    return Status;
}
