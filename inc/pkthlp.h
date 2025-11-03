//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#pragma warning(push)
#pragma warning(disable:4201)  // nonstandard extension used: nameless struct/union

#if defined(_KERNEL_MODE) && !defined(htons)
#define __pkthlp_htons
#define htons RtlUshortByteSwap
#define ntohs RtlUshortByteSwap
#define htonl RtlUlongByteSwap
#define ntohl RtlUlongByteSwap
#endif

#ifndef STATUS_SUCCESS
#define __pkthlp_NTSTATUS
#define STATUS_SUCCESS 0
#endif

EXTERN_C_START

typedef DL_EUI48 ETHERNET_ADDRESS;

typedef struct _UDP_HDR {
    UINT16 uh_sport;
    UINT16 uh_dport;
    UINT16 uh_ulen;
    UINT16 uh_sum;
} UDP_HDR;

typedef union {
    IN_ADDR Ipv4;
    IN6_ADDR Ipv6;
} INET_ADDR;

#define UDP_HEADER_BACKFILL(AddressFamily) \
    (sizeof(ETHERNET_HEADER) + sizeof(UDP_HDR) + \
        ((AddressFamily == AF_INET) ? sizeof(IPV4_HEADER) : sizeof(IPV6_HEADER)))

#define TCP_HEADER_BACKFILL(AddressFamily) \
    (sizeof(ETHERNET_HEADER) + sizeof(TCP_HDR) + \
        ((AddressFamily == AF_INET) ? sizeof(IPV4_HEADER) : sizeof(IPV6_HEADER)))

#define TCP_MAX_OPTION_LEN 40
#define QUIC_MAX_HEADER_LEN 47 // Based on RFC 9000 (QUIC v1)
#define UDP_HEADER_STORAGE UDP_HEADER_BACKFILL(AF_INET6)
#define TCP_HEADER_STORAGE (TCP_HEADER_BACKFILL(AF_INET6) + TCP_MAX_OPTION_LEN)
#define ICMP4_ECHOREPLY_TYPE 0
#define ICMP4_ECHOREPLY_CODE 0
#define ICMP6_ECHOREPLY_TYPE 129
#define ICMP6_ECHOREPLY_CODE 0

inline
UINT16
PktChecksumFold(
    _In_ UINT32 Checksum
    )
{
    Checksum = (UINT16)Checksum + (Checksum >> 16);
    Checksum = (UINT16)Checksum + (Checksum >> 16);

    return (UINT16)Checksum;
}

inline
UINT16
PktPartialChecksum(
    _In_ CONST VOID *Buffer,
    _In_ UINT16 BufferLength
    )
{
    UINT32 Checksum = 0;
    CONST UINT16 *Buffer16 = (CONST UINT16 *)Buffer;

    while (BufferLength >= sizeof(*Buffer16)) {
        Checksum += *Buffer16++;
        BufferLength -= sizeof(*Buffer16);
    }

    if (BufferLength > 0) {
        Checksum += *(UCHAR *)Buffer16;
    }

    return PktChecksumFold(Checksum);
}

inline
UINT16
PktPseudoHeaderChecksum(
    _In_ CONST VOID *SourceAddress,
    _In_ CONST VOID *DestinationAddress,
    _In_ UINT8 AddressLength,
    _In_ UINT16 DataLength,
    _In_ UINT8 NextHeader
    )
{
    UINT32 Checksum = 0;

    Checksum += PktPartialChecksum(SourceAddress, AddressLength);
    Checksum += PktPartialChecksum(DestinationAddress, AddressLength);
    DataLength = htons(DataLength);
    Checksum += PktPartialChecksum(&DataLength, sizeof(DataLength));
    Checksum += (NextHeader << 8);

    return PktChecksumFold(Checksum);
}

inline
UINT16
PktChecksum(
    _In_ UINT16 InitialChecksum,
    _In_ CONST VOID *Buffer,
    _In_ UINT16 BufferLength
    )
{
    UINT32 Checksum = InitialChecksum;

    Checksum += PktPartialChecksum(Buffer, BufferLength);

    return ~PktChecksumFold(Checksum);
}

