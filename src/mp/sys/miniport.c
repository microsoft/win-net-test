//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "miniport.tmh"

NDIS_STRING RegRSS = NDIS_STRING_CONST("*RSS");
NDIS_STRING RegNumRssQueues = NDIS_STRING_CONST("*NumRssQueues");

UCHAR MpMacAddressBase[MAC_ADDR_LEN] = {0x22, 0x22, 0x22, 0x22, 0x00, 0x00};

GLOBAL_CONTEXT MpGlobalContext = {0};

typedef struct _OFFLOAD_REGKEY {
    NDIS_STRING CurrentConfigName;
    NDIS_STRING HardwareCapabilityName;
} OFFLOAD_REGKEY;

#define DEFINE_OFFLOAD_REGISTRY_REGKEY(_RegKeyName) \
    static const OFFLOAD_REGKEY _RegKeyName = { \
        .CurrentConfigName = NDIS_STRING_CONST(#_RegKeyName), \
        .HardwareCapabilityName = NDIS_STRING_CONST(#_RegKeyName "Capability") \
    }

DEFINE_OFFLOAD_REGISTRY_REGKEY(IPChecksumOffloadIPv4);
DEFINE_OFFLOAD_REGISTRY_REGKEY(TCPChecksumOffloadIPv4);
DEFINE_OFFLOAD_REGISTRY_REGKEY(TCPChecksumOffloadIPv6);
DEFINE_OFFLOAD_REGISTRY_REGKEY(UDPChecksumOffloadIPv4);
DEFINE_OFFLOAD_REGISTRY_REGKEY(UDPChecksumOffloadIPv6);
DEFINE_OFFLOAD_REGISTRY_REGKEY(LsoV2IPv4);
DEFINE_OFFLOAD_REGISTRY_REGKEY(LsoV2IPv6);
DEFINE_OFFLOAD_REGISTRY_REGKEY(UsoIPv4);
DEFINE_OFFLOAD_REGISTRY_REGKEY(UsoIPv6);
DEFINE_OFFLOAD_REGISTRY_REGKEY(RscIPv4);
DEFINE_OFFLOAD_REGISTRY_REGKEY(RscIPv6);
DEFINE_OFFLOAD_REGISTRY_REGKEY(UdpRsc);

static FN_WATCHDOG_CALLBACK MpWatchdog;

static
const NDIS_STRING *
GetOffloadRegKeyName(
    _In_ const OFFLOAD_REGKEY *RegKey,
    _In_ FN_OFFLOAD_TYPE Store
    )
{
    switch (Store) {
    case FnOffloadCurrentConfig:
        return &RegKey->CurrentConfigName;
    case FnOffloadHardwareCapabilities:
        return &RegKey->HardwareCapabilityName;
    default:
        FRE_ASSERT(FALSE);
    }
}

static
VOID
MpCleanupAdapter(
   _Inout_ ADAPTER_CONTEXT *Adapter
   )
{
    if (Adapter->Watchdog != NULL) {
        FnWatchdogClose(Adapter->Watchdog);
        Adapter->Watchdog = NULL;
    }

    MpCleanupRssQueues(Adapter);

    if (Adapter->Shared != NULL) {
        SharedAdapterCleanup(Adapter->Shared);
    }

    MpIoctlDereference();
    TraceInfo(TRACE_CONTROL, "Adapter=%p freed", Adapter);
    ExFreePool(Adapter);
}

static
ADAPTER_CONTEXT *
MpCreateAdapter(
   _In_ NDIS_HANDLE NdisMiniportHandle,
   _In_ UINT32 IfIndex
   )
{
    ADAPTER_CONTEXT *Adapter = NULL;
    NTSTATUS Status;

    if (MpIoctlReference() != NDIS_STATUS_SUCCESS) {
        Status = STATUS_UNSUCCESSFUL;
        goto Exit;
    }

    Adapter = ExAllocatePoolZero(NonPagedPoolNx, sizeof(ADAPTER_CONTEXT), POOLTAG_MP_ADAPTER);
    if (Adapter == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Adapter->MiniportHandle = NdisMiniportHandle;
    Adapter->IfIndex = IfIndex;
    Adapter->ReferenceCount = 1;
    InitializeListHead(&Adapter->AdapterListLink);
    KeInitializeSpinLock(&Adapter->Lock);
    ExInitializePushLock(&Adapter->PushLock);

    for (UINT32 i = 0; i < RTL_NUMBER_OF(Adapter->FilteredOidRequestLists); i++) {
        InitializeListHead(&Adapter->FilteredOidRequestLists[i]);
    }

    Adapter->Shared = SharedAdapterCreate(Adapter);
    if (Adapter->Shared == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Status = FnWatchdogCreate(&Adapter->Watchdog, MpWatchdog, Adapter, RTL_SEC_TO_MILLISEC(1));
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        if (Adapter != NULL) {
            MpCleanupAdapter(Adapter);
            Adapter = NULL;
        }
    }

    return Adapter;
}

static
VOID
MpReferenceAdapter(
    _In_ ADAPTER_CONTEXT *Adapter
    )
{
    FRE_ASSERT(InterlockedIncrementAcquire64(&Adapter->ReferenceCount) > 1);
}

VOID
MpDereferenceAdapter(
    _In_ ADAPTER_CONTEXT *Adapter
    )
{
    INT64 NewCount = InterlockedDecrementRelease64(&Adapter->ReferenceCount);
    FRE_ASSERT(NewCount >= 0);
    if (NewCount == 0) {
        MpCleanupAdapter(Adapter);
    }
}

ADAPTER_CONTEXT *
MpFindAdapter(
    _In_ UINT32 IfIndex
    )
{
    LIST_ENTRY *Entry;
    ADAPTER_CONTEXT *Adapter = NULL;

    RtlAcquirePushLockShared(&MpGlobalContext.Lock);

    Entry = MpGlobalContext.AdapterList.Flink;
    while (Entry != &MpGlobalContext.AdapterList) {
        ADAPTER_CONTEXT *Candidate = CONTAINING_RECORD(Entry, ADAPTER_CONTEXT, AdapterListLink);
        Entry = Entry->Flink;

        if (Candidate->IfIndex == IfIndex) {
            Adapter = Candidate;
            MpReferenceAdapter(Adapter);
            break;
        }
    }

    RtlReleasePushLockShared(&MpGlobalContext.Lock);

    return Adapter;
}

ADAPTER_OFFLOAD *
MpGetOffload(
    _In_ ADAPTER_CONTEXT *Adapter,
    _In_ FN_OFFLOAD_TYPE Store
    )
{
    switch (Store) {
    case FnOffloadCurrentConfig:
        return &Adapter->OffloadConfig;
    case FnOffloadHardwareCapabilities:
        return &Adapter->OffloadCapabilities;
    default:
        FRE_ASSERT(FALSE);
    }
}

NDIS_STATUS
MpReadOffload(
    _Inout_ ADAPTER_CONTEXT *Adapter,
    _In_ NDIS_HANDLE ConfigHandle,
    _In_ FN_OFFLOAD_TYPE Store
    )
{
    NDIS_STATUS Status;
    ADAPTER_OFFLOAD *Offload = MpGetOffload(Adapter, Store);

    RtlAcquirePushLockExclusive(&Adapter->PushLock);

    TRY_READ_INT_CONFIGURATION(
        ConfigHandle, GetOffloadRegKeyName(&IPChecksumOffloadIPv4, Store),
        &Offload->IPChecksumOffloadIPv4);
    if ((UINT32)Offload->IPChecksumOffloadIPv4 > ChecksumOffloadRxTx) {
        Status = NDIS_STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    TRY_READ_INT_CONFIGURATION(
        ConfigHandle, GetOffloadRegKeyName(&TCPChecksumOffloadIPv4, Store),
        &Offload->TCPChecksumOffloadIPv4);
    if ((UINT32)Offload->TCPChecksumOffloadIPv4 > ChecksumOffloadRxTx) {
        Status = NDIS_STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    TRY_READ_INT_CONFIGURATION(
        ConfigHandle, GetOffloadRegKeyName(&TCPChecksumOffloadIPv6, Store),
        &Offload->TCPChecksumOffloadIPv6);
    if ((UINT32)Offload->TCPChecksumOffloadIPv6 > ChecksumOffloadRxTx) {
        Status = NDIS_STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    TRY_READ_INT_CONFIGURATION(
        ConfigHandle, GetOffloadRegKeyName(&UDPChecksumOffloadIPv4, Store),
        &Offload->UDPChecksumOffloadIPv4);
    if ((UINT32)Offload->UDPChecksumOffloadIPv4 > ChecksumOffloadRxTx) {
        Status = NDIS_STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    TRY_READ_INT_CONFIGURATION(
        ConfigHandle, GetOffloadRegKeyName(&UDPChecksumOffloadIPv6, Store),
        &Offload->UDPChecksumOffloadIPv6);
    if ((UINT32)Offload->UDPChecksumOffloadIPv6 > ChecksumOffloadRxTx) {
        Status = NDIS_STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    TRY_READ_INT_CONFIGURATION(
        ConfigHandle, GetOffloadRegKeyName(&LsoV2IPv4, Store), &Offload->LsoV2IPv4);
    if ((UINT32)Offload->LsoV2IPv4 > TRUE) {
        Status = NDIS_STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    TRY_READ_INT_CONFIGURATION(
        ConfigHandle, GetOffloadRegKeyName(&LsoV2IPv6, Store), &Offload->LsoV2IPv6);
    if ((UINT32)Offload->LsoV2IPv6 > TRUE) {
        Status = NDIS_STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    TRY_READ_INT_CONFIGURATION(
        ConfigHandle, GetOffloadRegKeyName(&UsoIPv4, Store), &Offload->UsoIPv4);
    if ((UINT32)Offload->UsoIPv4 > TRUE) {
        Status = NDIS_STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    TRY_READ_INT_CONFIGURATION(
        ConfigHandle, GetOffloadRegKeyName(&UsoIPv6, Store), &Offload->UsoIPv6);
    if ((UINT32)Offload->UsoIPv6 > TRUE) {
        Status = NDIS_STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    TRY_READ_INT_CONFIGURATION(
        ConfigHandle, GetOffloadRegKeyName(&RscIPv4, Store), &Offload->RscIPv4);
    if ((UINT32)Offload->RscIPv4 > TRUE) {
        Status = NDIS_STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    TRY_READ_INT_CONFIGURATION(
        ConfigHandle, GetOffloadRegKeyName(&RscIPv6, Store), &Offload->RscIPv6);
    if ((UINT32)Offload->RscIPv6 > TRUE) {
        Status = NDIS_STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    TRY_READ_INT_CONFIGURATION(
        ConfigHandle, GetOffloadRegKeyName(&UdpRsc, Store), &Offload->UdpRsc);
    if ((UINT32)Offload->UdpRsc > TRUE) {
        Status = NDIS_STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    Status = NDIS_STATUS_SUCCESS;

Exit:

    if (Status != NDIS_STATUS_SUCCESS) {
        RtlZeroMemory(Offload, sizeof(*Offload));
    }

    RtlReleasePushLockExclusive(&Adapter->PushLock);

    return Status;
}

NDIS_STATUS
MpOpenConfiguration(
    _Out_ NDIS_HANDLE *ConfigHandle,
    _In_ ADAPTER_CONTEXT *Adapter
    )
{
    NDIS_CONFIGURATION_OBJECT ConfigObject = {0};
    NDIS_STATUS Status;

    *ConfigHandle = NULL;

    ConfigObject.Header.Type = NDIS_OBJECT_TYPE_CONFIGURATION_OBJECT;
    ConfigObject.Header.Revision = NDIS_CONFIGURATION_OBJECT_REVISION_1;
    ConfigObject.Header.Size = sizeof(NDIS_CONFIGURATION_OBJECT);
    ConfigObject.NdisHandle = Adapter->MiniportHandle;
    ConfigObject.Flags = 0;

    Status = NdisOpenConfigurationEx(&ConfigObject, ConfigHandle);
    if (Status != NDIS_STATUS_SUCCESS) {
        goto Exit;
    }

Exit:

    return Status;
}

static
VOID
MpInitializeOffload(
    _Inout_ ADAPTER_OFFLOAD *Offload
    )
{
    Offload->GsoMaxOffloadSize = FNMP_DEFAULT_MAX_GSO_SIZE;
}

static
NDIS_STATUS
MpReadConfiguration(
   _Inout_ ADAPTER_CONTEXT *Adapter
   )
{
    NDIS_HANDLE ConfigHandle = NULL;
    NDIS_STATUS Status;

    Status = MpOpenConfiguration(&ConfigHandle, Adapter);
    if (Status != NDIS_STATUS_SUCCESS) {
        goto Exit;
    }

    Adapter->MtuSize = FNMP_DEFAULT_MTU - ETH_HDR_LEN;
    Adapter->CurrentLookAhead = 0;
    Adapter->CurrentPacketFilter = 0;
    MpInitializeOffload(&Adapter->OffloadCapabilities);
    MpInitializeOffload(&Adapter->OffloadConfig);

    Adapter->RssEnabled = 0;
    TRY_READ_INT_CONFIGURATION(ConfigHandle, &RegRSS, &Adapter->RssEnabled);

    Adapter->NumRssQueues = FNMP_DEFAULT_RSS_QUEUES;
    TRY_READ_INT_CONFIGURATION(ConfigHandle, &RegNumRssQueues, &Adapter->NumRssQueues);
    if (Adapter->NumRssQueues == 0 || Adapter->NumRssQueues > MAX_RSS_QUEUES) {
        Status = NDIS_STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    Status = MpReadOffload(Adapter, ConfigHandle, FnOffloadHardwareCapabilities);
    if (Status != NDIS_STATUS_SUCCESS) {
        goto Exit;
    }

    Status = MpReadOffload(Adapter, ConfigHandle, FnOffloadCurrentConfig);
    if (Status != NDIS_STATUS_SUCCESS) {
        goto Exit;
    }

    Status = NDIS_STATUS_SUCCESS;

Exit:

    if (ConfigHandle != NULL) {
        NdisCloseConfiguration(ConfigHandle);
    }

    return Status;
}

VOID
MpFillOffload(
    _Out_ NDIS_OFFLOAD *Offload,
    _In_ ADAPTER_CONTEXT *Adapter,
    _In_ ADAPTER_OFFLOAD *AdapterOffload
    )
{
    CONST UINT32 Encapsulation = NDIS_ENCAPSULATION_IEEE_802_3;

    RtlZeroMemory(Offload, sizeof(*Offload));

    Offload->Header.Type = NDIS_OBJECT_TYPE_OFFLOAD;
    Offload->Header.Revision = NDIS_OFFLOAD_REVISION_7;
    Offload->Header.Size = NDIS_SIZEOF_NDIS_OFFLOAD_REVISION_7;

    RtlAcquirePushLockShared(&Adapter->PushLock);

    if (AdapterOffload->IPChecksumOffloadIPv4 & ChecksumOffloadTx) {
        Offload->Checksum.IPv4Transmit.Encapsulation = Encapsulation;
        Offload->Checksum.IPv4Transmit.IpChecksum = NDIS_OFFLOAD_SUPPORTED;
        Offload->Checksum.IPv4Transmit.IpOptionsSupported = NDIS_OFFLOAD_SUPPORTED;
    }

    if (AdapterOffload->IPChecksumOffloadIPv4 & ChecksumOffloadRx) {
        Offload->Checksum.IPv4Receive.Encapsulation = Encapsulation;
        Offload->Checksum.IPv4Receive.IpChecksum = NDIS_OFFLOAD_SUPPORTED;
        Offload->Checksum.IPv4Receive.IpOptionsSupported = NDIS_OFFLOAD_SUPPORTED;
    }

    if (AdapterOffload->TCPChecksumOffloadIPv4 & ChecksumOffloadTx) {
        Offload->Checksum.IPv4Transmit.Encapsulation = Encapsulation;
        Offload->Checksum.IPv4Transmit.TcpChecksum = NDIS_OFFLOAD_SUPPORTED;
        Offload->Checksum.IPv4Transmit.TcpOptionsSupported = NDIS_OFFLOAD_SUPPORTED;
    }

    if (AdapterOffload->TCPChecksumOffloadIPv4 & ChecksumOffloadRx) {
        Offload->Checksum.IPv4Receive.Encapsulation = Encapsulation;
        Offload->Checksum.IPv4Receive.TcpChecksum = NDIS_OFFLOAD_SUPPORTED;
        Offload->Checksum.IPv4Receive.TcpOptionsSupported = NDIS_OFFLOAD_SUPPORTED;
    }

    if (AdapterOffload->TCPChecksumOffloadIPv6 & ChecksumOffloadTx) {
        Offload->Checksum.IPv6Transmit.Encapsulation = Encapsulation;
        Offload->Checksum.IPv6Transmit.TcpChecksum = NDIS_OFFLOAD_SUPPORTED;
        Offload->Checksum.IPv6Transmit.TcpOptionsSupported = NDIS_OFFLOAD_SUPPORTED;
    }

    if (AdapterOffload->TCPChecksumOffloadIPv6 & ChecksumOffloadRx) {
        Offload->Checksum.IPv6Receive.Encapsulation = Encapsulation;
        Offload->Checksum.IPv6Receive.TcpChecksum = NDIS_OFFLOAD_SUPPORTED;
        Offload->Checksum.IPv6Receive.TcpOptionsSupported = NDIS_OFFLOAD_SUPPORTED;
    }

    if (AdapterOffload->UDPChecksumOffloadIPv4 & ChecksumOffloadTx) {
        Offload->Checksum.IPv4Transmit.Encapsulation = Encapsulation;
        Offload->Checksum.IPv4Transmit.UdpChecksum = NDIS_OFFLOAD_SUPPORTED;
    }

    if (AdapterOffload->UDPChecksumOffloadIPv4 & ChecksumOffloadRx) {
        Offload->Checksum.IPv4Receive.Encapsulation = Encapsulation;
        Offload->Checksum.IPv4Receive.UdpChecksum = NDIS_OFFLOAD_SUPPORTED;
    }

    if (AdapterOffload->UDPChecksumOffloadIPv6 & ChecksumOffloadTx) {
        Offload->Checksum.IPv6Transmit.Encapsulation = Encapsulation;
        Offload->Checksum.IPv6Transmit.UdpChecksum = NDIS_OFFLOAD_SUPPORTED;
    }

    if (AdapterOffload->UDPChecksumOffloadIPv6 & ChecksumOffloadRx) {
        Offload->Checksum.IPv6Receive.Encapsulation = Encapsulation;
        Offload->Checksum.IPv6Receive.UdpChecksum = NDIS_OFFLOAD_SUPPORTED;
    }

    if (AdapterOffload->LsoV2IPv4) {
        Offload->LsoV2.IPv4.Encapsulation = Encapsulation;
        Offload->LsoV2.IPv4.MaxOffLoadSize = AdapterOffload->GsoMaxOffloadSize;
        Offload->LsoV2.IPv4.MinSegmentCount = FNMP_DEFAULT_MIN_GSO_SEG_COUNT;
    }

    if (AdapterOffload->LsoV2IPv6) {
        Offload->LsoV2.IPv6.Encapsulation = Encapsulation;
        Offload->LsoV2.IPv6.MaxOffLoadSize = AdapterOffload->GsoMaxOffloadSize;
        Offload->LsoV2.IPv6.MinSegmentCount = FNMP_DEFAULT_MIN_GSO_SEG_COUNT;
        Offload->LsoV2.IPv6.IpExtensionHeadersSupported = NDIS_OFFLOAD_SUPPORTED;
        Offload->LsoV2.IPv6.TcpOptionsSupported = NDIS_OFFLOAD_SUPPORTED;
    }

    if (AdapterOffload->UsoIPv4) {
        Offload->UdpSegmentation.IPv4.Encapsulation = Encapsulation;
        Offload->UdpSegmentation.IPv4.MaxOffLoadSize = AdapterOffload->GsoMaxOffloadSize;
        Offload->UdpSegmentation.IPv4.MinSegmentCount = FNMP_DEFAULT_MIN_GSO_SEG_COUNT;
        Offload->UdpSegmentation.IPv4.SubMssFinalSegmentSupported = NDIS_OFFLOAD_SUPPORTED;
    }

    if (AdapterOffload->UsoIPv6) {
        Offload->UdpSegmentation.IPv6.Encapsulation = Encapsulation;
        Offload->UdpSegmentation.IPv6.MaxOffLoadSize = AdapterOffload->GsoMaxOffloadSize;
        Offload->UdpSegmentation.IPv6.MinSegmentCount = FNMP_DEFAULT_MIN_GSO_SEG_COUNT;
        Offload->UdpSegmentation.IPv6.IpExtensionHeadersSupported = NDIS_OFFLOAD_SUPPORTED;
        Offload->UdpSegmentation.IPv6.SubMssFinalSegmentSupported = NDIS_OFFLOAD_SUPPORTED;
    }

    if (AdapterOffload->RscIPv4) {
        Offload->Rsc.IPv4.Enabled = TRUE;
    }

    if (AdapterOffload->RscIPv4) {
        Offload->Rsc.IPv6.Enabled = TRUE;
    }

    RtlReleasePushLockShared(&Adapter->PushLock);
}

static
NDIS_STATUS
MpSetOffloadAttributes(
    _Inout_ ADAPTER_CONTEXT *Adapter
    )
{
    NDIS_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES OffloadAttributes = {0};
    NDIS_OFFLOAD DefaultConfig;
    NDIS_OFFLOAD HwCapabilities;

    MpFillOffload(&DefaultConfig, Adapter, &Adapter->OffloadConfig);
    MpFillOffload(&HwCapabilities, Adapter, &Adapter->OffloadCapabilities);

    OffloadAttributes.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES;
    OffloadAttributes.Header.Revision = NDIS_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES_REVISION_1;
    OffloadAttributes.Header.Size = sizeof(NDIS_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES);

    OffloadAttributes.DefaultOffloadConfiguration = &DefaultConfig;
    OffloadAttributes.HardwareOffloadCapabilities = &HwCapabilities;

    return
        NdisMSetMiniportAttributes(
            Adapter->MiniportHandle,
            (NDIS_MINIPORT_ADAPTER_ATTRIBUTES *)&OffloadAttributes);
}

static
VOID
MpUpdateChecksumParameter(
    _Inout_ CHECKSUM_OFFLOAD_STATE *OffloadState,
    _In_ UCHAR ParameterValue
    )
{
    switch (ParameterValue) {
    case NDIS_OFFLOAD_PARAMETERS_NO_CHANGE:
        break;

    case NDIS_OFFLOAD_PARAMETERS_TX_RX_DISABLED:
        *OffloadState = ChecksumOffloadDisabled;
        break;

    case NDIS_OFFLOAD_PARAMETERS_TX_ENABLED_RX_DISABLED:
        *OffloadState = ChecksumOffloadTx;
        break;

    case NDIS_OFFLOAD_PARAMETERS_RX_ENABLED_TX_DISABLED:
        *OffloadState = ChecksumOffloadRx;
        break;

    case NDIS_OFFLOAD_PARAMETERS_TX_RX_ENABLED:
        *OffloadState = ChecksumOffloadRxTx;
        break;

    default:
        ASSERT(FALSE);
        break;
    }
}

static
VOID
MpUpdateOffloadParameter(
    _Inout_ UINT32 *OffloadState,
    _In_ UCHAR ParameterValue
    )
{
    switch (ParameterValue) {
    case NDIS_OFFLOAD_PARAMETERS_NO_CHANGE:
        break;

    case NDIS_OFFLOAD_PARAMETERS_LSOV2_DISABLED:
        C_ASSERT(NDIS_OFFLOAD_PARAMETERS_LSOV2_DISABLED == NDIS_OFFLOAD_PARAMETERS_USO_DISABLED);
        C_ASSERT(NDIS_OFFLOAD_PARAMETERS_LSOV2_DISABLED == NDIS_OFFLOAD_PARAMETERS_RSC_DISABLED);
        *OffloadState = FALSE;
        break;

    case NDIS_OFFLOAD_PARAMETERS_LSOV2_ENABLED:
        C_ASSERT(NDIS_OFFLOAD_PARAMETERS_LSOV2_ENABLED == NDIS_OFFLOAD_PARAMETERS_USO_ENABLED);
        C_ASSERT(NDIS_OFFLOAD_PARAMETERS_LSOV2_ENABLED == NDIS_OFFLOAD_PARAMETERS_RSC_ENABLED);
        *OffloadState = TRUE;
        break;

    default:
        ASSERT(FALSE);
        break;
    }
}

VOID
MpIndicateStatus(
    _In_ CONST ADAPTER_CONTEXT *Adapter,
    _In_ VOID *Buffer,
    _In_ UINT32 BufferSize,
    _In_ UINT32 StatusCode
    )
{
    NDIS_STATUS_INDICATION StatusIndication;

    RtlZeroMemory(&StatusIndication, sizeof(NDIS_STATUS_INDICATION));

    StatusIndication.Header.Type = NDIS_OBJECT_TYPE_STATUS_INDICATION;
    StatusIndication.Header.Size = NDIS_SIZEOF_STATUS_INDICATION_REVISION_1;
    StatusIndication.Header.Revision = NDIS_STATUS_INDICATION_REVISION_1;

    StatusIndication.SourceHandle = Adapter->MiniportHandle;
    StatusIndication.StatusBuffer = Buffer;
    StatusIndication.StatusBufferSize = BufferSize;
    StatusIndication.StatusCode = StatusCode;

    NdisMIndicateStatusEx(Adapter->MiniportHandle, &StatusIndication);
}

NDIS_STATUS
MpSetOffloadParameters(
    _Inout_ ADAPTER_CONTEXT *Adapter,
    _Inout_ ADAPTER_OFFLOAD *AdapterOffload,
    _In_ CONST NDIS_OFFLOAD_PARAMETERS *OffloadParameters,
    _In_ UINT32 OffloadParametersLength,
    _In_ UINT32 StatusCode
    )
{
    NDIS_OFFLOAD NdisOffload;
    NDIS_STATUS Status;

    if (OffloadParametersLength < NDIS_SIZEOF_OFFLOAD_PARAMETERS_REVISION_1 ||
        OffloadParameters->Header.Type != NDIS_OBJECT_TYPE_DEFAULT) {
        Status = NDIS_STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    //
    // First, update our internal state.
    //

    RtlAcquirePushLockExclusive(&Adapter->PushLock);

    if (OffloadParameters->Header.Revision >= NDIS_OFFLOAD_PARAMETERS_REVISION_1 &&
        OffloadParametersLength >= NDIS_SIZEOF_OFFLOAD_PARAMETERS_REVISION_1) {
        MpUpdateChecksumParameter(
            &AdapterOffload->IPChecksumOffloadIPv4, OffloadParameters->IPv4Checksum);
        MpUpdateChecksumParameter(
            &AdapterOffload->TCPChecksumOffloadIPv4, OffloadParameters->TCPIPv4Checksum);
        MpUpdateChecksumParameter(
            &AdapterOffload->TCPChecksumOffloadIPv6, OffloadParameters->TCPIPv6Checksum);
        MpUpdateChecksumParameter(
            &AdapterOffload->UDPChecksumOffloadIPv4, OffloadParameters->UDPIPv4Checksum);
        MpUpdateChecksumParameter(
            &AdapterOffload->UDPChecksumOffloadIPv6, OffloadParameters->UDPIPv6Checksum);
        MpUpdateOffloadParameter(&AdapterOffload->LsoV2IPv4, OffloadParameters->LsoV2IPv4);
        MpUpdateOffloadParameter(&AdapterOffload->LsoV2IPv6, OffloadParameters->LsoV2IPv6);
    }

    if (OffloadParameters->Header.Revision >= NDIS_OFFLOAD_PARAMETERS_REVISION_3 &&
        OffloadParametersLength >= NDIS_SIZEOF_OFFLOAD_PARAMETERS_REVISION_3) {
        MpUpdateOffloadParameter(&AdapterOffload->RscIPv4, OffloadParameters->RscIPv4);
        MpUpdateOffloadParameter(&AdapterOffload->RscIPv6, OffloadParameters->RscIPv6);
    }

    if (OffloadParameters->Header.Revision >= NDIS_OFFLOAD_PARAMETERS_REVISION_5 &&
        OffloadParametersLength >= NDIS_SIZEOF_OFFLOAD_PARAMETERS_REVISION_5) {
        MpUpdateOffloadParameter(&AdapterOffload->UsoIPv4, OffloadParameters->UdpSegmentation.IPv4);
        MpUpdateOffloadParameter(&AdapterOffload->UsoIPv6, OffloadParameters->UdpSegmentation.IPv6);
    }

    RtlReleasePushLockExclusive(&Adapter->PushLock);

    //
    // Then, build an NDIS offload struct and indicate up the stack. Note that
    // this state may already have changed after releasing the write lock.
    //

    MpFillOffload(&NdisOffload, Adapter, AdapterOffload);
    MpIndicateStatus(Adapter, &NdisOffload, sizeof(NdisOffload), StatusCode);

    Status = NDIS_STATUS_SUCCESS;

Exit:

    return Status;
}

static
NDIS_STATUS
MiniportInitializeHandler(
   _Inout_ NDIS_HANDLE NdisMiniportHandle,
   _In_ NDIS_HANDLE MiniportDriverContext,
   _In_ NDIS_MINIPORT_INIT_PARAMETERS *InitParameters
   )
{
    ADAPTER_CONTEXT *Adapter = NULL;
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
    NDIS_MINIPORT_ADAPTER_ATTRIBUTES AdapterAttributes;
    NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES *RegAttributes;
    NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES GeneralAttributes;
    NDIS_RECEIVE_SCALE_CAPABILITIES RssCaps;

    UNREFERENCED_PARAMETER(MiniportDriverContext);

    TraceEnter(TRACE_CONTROL, "NdisMiniportHandle=%p", NdisMiniportHandle);

    Adapter = MpCreateAdapter(NdisMiniportHandle, InitParameters->IfIndex);
    if (Adapter == NULL) {
        Status = NDIS_STATUS_RESOURCES;
        goto Exit;
    }

    TraceInfo(
        TRACE_CONTROL, "Adapter=%p allocated IfIndex=%u", Adapter, InitParameters->IfIndex);

    NdisMoveMemory(Adapter->MACAddress, MpMacAddressBase, MAC_ADDR_LEN);

    NdisZeroMemory(&AdapterAttributes, sizeof(NDIS_MINIPORT_ADAPTER_ATTRIBUTES));

    RegAttributes = &AdapterAttributes.RegistrationAttributes;
    RegAttributes->Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES;
    RegAttributes->Header.Revision = NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
    RegAttributes->Header.Size = sizeof(NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES);
    RegAttributes->MiniportAdapterContext = (NDIS_HANDLE)Adapter;
    RegAttributes->AttributeFlags =
        NDIS_MINIPORT_ATTRIBUTES_NDIS_WDM |
        NDIS_MINIPORT_ATTRIBUTES_SURPRISE_REMOVE_OK |
        NDIS_MINIPORT_ATTRIBUTES_NO_PAUSE_ON_SUSPEND;
    RegAttributes->CheckForHangTimeInSeconds = 0;
    RegAttributes->InterfaceType = NdisInterfacePNPBus;

    Status = NdisMSetMiniportAttributes(NdisMiniportHandle, &AdapterAttributes);
    if (Status != NDIS_STATUS_SUCCESS) {
        goto Exit;
    }

    Status = MpReadConfiguration(Adapter);
    if (Status != NDIS_STATUS_SUCCESS) {
        goto Exit;
    }

    NdisZeroMemory(&GeneralAttributes, sizeof(NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES));
    GeneralAttributes.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES;
    GeneralAttributes.Header.Revision = NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_2;
    GeneralAttributes.Header.Size = sizeof(NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES);
    GeneralAttributes.MediaType = MpGlobalContext.Medium;
    GeneralAttributes.MtuSize = Adapter->MtuSize;
    GeneralAttributes.MaxXmitLinkSpeed = MpGlobalContext.MaxXmitLinkSpeed;
    GeneralAttributes.XmitLinkSpeed = MpGlobalContext.XmitLinkSpeed;
    GeneralAttributes.MaxRcvLinkSpeed = MpGlobalContext.MaxRecvLinkSpeed;
    GeneralAttributes.RcvLinkSpeed = MpGlobalContext.RecvLinkSpeed;
    GeneralAttributes.MediaConnectState = MediaConnectStateConnected;
    GeneralAttributes.MediaDuplexState = MediaDuplexStateFull;
    GeneralAttributes.LookaheadSize = Adapter->MtuSize;
    GeneralAttributes.MacOptions = (ULONG)
        NDIS_MAC_OPTION_NO_LOOPBACK |
        NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
        NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
        NDIS_MAC_OPTION_FULL_DUPLEX |
        NDIS_MAC_OPTION_8021Q_VLAN |
        NDIS_MAC_OPTION_8021P_PRIORITY;
    GeneralAttributes.SupportedPacketFilters = MpGlobalContext.PacketFilter;
    GeneralAttributes.MaxMulticastListSize = MAX_MULTICAST_ADDRESSES;
    GeneralAttributes.MacAddressLength = MAC_ADDR_LEN;
    NdisMoveMemory(GeneralAttributes.PermanentMacAddress, Adapter->MACAddress, MAC_ADDR_LEN);
    NdisMoveMemory(GeneralAttributes.CurrentMacAddress, Adapter->MACAddress, MAC_ADDR_LEN);
    GeneralAttributes.PhysicalMediumType = NdisPhysicalMediumUnspecified;
    GeneralAttributes.AccessType = NET_IF_ACCESS_BROADCAST;
    GeneralAttributes.DirectionType = NET_IF_DIRECTION_SENDRECEIVE;
    GeneralAttributes.ConnectionType = (NET_IF_CONNECTION_TYPE)IF_CONNECTION_DEDICATED;
    GeneralAttributes.IfType = (NET_IFTYPE)IF_TYPE_ETHERNET_CSMACD;
    GeneralAttributes.IfConnectorPresent = TRUE;
    GeneralAttributes.SupportedStatistics = NDIS_STATISTICS_GEN_STATISTICS_SUPPORTED;
    GeneralAttributes.SupportedOidList = (NDIS_OID *)MpSupportedOidArray;
    GeneralAttributes.SupportedOidListLength = MpSupportedOidArraySize;

    if (Adapter->RssEnabled) {
        NdisZeroMemory(&RssCaps, sizeof(NDIS_RECEIVE_SCALE_CAPABILITIES));
        RssCaps.Header.Type = NDIS_OBJECT_TYPE_RSS_CAPABILITIES;
        RssCaps.Header.Revision = NDIS_RECEIVE_SCALE_CAPABILITIES_REVISION_2;
        RssCaps.Header.Size = sizeof(RssCaps);
        RssCaps.NumberOfInterruptMessages = MAX_RSS_QUEUES;
        RssCaps.NumberOfReceiveQueues = Adapter->NumRssQueues;
        RssCaps.NumberOfIndirectionTableEntries = FNMP_MAX_RSS_INDIR_COUNT;
        RssCaps.CapabilitiesFlags =
            NDIS_RSS_CAPS_HASH_TYPE_TCP_IPV4 |
            NDIS_RSS_CAPS_HASH_TYPE_TCP_IPV6 |
            NdisHashFunctionToeplitz;

        GeneralAttributes.RecvScaleCapabilities = &RssCaps;
    } else {
        GeneralAttributes.RecvScaleCapabilities = NULL;
    }

    NDIS_DECLARE_MINIPORT_ADAPTER_CONTEXT(ADAPTER_CONTEXT);
    Status =
        NdisMSetMiniportAttributes(
            NdisMiniportHandle, (NDIS_MINIPORT_ADAPTER_ATTRIBUTES *)&GeneralAttributes);
    if (Status != NDIS_STATUS_SUCCESS) {
        goto Exit;
    }

    Status = MpSetOffloadAttributes(Adapter);
    if (Status != NDIS_STATUS_SUCCESS) {
        goto Exit;
    }

    Status = MpCreateRssQueues(Adapter);
    if (Status != NDIS_STATUS_SUCCESS) {
        goto Exit;
    }

    RtlAcquirePushLockExclusive(&MpGlobalContext.Lock);
    InsertTailList(&MpGlobalContext.AdapterList, &Adapter->AdapterListLink);
    RtlReleasePushLockExclusive(&MpGlobalContext.Lock);

Exit:

    if (Status != NDIS_STATUS_SUCCESS && Adapter != NULL) {
        MpDereferenceAdapter(Adapter);
    }

    TraceExitStatus(TRACE_CONTROL);

    return Status;
}

static
VOID
MiniportHaltHandler(
   _In_ NDIS_HANDLE MiniportAdapterContext,
   _In_ NDIS_HALT_ACTION HaltAction
   )
{
    ADAPTER_CONTEXT *Adapter = (ADAPTER_CONTEXT *)MiniportAdapterContext;

    TraceEnter(TRACE_CONTROL, "Adapter=%p", Adapter);

    UNREFERENCED_PARAMETER(HaltAction);

    RtlAcquirePushLockExclusive(&MpGlobalContext.Lock);
    Adapter->MiniportHandle = NULL;
    if (!IsListEmpty(&Adapter->AdapterListLink)) {
        RemoveEntryList(&Adapter->AdapterListLink);
    }
    RtlReleasePushLockExclusive(&MpGlobalContext.Lock);

    MpDereferenceAdapter(Adapter);

    TraceExitSuccess(TRACE_CONTROL);
}

static
VOID
MiniportShutdownHandler(
   _In_ NDIS_HANDLE          MiniportAdapterContext,
   _In_ NDIS_SHUTDOWN_ACTION ShutdownAction
   )
{
    ADAPTER_CONTEXT *Adapter = (ADAPTER_CONTEXT *)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(Adapter);
    UNREFERENCED_PARAMETER(ShutdownAction);
}

static
VOID
MiniportPnPEventNotifyHandler(
   _In_ NDIS_HANDLE MiniportAdapterContext,
   _In_ NET_DEVICE_PNP_EVENT *NetDevicePnPEvent
   )
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(NetDevicePnPEvent);
}

static
VOID
MiniportUnloadHandler(
    _In_ DRIVER_OBJECT *DriverObject
    )
{
    TraceEnter(TRACE_CONTROL, "DriverObject=%p", DriverObject);

    if (MpGlobalContext.NdisMiniportDriverHandle != NULL) {
        NdisMDeregisterMiniportDriver(MpGlobalContext.NdisMiniportDriverHandle);
        MpGlobalContext.NdisMiniportDriverHandle = NULL;
    }

    MpIoctlCleanup();

    TraceExitSuccess(TRACE_CONTROL);

    WPP_CLEANUP(DriverObject);
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
MpWatchdog(
    _In_ VOID *CallbackContext
    )
{
    ADAPTER_CONTEXT *Adapter = CallbackContext;

    if (MpOidWatchdogIsExpired(Adapter)) {
        TraceError(
            TRACE_CONTROL, "Adapter=%p IfIndex=%u OID watchdog expired", Adapter, Adapter->IfIndex);
        MpOidClearFilterAndFlush(Adapter);
    }

    //
    // TODO: NBL watchdog
    //
}

_Function_class_(DRIVER_INITIALIZE)
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
DriverEntry(
    _In_ struct _DRIVER_OBJECT *DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS MChars;

#pragma prefast(suppress : __WARNING_BANNED_MEM_ALLOCATION_UNSAFE, "Non executable pool is enabled via -DPOOL_NX_OPTIN_AUTO=1.")
    ExInitializeDriverRuntime(0);
    WPP_INIT_TRACING(DriverObject, RegistryPath);
    ExInitializePushLock(&MpGlobalContext.Lock);
    InitializeListHead(&MpGlobalContext.AdapterList);

    TraceEnter(TRACE_CONTROL, "DriverObject=%p", DriverObject);

    MpGlobalContext.NdisVersion = NdisGetVersion();
    MpGlobalContext.Medium = NdisMedium802_3;
    MpGlobalContext.LinkSpeed = MAXULONG;
    MpGlobalContext.MaxRecvLinkSpeed = 100000000000;
    MpGlobalContext.RecvLinkSpeed = 100000000000;
    MpGlobalContext.MaxXmitLinkSpeed = 100000000000;
    MpGlobalContext.XmitLinkSpeed = 100000000000;
    MpGlobalContext.PacketFilter =
        NDIS_PACKET_TYPE_DIRECTED  |
        NDIS_PACKET_TYPE_MULTICAST |
        NDIS_PACKET_TYPE_BROADCAST |
        NDIS_PACKET_TYPE_PROMISCUOUS |
        NDIS_PACKET_TYPE_ALL_MULTICAST;

    NdisZeroMemory(&MChars, sizeof(MChars));
    MChars.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS;
    MChars.Header.Size = sizeof(NDIS_MINIPORT_DRIVER_CHARACTERISTICS);
    MChars.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_3;
    MChars.MajorNdisVersion = NDIS_MINIPORT_MAJOR_VERSION;
    MChars.MinorNdisVersion = NDIS_MINIPORT_MINOR_VERSION;
    MChars.Flags = 0;

    if (MpGlobalContext.NdisVersion >= NDIS_RUNTIME_VERSION_685) {
        //
        // Fe (Iron) / 20348 / Windows Server 2022 or higher.
        //
        MChars.MajorNdisVersion = 6;
        MChars.MinorNdisVersion = 85;
    } else if (MpGlobalContext.NdisVersion >= NDIS_RUNTIME_VERSION_682) {
        //
        // RS5 / 17763 / Windows 1809 / Windows Server 2019 or higher.
        //
        MChars.MajorNdisVersion = 6;
        MChars.MinorNdisVersion = 82;
    } else if (MpGlobalContext.NdisVersion >= NDIS_RUNTIME_VERSION_660) {
        //
        // RS1 / 14393 / Windows 1607 / Windows Server 2016 or higher.
        //
        MChars.MajorNdisVersion = 6;
        MChars.MinorNdisVersion = 60;
    } else {
        Status = NDIS_STATUS_NOT_SUPPORTED;
        goto Cleanup;
    }

    MChars.InitializeHandlerEx = MiniportInitializeHandler;
    MChars.HaltHandlerEx = MiniportHaltHandler;
    MChars.UnloadHandler = MiniportUnloadHandler;
    MChars.PauseHandler = MiniportPauseHandler;
    MChars.RestartHandler = MiniportRestartHandler;
    MChars.OidRequestHandler = MiniportRequestHandler;
    MChars.DirectOidRequestHandler = MiniportDirectRequestHandler;
    MChars.SendNetBufferListsHandler = MpSendNetBufferLists;
    MChars.ReturnNetBufferListsHandler = MpReturnNetBufferLists;
    MChars.CancelSendHandler = MiniportCancelSendHandler;
    MChars.DevicePnPEventNotifyHandler = MiniportPnPEventNotifyHandler;
    MChars.ShutdownHandlerEx = MiniportShutdownHandler;
    MChars.CancelOidRequestHandler = MiniportCancelRequestHandler;
    MChars.CancelDirectOidRequestHandler = MiniportCancelDirectRequestHandler;

    Status =
        NdisMRegisterMiniportDriver(
            DriverObject, RegistryPath, (NDIS_HANDLE *)NULL, &MChars,
            &MpGlobalContext.NdisMiniportDriverHandle);
    if (Status != NDIS_STATUS_SUCCESS) {
        goto Cleanup;
    }

    Status = MpIoctlStart();
    if (Status != NDIS_STATUS_SUCCESS) {
        goto Cleanup;
    }

Cleanup:

    TraceExitStatus(TRACE_CONTROL);

    if (Status != NDIS_STATUS_SUCCESS) {
        MiniportUnloadHandler(DriverObject);
    }

    return Status;
}