inline
_Success_(return != FALSE)
BOOLEAN
PktBuildUdpFrame(
    _Out_ VOID *Buffer,
    _Inout_ UINT32 *BufferSize,
    _In_ CONST UCHAR *Payload,
    _In_ UINT16 PayloadLength,
    _In_ CONST ETHERNET_ADDRESS *EthernetDestination,
    _In_ CONST ETHERNET_ADDRESS *EthernetSource,
    _In_ ADDRESS_FAMILY AddressFamily,
    _In_ CONST VOID *IpDestination,
    _In_ CONST VOID *IpSource,
    _In_ UINT16 PortDestination,
    _In_ UINT16 PortSource
    )
{
    CONST UINT32 TotalLength = UDP_HEADER_BACKFILL(AddressFamily) + PayloadLength;
    if (*BufferSize < TotalLength) {
        return FALSE;
    }

    UINT16 UdpLength = sizeof(UDP_HDR) + PayloadLength;
    UINT8 AddressLength;

    ETHERNET_HEADER *EthernetHeader = (ETHERNET_HEADER *)Buffer;
    EthernetHeader->Destination = *EthernetDestination;
    EthernetHeader->Source = *EthernetSource;
    EthernetHeader->Type =
        htons(AddressFamily == AF_INET ? ETHERNET_TYPE_IPV4 : ETHERNET_TYPE_IPV6);
    Buffer = EthernetHeader + 1;

    if (AddressFamily == AF_INET) {
        IPV4_HEADER *IpHeader = (IPV4_HEADER *)Buffer;

        if (UdpLength + (UINT16)sizeof(*IpHeader) < UdpLength) {
            return FALSE;
        }

        RtlZeroMemory(IpHeader, sizeof(*IpHeader));
        IpHeader->Version = IPV4_VERSION;
        IpHeader->HeaderLength = sizeof(*IpHeader) >> 2;
        IpHeader->TotalLength = htons(sizeof(*IpHeader) + UdpLength);
        IpHeader->TimeToLive = 1;
        IpHeader->Protocol = IPPROTO_UDP;
        AddressLength = sizeof(IN_ADDR);
        RtlCopyMemory(&IpHeader->SourceAddress, IpSource, AddressLength);
        RtlCopyMemory(&IpHeader->DestinationAddress, IpDestination, AddressLength);
        IpHeader->HeaderChecksum = PktChecksum(0, IpHeader, sizeof(*IpHeader));

        Buffer = IpHeader + 1;
    } else {
        IPV6_HEADER *IpHeader = (IPV6_HEADER *)Buffer;
        RtlZeroMemory(IpHeader, sizeof(*IpHeader));
        IpHeader->VersionClassFlow = IPV6_VERSION;
        IpHeader->PayloadLength = htons(UdpLength);
        IpHeader->NextHeader = IPPROTO_UDP;
        IpHeader->HopLimit = 1;
        AddressLength = sizeof(IN6_ADDR);
        RtlCopyMemory(&IpHeader->SourceAddress, IpSource, AddressLength);
        RtlCopyMemory(&IpHeader->DestinationAddress, IpDestination, AddressLength);

        Buffer = IpHeader + 1;
    }

    UDP_HDR *UdpHeader = (UDP_HDR *)Buffer;
    UdpHeader->uh_sport = PortSource;
    UdpHeader->uh_dport = PortDestination;
    UdpHeader->uh_ulen = htons(UdpLength);
    UdpHeader->uh_sum =
        PktPseudoHeaderChecksum(IpSource, IpDestination, AddressLength, UdpLength, IPPROTO_UDP);

    Buffer = UdpHeader + 1;

    RtlCopyMemory(Buffer, Payload, PayloadLength);
    UdpHeader->uh_sum = PktChecksum(0, UdpHeader, UdpLength);

    if (UdpHeader->uh_sum == 0 && AddressFamily == AF_INET6) {
        //
        // UDPv6 requires a non-zero checksum field.
        //
        UdpHeader->uh_sum = (UINT16)~0;
    }

    *BufferSize = TotalLength;

    return TRUE;
}

inline
_Success_(return != FALSE)
BOOLEAN
PktBuildTcpFrame(
    _Out_ VOID *Buffer,
    _Inout_ UINT32 *BufferSize,
    _In_opt_ CONST UCHAR *Payload,
    _In_ UINT16 PayloadLength,
    _In_opt_ CONST UINT8 *TcpOptions,
    _In_ UINT16 TcpOptionsLength,
    _In_ UINT32 ThSeq, // host order
    _In_ UINT32 ThAck, // host order
    _In_ UINT8 ThFlags,
    _In_ UINT16 ThWin, // host order
    _In_ CONST ETHERNET_ADDRESS *EthernetDestination,
    _In_ CONST ETHERNET_ADDRESS *EthernetSource,
    _In_ ADDRESS_FAMILY AddressFamily,
    _In_ CONST VOID *IpDestination,
    _In_ CONST VOID *IpSource,
    _In_ UINT16 PortDestination,
    _In_ UINT16 PortSource
    )
{
    CONST UINT32 TotalLength =
        TCP_HEADER_BACKFILL(AddressFamily) + PayloadLength + TcpOptionsLength;
    if (*BufferSize < TotalLength || TcpOptionsLength > TCP_MAX_OPTION_LEN) {
        return FALSE;
    }

    UINT16 TcpLength = sizeof(TCP_HDR) + TcpOptionsLength + PayloadLength;
    UINT8 AddressLength;

    ETHERNET_HEADER *EthernetHeader = (ETHERNET_HEADER *)Buffer;
    EthernetHeader->Destination = *EthernetDestination;
    EthernetHeader->Source = *EthernetSource;
    EthernetHeader->Type =
        htons(AddressFamily == AF_INET ? ETHERNET_TYPE_IPV4 : ETHERNET_TYPE_IPV6);
    Buffer = EthernetHeader + 1;

    if (AddressFamily == AF_INET) {
        IPV4_HEADER *IpHeader = (IPV4_HEADER *)Buffer;

        if (TcpLength + (UINT16)sizeof(*IpHeader) < TcpLength) {
            return FALSE;
        }

        RtlZeroMemory(IpHeader, sizeof(*IpHeader));
        IpHeader->Version = IPV4_VERSION;
        IpHeader->HeaderLength = sizeof(*IpHeader) >> 2;
        IpHeader->TotalLength = htons(sizeof(*IpHeader) + TcpLength);
        IpHeader->TimeToLive = 1;
        IpHeader->Protocol = IPPROTO_TCP;
        AddressLength = sizeof(IN_ADDR);
        RtlCopyMemory(&IpHeader->SourceAddress, IpSource, AddressLength);
        RtlCopyMemory(&IpHeader->DestinationAddress, IpDestination, AddressLength);
        IpHeader->HeaderChecksum = PktChecksum(0, IpHeader, sizeof(*IpHeader));

        Buffer = IpHeader + 1;
    } else {
        IPV6_HEADER *IpHeader = (IPV6_HEADER *)Buffer;
        RtlZeroMemory(IpHeader, sizeof(*IpHeader));
        IpHeader->VersionClassFlow = IPV6_VERSION;
        IpHeader->PayloadLength = htons(TcpLength);
        IpHeader->NextHeader = IPPROTO_TCP;
        IpHeader->HopLimit = 1;
        AddressLength = sizeof(IN6_ADDR);
        RtlCopyMemory(&IpHeader->SourceAddress, IpSource, AddressLength);
        RtlCopyMemory(&IpHeader->DestinationAddress, IpDestination, AddressLength);

        Buffer = IpHeader + 1;
    }

    TCP_HDR *TcpHeader = (TCP_HDR *)Buffer;
    TcpHeader->th_sport = PortSource;
    TcpHeader->th_dport = PortDestination;
    TcpHeader->th_len = (UINT8)((sizeof(TCP_HDR) + TcpOptionsLength) / 4);
    TcpHeader->th_x2 = 0;
    TcpHeader->th_urp = 0;
    TcpHeader->th_seq = htonl(ThSeq);
    TcpHeader->th_ack = htonl(ThAck);
    TcpHeader->th_win = htons(ThWin);
    TcpHeader->th_flags = ThFlags;
    TcpHeader->th_sum =
        PktPseudoHeaderChecksum(IpSource, IpDestination, AddressLength, TcpLength, IPPROTO_TCP);

    Buffer = TcpHeader + 1;
    if (TcpOptions != NULL) {
        RtlCopyMemory(Buffer, TcpOptions, TcpOptionsLength);
    }

    Buffer = (UINT8 *)Buffer + TcpOptionsLength;
    if (Payload != NULL) {
        RtlCopyMemory(Buffer, Payload, PayloadLength);
    }
    TcpHeader->th_sum = PktChecksum(0, TcpHeader, TcpLength);
    *BufferSize = TotalLength;

    return TRUE;
}

inline
_Success_(return != FALSE)
BOOLEAN
PktBuildIcmp4Frame(
    _Out_ VOID *Buffer,
    _Inout_ UINT32 *BufferSize,
    _In_ CONST UCHAR *Payload,
    _In_ UINT16 PayloadLength,
    _In_ CONST ETHERNET_ADDRESS *EthernetDestination,
    _In_ CONST ETHERNET_ADDRESS *EthernetSource,
    _In_ CONST VOID *IpDestination,
    _In_ CONST VOID *IpSource
    )
{
    CONST UINT32 TotalLength = sizeof(ETHERNET_HEADER) + sizeof(IPV4_HEADER) + sizeof(ICMPV4_HEADER) + PayloadLength;
    if (*BufferSize < TotalLength) {
        return FALSE;
    }
    void *PktBuffer = Buffer;

    // Fill Ethernet headers.
    ETHERNET_HEADER *EthernetHeader = (ETHERNET_HEADER *) PktBuffer;
    EthernetHeader->Destination = *EthernetDestination;
    EthernetHeader->Source = *EthernetSource;
    EthernetHeader->Type = htons(ETHERNET_TYPE_IPV4);
    PktBuffer = EthernetHeader + 1;

    // Fill IP and ICMP headers
    IPV4_HEADER *IpHeader = (IPV4_HEADER *)PktBuffer;
    IpHeader->Version = IPV4_VERSION;
    IpHeader->HeaderLength = sizeof(*IpHeader) >> 2;
    IpHeader->TotalLength = htons(sizeof(*IpHeader) + sizeof(ICMPV4_HEADER) + PayloadLength);
    IpHeader->Protocol = IPPROTO_ICMP;
    IpHeader->TimeToLive = 1;
    UINT8 AddressLength = sizeof(IN_ADDR);
    RtlCopyMemory(&IpHeader->SourceAddress, IpSource, AddressLength);
    RtlCopyMemory(&IpHeader->DestinationAddress, IpDestination, AddressLength);
    IpHeader->HeaderChecksum = PktChecksum(0, IpHeader, sizeof(*IpHeader));
    PktBuffer = IpHeader + 1;
    ICMPV4_HEADER *Icmp4Hdr = (ICMPV4_HEADER *) PktBuffer;
    Icmp4Hdr->Type = ICMP4_ECHOREPLY_TYPE;
    Icmp4Hdr->Code = ICMP4_ECHOREPLY_CODE;
    PktBuffer = Icmp4Hdr + 1;

    // Fill ICMP payload
    RtlCopyMemory(PktBuffer, Payload, PayloadLength);
    Icmp4Hdr->Checksum = PktChecksum(0, Icmp4Hdr, sizeof(*Icmp4Hdr) + PayloadLength);

    *BufferSize = TotalLength;
    return TRUE;
}

inline
_Success_(return != FALSE)
BOOLEAN
PktBuildIcmp6Frame(
    _Out_ VOID *Buffer,
    _Inout_ UINT32 *BufferSize,
    _In_ CONST UCHAR *Payload,
    _In_ UINT16 PayloadLength,
    _In_ CONST ETHERNET_ADDRESS *EthernetDestination,
    _In_ CONST ETHERNET_ADDRESS *EthernetSource,
    _In_ CONST VOID *IpDestination,
    _In_ CONST VOID *IpSource
    )
{
    CONST UINT32 TotalLength = sizeof(ETHERNET_HEADER) + sizeof(IPV6_HEADER) + sizeof(ICMPV6_HEADER) + PayloadLength;
    if (*BufferSize < TotalLength) {
        return FALSE;
    }

    void* PktBuffer = Buffer;

    // Fill Ethernet headers.
    ETHERNET_HEADER *EthernetHeader = (ETHERNET_HEADER *) PktBuffer;
    EthernetHeader->Destination = *EthernetDestination;
    EthernetHeader->Source = *EthernetSource;
    EthernetHeader->Type = htons(ETHERNET_TYPE_IPV6);
    PktBuffer = EthernetHeader + 1;

    // Fill IP and ICMP headers
    IPV6_HEADER *IpHeader = (IPV6_HEADER *)PktBuffer;
    IpHeader->VersionClassFlow = IPV6_VERSION;
    IpHeader->PayloadLength = htons(sizeof(ICMPV6_HEADER) + PayloadLength);
    IpHeader->NextHeader = IPPROTO_ICMPV6;
    IpHeader->HopLimit = 1;
    UINT8 AddressLength = sizeof(IN6_ADDR);
    RtlCopyMemory(&IpHeader->SourceAddress, IpSource, AddressLength);
    RtlCopyMemory(&IpHeader->DestinationAddress, IpDestination, AddressLength);
    PktBuffer = IpHeader + 1;
    ICMPV6_HEADER *Icmp6Hdr = (ICMPV6_HEADER *) PktBuffer;
    Icmp6Hdr->Type = ICMP6_ECHOREPLY_TYPE;
    Icmp6Hdr->Code = ICMP6_ECHOREPLY_CODE;
    PktBuffer = Icmp6Hdr + 1;

    // Fill ICMP payload
    RtlCopyMemory(PktBuffer, Payload, PayloadLength);

    //
    // Prepare data to compute ICMPV6 checksum
    // Data for checksum calculation: { IPV6 pseudo header + ICMPv6 header (0 for cxsum) + ICMPv6 payload }
    //

    // Pseudo header
    UINT32 Checksum = 0;
    Checksum += PktPartialChecksum(IpSource, AddressLength);
    Checksum += PktPartialChecksum(IpDestination, AddressLength);

    UINT16 UpperLayerPacketLen = sizeof(ICMPV6_HEADER) + PayloadLength;

    UINT32 UpperLayerPacketLen2 = htons(UpperLayerPacketLen);
    Checksum += PktPartialChecksum(&UpperLayerPacketLen2, sizeof(UINT32));
    Checksum += (IpHeader->NextHeader << 8);

    // ICMPv6 header
    Icmp6Hdr->Checksum = 0;
    Checksum += PktPartialChecksum(Icmp6Hdr, sizeof(*Icmp6Hdr));

    // Payload
    Checksum += PktPartialChecksum(Payload, PayloadLength);
    Icmp6Hdr->Checksum = ~PktChecksumFold(Checksum);

    *BufferSize = TotalLength;

    return TRUE;
}

inline
_Success_(return != FALSE)
BOOLEAN
PktBuildQuicV1PacketLongHeader(
    _Out_writes_bytes_(*BufferSize) VOID *Buffer,
    _Inout_ UINT32 *BufferSize,
    _In_ UINT8 TypeAndSpecificBits,
    _In_reads_bytes_(DestConnIdLength) CONST UCHAR *DestConnId,
    _In_ UINT8 DestConnIdLength,
    _In_reads_bytes_(SrcConnIdLength) CONST UCHAR *SrcConnId,
    _In_ UINT8 SrcConnIdLength,
    _In_opt_ CONST UCHAR *Payload,
    _In_ UINT16 PayloadLength
    )
{
    UINT8* Cursor = (UINT8*)Buffer;
    CONST UINT32 Version = 1;
    CONST UINT32 TotalLength = sizeof(UINT8) + sizeof(Version) + sizeof(DestConnIdLength) +
        DestConnIdLength + sizeof(SrcConnIdLength) + SrcConnIdLength + PayloadLength;

    if (*BufferSize < TotalLength) {
        return FALSE;
    }

    //
    // Set the Header Form bit to 1 for a long header, and Fixed bit to 1
    //
    CONST UINT8 Prelude = 0xC0 | (0x3F & TypeAndSpecificBits);

    *Cursor = Prelude;
    Cursor += sizeof(Prelude);

    RtlCopyMemory(Cursor, &Version, sizeof(Version));
    Cursor += sizeof(Version);

    *Cursor = DestConnIdLength;
    Cursor += sizeof(DestConnIdLength);
    RtlCopyMemory(Cursor, DestConnId, DestConnIdLength);
    Cursor += DestConnIdLength;

    *Cursor = SrcConnIdLength;
    Cursor += sizeof(SrcConnIdLength);
    RtlCopyMemory(Cursor, SrcConnId, SrcConnIdLength);
    Cursor += SrcConnIdLength;

    if (Payload != NULL) {
        RtlCopyMemory(Cursor, Payload, PayloadLength);
    }

    *BufferSize = TotalLength;
    return TRUE;
}

inline
_Success_(return != FALSE)
BOOLEAN
PktBuildQuicPacketShortHeader(
    _Out_writes_bytes_(*BufferSize) VOID *Buffer,
    _Inout_ UINT32 *BufferSize,
    _In_ UINT8 TypeAndSpecificBits,
    _In_reads_bytes_(DestConnIdLength) CONST UCHAR *DestConnId,
    _In_ UINT8 DestConnIdLength,
    _In_opt_ CONST UCHAR *Payload,
    _In_ UINT16 PayloadLength
    )
{
    UINT8* Cursor = (UINT8*)Buffer;

    CONST UINT32 TotalLength = sizeof(UINT8) + DestConnIdLength + PayloadLength;
    if (*BufferSize < TotalLength) {
        return FALSE;
    }

    //
    // Set the Header Form bit to 0 for a short header, and Fixed bit to 1
    //
    CONST UINT8 Prelude = 0x40 | (0x3F & TypeAndSpecificBits);

    *Cursor = Prelude;
    Cursor += sizeof(Prelude);

    RtlCopyMemory(Cursor, DestConnId, DestConnIdLength);
    Cursor += DestConnIdLength;

    if (Payload != NULL) {
        RtlCopyMemory(Cursor, Payload, PayloadLength);
    }

    *BufferSize = TotalLength;
    return TRUE;
}

//
// Build a QUIC packet from the given parameters and paylaod.
// The frame is meant to be wraped in a TCP or UDP frames,
// as the payload in `PktBuildTcpFrame` or `PktBuildUdpFrame`.
//
inline
_Success_(return != FALSE)
BOOLEAN
PktBuildQuicPacket(
    _Out_writes_bytes_(*BufferSize) VOID *Buffer,
    _Inout_ UINT32 *BufferSize,
    _In_opt_ CONST UCHAR *Payload,
    _In_ UINT16 PayloadLength,
    _In_ UINT8 TypeAndSpecificBits,
    _In_ UINT32 Version,
    _In_reads_bytes_(DestConnIdLength) CONST UCHAR *DestConnId,
    _In_ UINT8 DestConnIdLength,
    _In_reads_bytes_(SrcConnIdLength) CONST UCHAR *SrcConnId,
    _In_ UINT8 SrcConnIdLength,
    _In_ BOOLEAN UseShortHeader
    )
{
    //
    // Only QUIC v1 is supported.
    //
    if (Version != 1) {
        return FALSE;
    }

    if (UseShortHeader) {
        return
            PktBuildQuicPacketShortHeader(
                Buffer, BufferSize, TypeAndSpecificBits, DestConnId, DestConnIdLength, Payload,
                PayloadLength);
    }

    return
        PktBuildQuicV1PacketLongHeader(
            Buffer, BufferSize, TypeAndSpecificBits, DestConnId, DestConnIdLength, SrcConnId,
            SrcConnIdLength, Payload, PayloadLength);
}

inline
_Success_(return != FALSE)
BOOLEAN
PktParseTcpFrame(
    _In_ UCHAR *Frame,
    _In_ UINT32 FrameSize,
    _Out_ TCP_HDR **TcpHdr,
    _Outptr_opt_result_maybenull_ VOID **Payload,
    _Out_opt_ UINT32 *PayloadLength
    )
{
    UINT16 IpProto = IPPROTO_MAX;
    ETHERNET_HEADER *EthHdr;
    UINT16 IPPayloadLength;
    UINT32 Offset = 0;

    if (FrameSize < sizeof(*EthHdr)) {
        return FALSE;
    }

    EthHdr = (ETHERNET_HEADER *)Frame;
    Offset += sizeof(*EthHdr);
    if (EthHdr->Type == htons(ETHERNET_TYPE_IPV4)) {
        if (FrameSize < Offset + sizeof(IPV4_HEADER)) {
            return FALSE;
        }
        IPV4_HEADER *Ip = (IPV4_HEADER *)&Frame[Offset];
        Offset += sizeof(IPV4_HEADER);
        IpProto = Ip->Protocol;
        IPPayloadLength = ntohs(Ip->TotalLength) - sizeof(IPV4_HEADER);
    } else if (EthHdr->Type == htons(ETHERNET_TYPE_IPV6)) {
        if (FrameSize < Offset + sizeof(IPV6_HEADER)) {
            return FALSE;
        }
        IPV6_HEADER *Ip = (IPV6_HEADER *)&Frame[Offset];
        Offset += sizeof(IPV6_HEADER);
        IpProto = (Ip)->NextHeader;
        IPPayloadLength = ntohs(Ip->PayloadLength);
    } else {
        return FALSE;
    }

    if (IpProto == IPPROTO_TCP) {
        if (FrameSize < Offset + sizeof(TCP_HDR)) {
            return FALSE;
        }

        *TcpHdr = (TCP_HDR *)&Frame[Offset];
        UINT8 TcpHeaderLen = (*TcpHdr)->th_len * 4;
        if (FrameSize >= Offset + IPPayloadLength) {
            if (Payload != NULL) {
                Offset += TcpHeaderLen;
                *Payload = &Frame[Offset];
            }

            if (PayloadLength != NULL) {
                *PayloadLength = IPPayloadLength - TcpHeaderLen;
            }
        } else {
            return FALSE;
        }
    } else {
        return FALSE;
    }

    return TRUE;
}

#ifndef _KERNEL_MODE
inline
BOOLEAN
PktStringToInetAddressA(
    _Out_ INET_ADDR *InetAddr,
    _Out_ ADDRESS_FAMILY *AddressFamily,
    _In_ CONST CHAR *String
    )
{
    NTSTATUS Status;
    CONST CHAR *Terminator;

    //
    // Attempt to parse the target as an IPv4 literal.
    //
    *AddressFamily = AF_INET;
    Status = RtlIpv4StringToAddressA(String, TRUE, &Terminator, &InetAddr->Ipv4);

    if (Status != STATUS_SUCCESS) {
        //
        // Attempt to parse the target as an IPv6 literal.
        //
        *AddressFamily = AF_INET6;
        Status = RtlIpv6StringToAddressA(String, &Terminator, &InetAddr->Ipv6);

        if (Status != STATUS_SUCCESS) {
            //
            // No luck, bail.
            //
            return FALSE;
        }
    }

    return TRUE;
}
#endif

EXTERN_C_END

#ifdef __pkthlp_NTSTATUS
#undef STATUS_SUCCESS
#endif

#ifdef __pkthlp_htons
#undef htons
#undef ntohs
#undef htonl
#undef ntohl
#endif

#pragma warning(pop)
