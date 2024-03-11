//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#if defined(KERNEL_MODE)
#include <ntddk.h>
#include <ntintsafe.h>
#include <ndis.h>
#include <ws2def.h>
#include <ws2ipdef.h>
#include <netiodef.h>
#include <mstcpip.h>
#pragma warning(push) // SAL issues in WIL header.
#pragma warning(disable:28157)
#pragma warning(disable:28158)
#pragma warning(disable:28167)
#include <wil/resource.h>
#pragma warning(pop)
#else
#include <chrono>
#include <cstdio>
#include <future>
#include <xlocnum>

// Windows and WIL includes need to be ordered in a certain way.
#pragma warning(push)
#pragma warning(disable:4324) // structure was padded due to alignment specifier
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <ws2def.h>
#include <ws2ipdef.h>
#include <netiodef.h>
#include <mstcpip.h>
#include <iphlpapi.h>
#pragma warning(pop)
#include <wil/resource.h>
#endif // defined(KERNEL_MODE)

#include <cxplat.h>
#include <pkthlp.h>
#include <fnmpapi.h>
#include <fnlwfapi.h>
#include <fntrace.h>

#include "fntest.h"

#include "tests.tmh"

#define FNMP_IF_DESC "FNMP"
#define FNMP_IPV4_ADDRESS "192.168.200.1"
#define FNMP_NEIGHBOR_IPV4_ADDRESS "192.168.200.2"
#define FNMP_IPV6_ADDRESS "fc00::200:1"
#define FNMP_NEIGHBOR_IPV6_ADDRESS "fc00::200:2"

FNMP_LOAD_API_CONTEXT FnMpLoadApiContext;
FNLWF_LOAD_API_CONTEXT FnLwfLoadApiContext;

#if defined(KERNEL_MODE)
#define TEST_FNMPAPI TEST_NTSTATUS
#define TEST_FNLWFAPI TEST_NTSTATUS
#else
#define TEST_FNMPAPI TEST_HRESULT
#define TEST_FNLWFAPI TEST_HRESULT
#endif // defined(KERNEL_MODE)

//
// A timeout value that allows for a little latency, e.g. async threads to
// execute.
//
#define TEST_TIMEOUT_ASYNC_MS 1000
/*
#define TEST_TIMEOUT_ASYNC std::chrono::milliseconds(TEST_TIMEOUT_ASYNC_MS)

//
// The expected maximum time needed for a network adapter to restart.
//
#define MP_RESTART_TIMEOUT std::chrono::seconds(15)

//
// Interval between polling attempts.
//
#define POLL_INTERVAL_MS 10
C_ASSERT(POLL_INTERVAL_MS * 5 <= TEST_TIMEOUT_ASYNC_MS);
C_ASSERT(POLL_INTERVAL_MS * 5 <= std::chrono::milliseconds(MP_RESTART_TIMEOUT).count());

*/
template <typename T>
using unique_malloc_ptr = wistd::unique_ptr<T, wil::function_deleter<decltype(&::free), ::free>>;
using unique_fnmp_handle = wil::unique_any<FNMP_HANDLE, decltype(::FnMpClose), ::FnMpClose>;
using unique_fnlwf_handle = wil::unique_any<FNLWF_HANDLE, decltype(::FnLwfClose), ::FnLwfClose>;
using unique_cxplat_socket = wil::unique_any<CXPLAT_SOCKET, decltype(::CxPlatSocketClose), ::CxPlatSocketClose>;

static CONST CHAR *PowershellPrefix = "powershell -noprofile -ExecutionPolicy Bypass";

//
// Helper functions.
//

class TestInterface;

/*
static
INT
InvokeSystem(
    _In_z_ const CHAR *Command
    )
{
    INT Result;

    TraceVerbose("system(%s)", Command);
    Result = system(Command);
    TraceVerbose("system(%s) returned %u", Command, Result);

    return Result;
}
*/

class TestInterface {
private:
    CONST CHAR *_IfDesc;
    mutable UINT32 _IfIndex;
    mutable UCHAR _HwAddress[sizeof(ETHERNET_ADDRESS)]{ 0 };
    IN_ADDR _Ipv4Address;
    IN6_ADDR _Ipv6Address;

    VOID
    Query() const
    {
/*
        IP_ADAPTER_INFO *Adapter;
        ULONG OutBufLen;

        if (ReadUInt32Acquire(&_IfIndex) != NET_IFINDEX_UNSPECIFIED) {
            return;
        }

        //
        // Get information on all adapters.
        //
        OutBufLen = 0;
        TEST_EQUAL((ULONG)ERROR_BUFFER_OVERFLOW, GetAdaptersInfo(NULL, &OutBufLen));
        unique_malloc_ptr<IP_ADAPTER_INFO> AdapterInfoList{ (IP_ADAPTER_INFO *)malloc(OutBufLen) };
        TEST_NOT_NULL(AdapterInfoList);
        TEST_EQUAL((ULONG)NO_ERROR, GetAdaptersInfo(AdapterInfoList.get(), &OutBufLen));

        //
        // Search for the test adapter.
        //
        Adapter = AdapterInfoList.get();
        while (Adapter != NULL) {
            if (!strcmp(Adapter->Description, _IfDesc)) {
                TEST_EQUAL(sizeof(_HwAddress), Adapter->AddressLength);
                RtlCopyMemory(_HwAddress, Adapter->Address, sizeof(_HwAddress));

                WriteUInt32Release(&_IfIndex, Adapter->Index);
            }
            Adapter = Adapter->Next;
        }

        TEST_NOT_EQUAL(NET_IFINDEX_UNSPECIFIED, _IfIndex);
//*/
    }

public:

    TestInterface(
        _In_z_ CONST CHAR *IfDesc,
        _In_z_ CONST CHAR *Ipv4Address,
        _In_z_ CONST CHAR *Ipv6Address
        )
        :
        _IfDesc(IfDesc),
        _IfIndex(NET_IFINDEX_UNSPECIFIED)
    {
        CONST CHAR *Terminator;
        TEST_NTSTATUS(RtlIpv4StringToAddressA(Ipv4Address, FALSE, &Terminator, &_Ipv4Address));
        TEST_NTSTATUS(RtlIpv6StringToAddressA(Ipv6Address, &Terminator, &_Ipv6Address));
    }

    CONST CHAR*
    GetIfDesc() const
    {
        return _IfDesc;
    }

    NET_IFINDEX
    GetIfIndex() const
    {
        Query();
        return _IfIndex;
    }

    UINT32
    GetQueueId() const
    {
        return 0;
    }

    VOID
    GetHwAddress(
        _Out_ ETHERNET_ADDRESS *HwAddress
        ) const
    {
        Query();
        RtlCopyMemory(HwAddress, _HwAddress, sizeof(_HwAddress));
    }

    VOID
    GetRemoteHwAddress(
        _Out_ ETHERNET_ADDRESS *HwAddress
        ) const
    {
        GetHwAddress(HwAddress);
        HwAddress->Byte[sizeof(_HwAddress) - 1]++;
    }

    VOID
    GetIpv4Address(
        _Out_ IN_ADDR *Ipv4Address
        ) const
    {
        *Ipv4Address = _Ipv4Address;
    }

    VOID
    GetRemoteIpv4Address(
        _Out_ IN_ADDR *Ipv4Address
        ) const
    {
        GetIpv4Address(Ipv4Address);
        Ipv4Address->S_un.S_un_b.s_b4++;
    }

    VOID
    GetIpv6Address(
        _Out_ IN6_ADDR *Ipv6Address
        ) const
    {
        *Ipv6Address = _Ipv6Address;
    }

    VOID
    GetRemoteIpv6Address(
        _Out_ IN6_ADDR *Ipv6Address
        ) const
    {
        GetIpv6Address(Ipv6Address);
        Ipv6Address->u.Byte[sizeof(*Ipv6Address) - 1]++;
    }
/*
    VOID
    Restart() const
    {
        CHAR CmdBuff[256];
        RtlZeroMemory(CmdBuff, sizeof(CmdBuff));
        sprintf_s(CmdBuff, "%s /c Restart-NetAdapter -ifDesc \"%s\"", PowershellPrefix, _IfDesc);
        TEST_EQUAL(0, system(CmdBuff));
    }
*/
};

template<class T>
class Stopwatch {
private:
    LARGE_INTEGER _StartQpc;
    LARGE_INTEGER _FrequencyQpc;
    T _TimeoutInterval;

public:
    Stopwatch(
        _In_opt_ T TimeoutInterval = T::max()
        )
        :
        _TimeoutInterval(TimeoutInterval)
    {
        QueryPerformanceFrequency(&_FrequencyQpc);
        QueryPerformanceCounter(&_StartQpc);
    }

    T
    Elapsed()
    {
        LARGE_INTEGER End;
        UINT64 ElapsedQpc;

        QueryPerformanceCounter(&End);
        ElapsedQpc = End.QuadPart - _StartQpc.QuadPart;

        return T((ElapsedQpc * T::period::den) / T::period::num / _FrequencyQpc.QuadPart);
    }

    T
    Remaining()
    {
        return std::max(T(0), _TimeoutInterval - Elapsed());
    }

    bool
    IsExpired()
    {
        return Elapsed() >= _TimeoutInterval;
    }

    void
    ExpectElapsed(
        _In_ T ExpectedInterval,
        _In_opt_ UINT32 MarginPercent = 10
        )
    {
        T Fudge = (ExpectedInterval * MarginPercent) / 100;
        TEST_TRUE(MarginPercent == 0 || Fudge > T(0));
        TEST_TRUE(Elapsed() >= ExpectedInterval - Fudge);
        TEST_TRUE(Elapsed() <= ExpectedInterval + Fudge);
    }

    void
    Reset()
    {
        QueryPerformanceCounter(&_StartQpc);
    }

    void
    Reset(
        _In_ T TimeoutInterval
        )
    {
        _TimeoutInterval = TimeoutInterval;
        Reset();
    }
};
/*

static
VOID
InitializeOidKey(
    _Out_ OID_KEY *Key,
    _In_ NDIS_OID Oid,
    _In_ NDIS_REQUEST_TYPE RequestType,
    _In_opt_ OID_REQUEST_INTERFACE RequestInterface = OID_REQUEST_INTERFACE_REGULAR
    )
{
    RtlZeroMemory(Key, sizeof(*Key));
    Key->Oid = Oid;
    Key->RequestType = RequestType;
    Key->RequestInterface = RequestInterface;
}

static TestInterface FnMpIf(FNMP_IF_DESC, FNMP_IPV4_ADDRESS, FNMP_IPV6_ADDRESS);

struct RX_FRAME {
    DATA_FRAME Frame;
    DATA_BUFFER SingleBufferStorage;

    //
    // Need to explicitly state default constructor when deleting other
    // constructors.
    //
    RX_FRAME() = default;

    //
    // Delete the move and copy constructors. There are cases where a pointer
    // inside of Frame points directly to SingleBufferStorage, and any of these
    // constructors default implementations being called would break this
    // invariant.
    //
    RX_FRAME(const RX_FRAME&) = delete;
    RX_FRAME(RX_FRAME&&) = delete;

    RX_FRAME& operator=(const RX_FRAME&) = delete;
    RX_FRAME& operator=(RX_FRAME&&) = delete;
};

static
VOID
RxInitializeFrame(
    _Out_ RX_FRAME *Frame,
    _In_ UINT32 HashQueueId,
    _In_ DATA_BUFFER *Buffer
    )
{
    RtlZeroMemory(Frame, sizeof(*Frame));
    Frame->Frame.BufferCount = 1;
    Frame->Frame.Input.RssHashQueueId = HashQueueId;
    Frame->Frame.Buffers = Buffer;
}

static
VOID
RxInitializeFrame(
    _Out_ RX_FRAME *Frame,
    _In_ UINT32 HashQueueId,
    _In_ const UCHAR *FrameBuffer,
    _In_ UINT32 FrameLength
    )
{
    RtlZeroMemory(Frame, sizeof(*Frame));
    Frame->Frame.BufferCount = 1;
    Frame->Frame.Input.RssHashQueueId = HashQueueId;
    Frame->Frame.Buffers = nullptr;
    Frame->SingleBufferStorage.DataOffset = 0;
    Frame->SingleBufferStorage.DataLength = FrameLength;
    Frame->SingleBufferStorage.BufferLength = FrameLength;
    Frame->SingleBufferStorage.VirtualAddress = FrameBuffer;
    Frame->Frame.Buffers = &Frame->SingleBufferStorage;
}

static
unique_fnmp_handle
MpOpenShared(
    _In_ UINT32 IfIndex
    )
{
    unique_fnmp_handle Handle;
    TEST_FNMPAPI(FnMpOpenShared(IfIndex, &Handle));
    return Handle;
}

static
unique_fnmp_handle
MpOpenExclusive(
    _In_ UINT32 IfIndex
    )
{
    unique_fnmp_handle Handle;
    TEST_FNMPAPI(FnMpOpenExclusive(IfIndex, &Handle));
    return Handle;
}

[[nodiscard]]
static
FNMPAPI_STATUS
MpRxEnqueueFrame(
    _In_ const unique_fnmp_handle& Handle,
    _In_ RX_FRAME *RxFrame
    )
{
    return FnMpRxEnqueue(Handle.get(), &RxFrame->Frame);
}

[[nodiscard]]
static
FNMPAPI_STATUS
TryMpRxFlush(
    _In_ const unique_fnmp_handle& Handle,
    _In_opt_ DATA_FLUSH_OPTIONS *Options = nullptr
    )
{
    return FnMpRxFlush(Handle.get(), Options);
}

[[nodiscard]]
static
FNMPAPI_STATUS
MpRxIndicateFrame(
    _In_ const unique_fnmp_handle& Handle,
    _In_ RX_FRAME *RxFrame
    )
{
    FNMPAPI_STATUS Status = MpRxEnqueueFrame(Handle, RxFrame);
    if (!FNMPAPI_SUCCEEDED(Status)) {
        return Status;
    }
    return TryMpRxFlush(Handle);
}

static
VOID
MpTxFilter(
    _In_ const unique_fnmp_handle& Handle,
    _In_ const VOID *Pattern,
    _In_ const VOID *Mask,
    _In_ UINT32 Length
    )
{
    TEST_FNMPAPI(FnMpTxFilter(Handle.get(), Pattern, Mask, Length));
}

static
FNMPAPI_STATUS
MpTxGetFrame(
    _In_ const unique_fnmp_handle& Handle,
    _In_ UINT32 Index,
    _Inout_ UINT32 *FrameBufferLength,
    _Out_opt_ DATA_FRAME *Frame,
    _In_opt_ UINT32 SubIndex = 0
    )
{
    return FnMpTxGetFrame(Handle.get(), Index, SubIndex, FrameBufferLength, Frame);
}

static
unique_malloc_ptr<DATA_FRAME>
MpTxAllocateAndGetFrame(
    _In_ const unique_fnmp_handle& Handle,
    _In_ UINT32 Index,
    _In_opt_ UINT32 SubIndex = 0
    )
{
    unique_malloc_ptr<DATA_FRAME> FrameBuffer;
    UINT32 FrameLength = 0;
    FNMPAPI_STATUS Result;
    Stopwatch<std::chrono::milliseconds> Watchdog(TEST_TIMEOUT_ASYNC);

    //
    // Poll FNMP for TX: the driver doesn't support overlapped IO.
    //
    do {
        Result = MpTxGetFrame(Handle, Index, &FrameLength, NULL, SubIndex);
        if (Result != FNMPAPI_STATUS_NOT_FOUND) {
            break;
        }
    } while (Sleep(POLL_INTERVAL_MS), !Watchdog.IsExpired());

    TEST_EQUAL(FNMPAPI_STATUS_MORE_DATA, Result);
    TEST_TRUE(FrameLength >= sizeof(DATA_FRAME));
    FrameBuffer.reset((DATA_FRAME *)malloc(FrameLength));
    TEST_TRUE(FrameBuffer != NULL);

    TEST_FNMPAPI(MpTxGetFrame(Handle, Index, &FrameLength, FrameBuffer.get(), SubIndex));

    return FrameBuffer;
}

static
VOID
MpTxDequeueFrame(
    _In_ const unique_fnmp_handle& Handle,
    _In_ UINT32 Index
    )
{
    FNMPAPI_STATUS Result;
    Stopwatch<std::chrono::milliseconds> Watchdog(TEST_TIMEOUT_ASYNC);

    //
    // Poll FNMP for TX: the driver doesn't support overlapped IO.
    //
    do {
        Result = FnMpTxDequeueFrame(Handle.get(), Index);
    } while (!Watchdog.IsExpired() && Result == FNMPAPI_STATUS_NOT_FOUND);

    TEST_FNMPAPI(Result);
}

static
VOID
MpTxFlush(
    _In_ const unique_fnmp_handle& Handle
    )
{
    TEST_FNMPAPI(FnMpTxFlush(Handle.get()));
}

static
VOID
MpUpdateTaskOffload(
    _In_ const unique_fnmp_handle& Handle,
    _In_ FN_OFFLOAD_TYPE OffloadType,
    _In_opt_ const NDIS_OFFLOAD_PARAMETERS *OffloadParameters
    )
{
    UINT32 Size = OffloadParameters != NULL ? sizeof(*OffloadParameters) : 0;

    TEST_FNMPAPI(FnMpUpdateTaskOffload(Handle.get(), OffloadType, OffloadParameters, Size));
}

static
VOID
MpOidFilter(
    _In_ const unique_fnmp_handle& Handle,
    _In_ const OID_KEY *Keys,
    _In_ UINT32 KeyCount
    )
{
    TEST_FNMPAPI(FnMpOidFilter(Handle.get(), Keys, KeyCount));
}

static
FNMPAPI_STATUS
MpOidGetRequest(
    _In_ const unique_fnmp_handle& Handle,
    _In_ OID_KEY Key,
    _Inout_ UINT32 *InformationBufferLength,
    _Out_opt_ VOID *InformationBuffer
    )
{
    return FnMpOidGetRequest(Handle.get(), Key, InformationBufferLength, InformationBuffer);
}

template<typename T=decltype(TEST_TIMEOUT_ASYNC)>
static
unique_malloc_ptr<VOID>
MpOidAllocateAndGetRequest(
    _In_ const unique_fnmp_handle& Handle,
    _In_ OID_KEY Key,
    _Out_ UINT32 *InformationBufferLength,
    _In_opt_ T Timeout = TEST_TIMEOUT_ASYNC
    )
{
    unique_malloc_ptr<VOID> InformationBuffer;
    UINT32 Length = 0;
    FNMPAPI_STATUS Result;
    Stopwatch<T> Watchdog(Timeout);

    //
    // Poll FNMP for an OID: the driver doesn't support overlapped IO.
    //
    do {
        Result = MpOidGetRequest(Handle, Key, &Length, NULL);
        if (Result != FNMPAPI_STATUS_NOT_FOUND) {
            break;
        }
    } while (Sleep(POLL_INTERVAL_MS), !Watchdog.IsExpired());

    TEST_EQUAL(FNMPAPI_STATUS_MORE_DATA, Result);
    TEST_TRUE(Length > 0);
    InformationBuffer.reset(malloc(Length));
    TEST_TRUE(InformationBuffer != NULL);

    TEST_FNMPAPI(MpOidGetRequest(Handle, Key, &Length, InformationBuffer.get()));

    *InformationBufferLength = Length;
    return InformationBuffer;
}

static
unique_fnlwf_handle
LwfOpenDefault(
    _In_ UINT32 IfIndex
    )
{
    unique_fnlwf_handle Handle;
    FNLWFAPI_STATUS Result;

    //
    // The LWF may not be bound immediately after an adapter restart completes,
    // so poll for readiness.
    //

    Stopwatch<std::chrono::milliseconds> Watchdog(TEST_TIMEOUT_ASYNC);
    do {
        Result = FnLwfOpenDefault(IfIndex, &Handle);
        if (FNLWFAPI_SUCCEEDED(Result)) {
            break;
        } else {
            TEST_EQUAL(FNLWFAPI_STATUS_NOT_FOUND, Result);
        }
    } while (Sleep(POLL_INTERVAL_MS), !Watchdog.IsExpired());

    TEST_FNLWFAPI(Result);

    return Handle;
}

static
VOID
LwfTxEnqueue(
    _In_ const unique_fnlwf_handle& Handle,
    _In_ DATA_FRAME *Frame
    )
{
    TEST_FNLWFAPI(FnLwfTxEnqueue(Handle.get(), Frame));
}

static
VOID
LwfTxFlush(
    _In_ const unique_fnlwf_handle& Handle,
    _In_opt_ DATA_FLUSH_OPTIONS *Options = nullptr
    )
{
    FNLWFAPI_STATUS Result;
    Stopwatch<std::chrono::milliseconds> Watchdog(TEST_TIMEOUT_ASYNC);

    //
    // Retry if the interface is not ready: the NDIS data path may be paused.
    //
    do {
        Result = FnLwfTxFlush(Handle.get(), Options);
        if (Result != FNLWFAPI_STATUS_NOT_READY) {
            break;
        }
    } while (Sleep(POLL_INTERVAL_MS), !Watchdog.IsExpired());

    TEST_FNLWFAPI(Result);
}

static
VOID
LwfRxFilter(
    _In_ const unique_fnlwf_handle& Handle,
    _In_ const VOID *Pattern,
    _In_ const VOID *Mask,
    _In_ UINT32 Length
    )
{
    TEST_FNLWFAPI(FnLwfRxFilter(Handle.get(), Pattern, Mask, Length));
}

static
FNLWFAPI_STATUS
LwfRxGetFrame(
    _In_ const unique_fnlwf_handle& Handle,
    _In_ UINT32 Index,
    _Inout_ UINT32 *FrameBufferLength,
    _Out_opt_ DATA_FRAME *Frame
    )
{
    return FnLwfRxGetFrame(Handle.get(), Index, FrameBufferLength, Frame);
}

static
unique_malloc_ptr<DATA_FRAME>
LwfRxAllocateAndGetFrame(
    _In_ const unique_fnlwf_handle& Handle,
    _In_ UINT32 Index
    )
{
    unique_malloc_ptr<DATA_FRAME> FrameBuffer;
    UINT32 FrameLength = 0;
    FNLWFAPI_STATUS Result;
    Stopwatch<std::chrono::milliseconds> Watchdog(TEST_TIMEOUT_ASYNC);

    //
    // Poll FNLWF for RX: the driver doesn't support overlapped IO.
    //
    do {
        Result = LwfRxGetFrame(Handle, Index, &FrameLength, NULL);
        if (Result != FNLWFAPI_STATUS_NOT_FOUND) {
            break;
        }
    } while (Sleep(POLL_INTERVAL_MS), !Watchdog.IsExpired());

    TEST_EQUAL(FNLWFAPI_STATUS_MORE_DATA, Result);
    TEST_TRUE(FrameLength >= sizeof(DATA_FRAME));
    FrameBuffer.reset((DATA_FRAME *)malloc(FrameLength));
    TEST_TRUE(FrameBuffer != NULL);

    TEST_FNLWFAPI(LwfRxGetFrame(Handle, Index, &FrameLength, FrameBuffer.get()));

    return FrameBuffer;
}

static
VOID
LwfRxDequeueFrame(
    _In_ const unique_fnlwf_handle& Handle,
    _In_ UINT32 Index
    )
{
    FNLWFAPI_STATUS Result;
    Stopwatch<std::chrono::milliseconds> Watchdog(TEST_TIMEOUT_ASYNC);

    //
    // Poll FNLWF for RX: the driver doesn't support overlapped IO.
    //
    do {
        Result = FnLwfRxDequeueFrame(Handle.get(), Index);
    } while (!Watchdog.IsExpired() && Result == FNLWFAPI_STATUS_NOT_FOUND);

    TEST_FNLWFAPI(Result);
}

static
VOID
LwfRxFlush(
    _In_ const unique_fnlwf_handle& Handle
    )
{
    TEST_FNLWFAPI(FnLwfRxFlush(Handle.get()));
}

static
FNLWFAPI_STATUS
LwfOidSubmitRequest(
    _In_ const unique_fnlwf_handle& Handle,
    _In_ OID_KEY Key,
    _Inout_ UINT32 *InformationBufferLength,
    _Inout_opt_ VOID *InformationBuffer
    )
{
    return
        FnLwfOidSubmitRequest(
            Handle.get(), Key, InformationBufferLength, InformationBuffer);
}
*/
static
VOID
WaitForWfpQuarantine(
    _In_ const TestInterface& If
    );

static
unique_cxplat_socket
CreateUdpSocket(
    _In_ ADDRESS_FAMILY Af,
    _In_opt_ const TestInterface *If,
    _Out_ UINT16 *LocalPort
    )
{
    if (If != NULL) {
        //
        // Ensure the local UDP stack has finished initializing the interface.
        //
        WaitForWfpQuarantine(*If);
    }

    unique_cxplat_socket Socket;
    CxPlatSocketCreate(Af, SOCK_DGRAM, IPPROTO_UDP, &Socket);
    TEST_NOT_NULL(Socket.get());

    SOCKADDR_INET Address = {0};
    Address.si_family = Af;
    TEST_EQUAL(CXPLAT_STATUS_SUCCESS, CxPlatSocketBind(Socket.get(), (SOCKADDR *)&Address, sizeof(Address)));

    INT AddressLength = sizeof(Address);
    TEST_EQUAL(CXPLAT_STATUS_SUCCESS, CxPlatSocketGetSockName(Socket.get(), (SOCKADDR *)&Address, &AddressLength));

    INT TimeoutMs = TEST_TIMEOUT_ASYNC_MS;
    TEST_EQUAL(
        CXPLAT_STATUS_SUCCESS,
        CxPlatSocketSetSockOpt(Socket.get(), SOL_SOCKET, SO_RCVTIMEO, (CHAR *)&TimeoutMs, sizeof(TimeoutMs)));

    *LocalPort = SS_PORT(&Address);
    return Socket;
}
/*
static
VOID
WaitForWfpQuarantine(
    _In_ const TestInterface& If
    )
{
    //
    // Restarting the adapter churns WFP filter add/remove.
    // Ensure that our firewall rule is plumbed before we exit this test case.
    //
    UINT16 LocalPort, RemotePort;
    ETHERNET_ADDRESS LocalHw, RemoteHw;
    INET_ADDR LocalIp, RemoteIp;
    auto UdpSocket = CreateUdpSocket(AF_INET, NULL, &LocalPort);
    auto SharedMp = MpOpenShared(If.GetIfIndex());

    RemotePort = htons(1234);
    If.GetHwAddress(&LocalHw);
    If.GetRemoteHwAddress(&RemoteHw);
    If.GetIpv4Address(&LocalIp.Ipv4);
    If.GetRemoteIpv4Address(&RemoteIp.Ipv4);

    UCHAR UdpPayload[] = "WaitForWfpQuarantine";
    CHAR RecvPayload[sizeof(UdpPayload)];
    UCHAR UdpFrame[UDP_HEADER_STORAGE + sizeof(UdpPayload)];
    UINT32 UdpFrameLength = sizeof(UdpFrame);
    TEST_TRUE(
        PktBuildUdpFrame(
            UdpFrame, &UdpFrameLength, UdpPayload, sizeof(UdpPayload), &LocalHw,
            &RemoteHw, AF_INET, &LocalIp, &RemoteIp, LocalPort, RemotePort));

    //
    // On older Windows builds, WFP takes a very long time to de-quarantine.
    //
    Stopwatch<std::chrono::seconds> Watchdog(std::chrono::seconds(30));
    DWORD Bytes;
    do {
        RX_FRAME RxFrame;
        RxInitializeFrame(&RxFrame, If.GetQueueId(), UdpFrame, UdpFrameLength);
        if (SUCCEEDED(MpRxIndicateFrame(SharedMp, &RxFrame))) {
            Bytes = CxPlatSocketRecv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), 0);
        } else {
            Bytes = (DWORD)-1;
        }

        if (Bytes == sizeof(UdpPayload)) {
            break;
        }
    } while (Sleep(POLL_INTERVAL_MS), !Watchdog.IsExpired());
    TEST_EQUAL(Bytes, sizeof(UdpPayload));
}

VOID
SetSockAddr(
    _In_ PCSTR Addr,
    _In_ USHORT Port,
    _In_ ADDRESS_FAMILY Af,
    _Out_ PSOCKADDR_STORAGE SockAddr
    )
{
    SockAddr->ss_family = Af;
    SS_PORT(SockAddr) = htons(Port);
    if (Af == AF_INET6) {
        ULONG Scope = 0;
        IN6_ADDR In6 = {0};
        TEST_NTSTATUS(RtlIpv6StringToAddressExA(Addr, &In6, &Scope, &Port));
        RtlCopyMemory(&((PSOCKADDR_IN6)SockAddr)->sin6_addr, &In6, sizeof(IN6_ADDR));
    } else {
        IN_ADDR In = {0};
        TEST_NTSTATUS(RtlIpv4StringToAddressExA(Addr, TRUE, &In, &Port));
        ((PSOCKADDR_IN)SockAddr)->sin_addr.s_addr = In.s_addr;
    }
}

//
// Test framework agnostic test suite(s).
//
*/
EXTERN_C
bool
TestSetup()
{
    TEST_TRUE(CXPLAT_SUCCEEDED(CxPlatInitialize()));
    // TEST_EQUAL(0, InvokeSystem("netsh advfirewall firewall add rule name=fnmptest dir=in action=allow protocol=any remoteip=any localip=any"));
    TEST_FNMPAPI(FnMpLoadApi(&FnMpLoadApiContext));
    TEST_FNLWFAPI(FnLwfLoadApi(&FnLwfLoadApiContext));
    // WaitForWfpQuarantine(FnMpIf);
    return true;
}

EXTERN_C
bool
TestCleanup()
{
    FnLwfUnloadApi(FnLwfLoadApiContext);
    FnMpUnloadApi(FnMpLoadApiContext);
    // TEST_EQUAL(0, InvokeSystem("netsh advfirewall firewall delete rule name=fnmptest"));
    CxPlatUninitialize();
    return true;
}

EXTERN_C
VOID
MpBasicRx()
{
/*
    UINT16 LocalPort, RemotePort;
    ETHERNET_ADDRESS LocalHw, RemoteHw;
    INET_ADDR LocalIp, RemoteIp;
    auto UdpSocket = CreateUdpSocket(AF_INET, NULL, &LocalPort);
    auto SharedMp = MpOpenShared(FnMpIf.GetIfIndex());

    RemotePort = htons(1234);
    FnMpIf.GetHwAddress(&LocalHw);
    FnMpIf.GetRemoteHwAddress(&RemoteHw);
    FnMpIf.GetIpv4Address(&LocalIp.Ipv4);
    FnMpIf.GetRemoteIpv4Address(&RemoteIp.Ipv4);

    UCHAR UdpPayload[] = "BasicRx";
    CHAR RecvPayload[sizeof(UdpPayload)];
    UCHAR UdpFrame[UDP_HEADER_STORAGE + sizeof(UdpPayload)];
    UINT32 UdpFrameLength = sizeof(UdpFrame);
    TEST_TRUE(
        PktBuildUdpFrame(
            UdpFrame, &UdpFrameLength, UdpPayload, sizeof(UdpPayload), &LocalHw,
            &RemoteHw, AF_INET, &LocalIp, &RemoteIp, LocalPort, RemotePort));

    RX_FRAME RxFrame;
    RxInitializeFrame(&RxFrame, FnMpIf.GetQueueId(), UdpFrame, UdpFrameLength);
    TEST_FNMPAPI(MpRxIndicateFrame(SharedMp, &RxFrame));
    TEST_EQUAL(sizeof(UdpPayload), CxPlatSocketRecv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), 0));
    TEST_TRUE(RtlEqualMemory(UdpPayload, RecvPayload, sizeof(UdpPayload)));
//*/
}

EXTERN_C
VOID
MpBasicTx()
{
/*
    UINT16 LocalPort;
    auto UdpSocket = CreateUdpSocket(AF_INET, NULL, &LocalPort);
    auto SharedMp = MpOpenShared(FnMpIf.GetIfIndex());
    BOOLEAN Uso = TRUE;

    UCHAR UdpPayload[] = "BasicTx0BasicTx1";
    UCHAR Pattern[UDP_HEADER_BACKFILL(AF_INET) + sizeof(UdpPayload) / 2 - 1];
    UCHAR Mask[sizeof(Pattern)];
    UINT32 ExpectedUdpPayloadSize = sizeof(UdpPayload) / 2;
    UINT32 SendSize = sizeof(UdpPayload);

    RtlZeroMemory(Pattern, sizeof(Pattern));
    RtlCopyMemory(Pattern + UDP_HEADER_BACKFILL(AF_INET), UdpPayload, sizeof(UdpPayload) / 2 - 1);

    RtlZeroMemory(Mask, sizeof(Mask));
    for (int i = UDP_HEADER_BACKFILL(AF_INET); i < sizeof(Mask); i++) {
        Mask[i] = 0xff;
    }

    MpTxFilter(SharedMp, Pattern, Mask, sizeof(Pattern));

    SOCKADDR_STORAGE RemoteAddr;
    SetSockAddr(FNMP_NEIGHBOR_IPV4_ADDRESS, 1234, AF_INET, &RemoteAddr);

    if (CXPLAT_FAILED(CxPlatSocketSetSockOpt(UdpSocket.get(), IPPROTO_UDP, UDP_SEND_MSG_SIZE, &ExpectedUdpPayloadSize, sizeof(ExpectedUdpPayloadSize)))) {
        TEST_EQUAL(WSAEINVAL, CxPlatSocketGetLastError());
        Uso = FALSE;
        SendSize = ExpectedUdpPayloadSize;
    }

    TEST_EQUAL((int)SendSize, CxPlatSocketSendto(UdpSocket.get(), (PCHAR)UdpPayload, SendSize, 0, (PSOCKADDR)&RemoteAddr, sizeof(RemoteAddr)));

    auto MpTxFrame = MpTxAllocateAndGetFrame(SharedMp, 0);

    //
    // The following assumes the following MDL structure formed by the UDP TX path.
    //     MDL #1: Ethernet header + IP header + UDP header
    //     MDL #1: UDP payload
    //

    TEST_EQUAL(2, MpTxFrame->BufferCount);
    TEST_EQUAL(MpTxFrame->Buffers[0].DataLength, UDP_HEADER_BACKFILL(AF_INET));
    TEST_EQUAL(MpTxFrame->Buffers[1].DataLength, ExpectedUdpPayloadSize);

    CONST DATA_BUFFER *MpTxBuffer = &MpTxFrame->Buffers[1];
    TEST_TRUE(
        RtlEqualMemory(
            UdpPayload, MpTxBuffer->VirtualAddress + MpTxBuffer->DataOffset,
            ExpectedUdpPayloadSize));

    if (Uso) {
        MpTxFrame = MpTxAllocateAndGetFrame(SharedMp, 0, 1);

        TEST_EQUAL(2, MpTxFrame->BufferCount);
        TEST_EQUAL(MpTxFrame->Buffers[0].DataLength, UDP_HEADER_BACKFILL(AF_INET));
        TEST_EQUAL(MpTxFrame->Buffers[1].DataLength, ExpectedUdpPayloadSize);

        MpTxBuffer = &MpTxFrame->Buffers[1];
        TEST_TRUE(
            RtlEqualMemory(
                UdpPayload + ExpectedUdpPayloadSize,
                MpTxBuffer->VirtualAddress + MpTxBuffer->DataOffset,
                ExpectedUdpPayloadSize));
    }

    UINT32 FrameLength = 0;
    TEST_EQUAL(
        FNMPAPI_STATUS_NOT_FOUND,
        MpTxGetFrame(SharedMp, 0, &FrameLength, NULL, 3));

    MpTxDequeueFrame(SharedMp, 0);
    TEST_EQUAL(
        FNMPAPI_STATUS_NOT_FOUND,
        MpTxGetFrame(SharedMp, 0, &FrameLength, NULL, 0));
    TEST_EQUAL(
        FNMPAPI_STATUS_NOT_FOUND,
        MpTxGetFrame(SharedMp, 0, &FrameLength, NULL, 1));

    MpTxFlush(SharedMp);
*/
}
/*
static
VOID
InitializeOffloadParameters(
    _Out_ NDIS_OFFLOAD_PARAMETERS *OffloadParameters
    )
{
    ZeroMemory(OffloadParameters, sizeof(*OffloadParameters));
    OffloadParameters->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    OffloadParameters->Header.Size = NDIS_SIZEOF_OFFLOAD_PARAMETERS_REVISION_5;
    OffloadParameters->Header.Revision = NDIS_OFFLOAD_PARAMETERS_REVISION_5;
}
*/

EXTERN_C
VOID
MpBasicRxOffload()
{
/*
    UINT16 LocalPort, RemotePort;
    ETHERNET_ADDRESS LocalHw, RemoteHw;
    INET_ADDR LocalIp, RemoteIp;
    auto UdpSocket = CreateUdpSocket(AF_INET, NULL, &LocalPort);
    auto SharedMp = MpOpenShared(FnMpIf.GetIfIndex());
    auto MpTaskOffloadCleanup = wil::scope_exit([&]() {
        MpUpdateTaskOffload(SharedMp, FnOffloadCurrentConfig, NULL);
    });

    RemotePort = htons(1234);
    FnMpIf.GetHwAddress(&LocalHw);
    FnMpIf.GetRemoteHwAddress(&RemoteHw);
    FnMpIf.GetIpv4Address(&LocalIp.Ipv4);
    FnMpIf.GetRemoteIpv4Address(&RemoteIp.Ipv4);

    UCHAR UdpPayload[] = "BasicRxOffload";
    CHAR RecvPayload[sizeof(UdpPayload)];
    UCHAR UdpFrame[UDP_HEADER_STORAGE + sizeof(UdpPayload)];
    UINT32 UdpFrameLength = sizeof(UdpFrame);
    TEST_TRUE(
        PktBuildUdpFrame(
            UdpFrame, &UdpFrameLength, UdpPayload, sizeof(UdpPayload), &LocalHw,
            &RemoteHw, AF_INET, &LocalIp, &RemoteIp, LocalPort, RemotePort));

    RX_FRAME RxFrame;
    RxInitializeFrame(&RxFrame, FnMpIf.GetQueueId(), UdpFrame, UdpFrameLength);
    TEST_FNMPAPI(MpRxIndicateFrame(SharedMp, &RxFrame));
    TEST_EQUAL(sizeof(UdpPayload), CxPlatSocketRecv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), 0));
    TEST_TRUE(RtlEqualMemory(UdpPayload, RecvPayload, sizeof(UdpPayload)));

    NDIS_OFFLOAD_PARAMETERS OffloadParams;
    InitializeOffloadParameters(&OffloadParams);
    OffloadParams.UDPIPv4Checksum = NDIS_OFFLOAD_PARAMETERS_RX_ENABLED_TX_DISABLED;
    MpUpdateTaskOffload(SharedMp, FnOffloadCurrentConfig, &OffloadParams);

    //
    // Set the checksum succeeded OOB bit, and mangle the checksum. If the OOB
    // is being respected, the UDP datagram will still be delivered.
    //
    RxFrame.Frame.Input.Checksum.Receive.UdpChecksumSucceeded = TRUE;
    UDP_HDR *UdpHdr = (UDP_HDR *)&UdpFrame[UDP_HEADER_BACKFILL(AF_INET) - sizeof(*UdpHdr)];
    UdpHdr->uh_sum++;
    TEST_FNMPAPI(MpRxIndicateFrame(SharedMp, &RxFrame));
    TEST_EQUAL(sizeof(UdpPayload), CxPlatSocketRecv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), 0));
    TEST_TRUE(RtlEqualMemory(UdpPayload, RecvPayload, sizeof(UdpPayload)));
    RxFrame.Frame.Input.Checksum.Value = 0;
    UdpHdr->uh_sum--;
    MpUpdateTaskOffload(SharedMp, FnOffloadCurrentConfig, NULL);

    //
    // Try the same thing with the IP header.
    //
    InitializeOffloadParameters(&OffloadParams);
    OffloadParams.IPv4Checksum = NDIS_OFFLOAD_PARAMETERS_RX_ENABLED_TX_DISABLED;
    MpUpdateTaskOffload(SharedMp, FnOffloadCurrentConfig, &OffloadParams);

    RxFrame.Frame.Input.Checksum.Receive.IpChecksumSucceeded = TRUE;
    RxFrame.Frame.Input.Checksum.Receive.IpChecksumValueInvalid = TRUE;
    IPV4_HEADER *IpHdr = (IPV4_HEADER *)&UdpFrame[sizeof(ETHERNET_HEADER)];
    IpHdr->HeaderChecksum++;
    TEST_FNMPAPI(MpRxIndicateFrame(SharedMp, &RxFrame));
    TEST_EQUAL(sizeof(UdpPayload), CxPlatSocketRecv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), 0));
    TEST_TRUE(RtlEqualMemory(UdpPayload, RecvPayload, sizeof(UdpPayload)));
    RxFrame.Frame.Input.Checksum.Value = 0;
    IpHdr->HeaderChecksum--;
*/
}

EXTERN_C
VOID
MpBasicTxOffload()
{
/*
    UINT16 LocalPort;
    auto UdpSocket = CreateUdpSocket(AF_INET, NULL, &LocalPort);
    auto SharedMp = MpOpenShared(FnMpIf.GetIfIndex());
    auto MpTaskOffloadCleanup = wil::scope_exit([&]() {
        MpUpdateTaskOffload(SharedMp, FnOffloadCurrentConfig, NULL);
    });

    UCHAR UdpPayload[] = "BasicTxOffload";
    UCHAR Pattern[UDP_HEADER_BACKFILL(AF_INET) + sizeof(UdpPayload)];
    UCHAR Mask[sizeof(Pattern)];

    RtlZeroMemory(Pattern, sizeof(Pattern));
    RtlCopyMemory(Pattern + UDP_HEADER_BACKFILL(AF_INET), UdpPayload, sizeof(UdpPayload));

    RtlZeroMemory(Mask, sizeof(Mask));
    for (int i = UDP_HEADER_BACKFILL(AF_INET); i < sizeof(Mask); i++) {
        Mask[i] = 0xff;
    }

    MpTxFilter(SharedMp, Pattern, Mask, sizeof(Pattern));

    SOCKADDR_STORAGE RemoteAddr;
    SetSockAddr(FNMP_NEIGHBOR_IPV4_ADDRESS, 1234, AF_INET, &RemoteAddr);

    TEST_EQUAL(
        (int)sizeof(UdpPayload),
        CxPlatSocketSendto(
            UdpSocket.get(), (PCHAR)UdpPayload, sizeof(UdpPayload), 0,
            (PSOCKADDR)&RemoteAddr, sizeof(RemoteAddr)));

    auto MpTxFrame = MpTxAllocateAndGetFrame(SharedMp, 0);
    TEST_FALSE(MpTxFrame->Output.Checksum.Transmit.UdpChecksum);
    MpTxDequeueFrame(SharedMp, 0);
    MpTxFlush(SharedMp);

    NDIS_OFFLOAD_PARAMETERS OffloadParams;
    InitializeOffloadParameters(&OffloadParams);
    OffloadParams.UDPIPv4Checksum = NDIS_OFFLOAD_PARAMETERS_TX_ENABLED_RX_DISABLED;
    MpUpdateTaskOffload(SharedMp, FnOffloadCurrentConfig, &OffloadParams);

    TEST_EQUAL(
        (int)sizeof(UdpPayload),
        CxPlatSocketSendto(
            UdpSocket.get(), (PCHAR)UdpPayload, sizeof(UdpPayload), 0,
            (PSOCKADDR)&RemoteAddr, sizeof(RemoteAddr)));

    MpTxFrame = MpTxAllocateAndGetFrame(SharedMp, 0);
    TEST_TRUE(MpTxFrame->Output.Checksum.Transmit.UdpChecksum);
    MpTxDequeueFrame(SharedMp, 0);
    MpTxFlush(SharedMp);
*/
}

EXTERN_C
VOID
LwfBasicRx()
{
/*
    auto GenericMp = MpOpenShared(FnMpIf.GetIfIndex());
    auto DefaultLwf = LwfOpenDefault(FnMpIf.GetIfIndex());

    const UINT32 DataOffset = 3;
    const UCHAR Payload[] = "FnLwfRx";
    UINT64 Pattern = 0x2865A18EE4DB02F0ui64;
    UINT64 Mask = ~0ui64;
    const UINT32 BufferVaSize = DataOffset + sizeof(Pattern) + sizeof(Payload);
    UCHAR BufferVa[BufferVaSize];

    DATA_BUFFER Buffer = {0};
    Buffer.DataOffset = DataOffset;
    Buffer.DataLength = sizeof(Pattern) + sizeof(Payload);
    Buffer.BufferLength = BufferVaSize;
    Buffer.VirtualAddress = BufferVa;

    RtlCopyMemory(BufferVa + DataOffset, &Pattern, sizeof(Pattern));
    RtlCopyMemory(BufferVa + DataOffset + sizeof(Pattern), Payload, sizeof(Payload));

    LwfRxFilter(DefaultLwf, &Pattern, &Mask, sizeof(Pattern));

    RX_FRAME Frame;
    RxInitializeFrame(&Frame, FnMpIf.GetQueueId(), &Buffer);
    TEST_FNMPAPI(MpRxEnqueueFrame(GenericMp, &Frame));
    TEST_FNMPAPI(TryMpRxFlush(GenericMp));

    auto LwfRxFrame = LwfRxAllocateAndGetFrame(DefaultLwf, 0);
    TEST_EQUAL(LwfRxFrame->BufferCount, Frame.Frame.BufferCount);

    const DATA_BUFFER *LwfRxBuffer = &LwfRxFrame->Buffers[0];
    TEST_EQUAL(LwfRxBuffer->BufferLength, Buffer.BufferLength);
    TEST_EQUAL(LwfRxBuffer->DataOffset, Buffer.DataOffset);
    TEST_TRUE(
        RtlEqualMemory(
            Buffer.VirtualAddress + Buffer.DataOffset,
            LwfRxBuffer->VirtualAddress + LwfRxBuffer->DataOffset,
            Buffer.DataLength));

    LwfRxDequeueFrame(DefaultLwf, 0);
    LwfRxFlush(DefaultLwf);
*/
}

EXTERN_C
VOID
LwfBasicTx()
{
/*
    auto GenericMp = MpOpenShared(FnMpIf.GetIfIndex());
    auto DefaultLwf = LwfOpenDefault(FnMpIf.GetIfIndex());

    const UINT32 DataOffset = 3;
    const UCHAR Payload[] = "FnLwfTx";
    UINT64 Pattern = 0x39E8534AA85B4A98ui64;
    UINT64 Mask = ~0ui64;
    const UINT32 BufferVaSize = DataOffset + sizeof(Pattern) + sizeof(Payload);
    UCHAR BufferVa[BufferVaSize];

    DATA_FRAME Frame = {0};
    DATA_BUFFER Buffer = {0};
    Frame.BufferCount = 1;
    Buffer.DataOffset = DataOffset;
    Buffer.DataLength = sizeof(Pattern) + sizeof(Payload);
    Buffer.BufferLength = BufferVaSize;
    Buffer.VirtualAddress = BufferVa;
    Frame.Buffers = &Buffer;

    RtlCopyMemory(BufferVa + DataOffset, &Pattern, sizeof(Pattern));
    RtlCopyMemory(BufferVa + DataOffset + sizeof(Pattern), Payload, sizeof(Payload));

    MpTxFilter(GenericMp, &Pattern, &Mask, sizeof(Pattern));

    LwfTxEnqueue(DefaultLwf, &Frame);
    LwfTxFlush(DefaultLwf);

    auto MpTxFrame = MpTxAllocateAndGetFrame(GenericMp, 0);
    TEST_EQUAL(MpTxFrame->BufferCount, Frame.BufferCount);

    const DATA_BUFFER *MpTxBuffer = &MpTxFrame->Buffers[0];
    TEST_EQUAL(MpTxBuffer->BufferLength, Buffer.BufferLength);
    TEST_EQUAL(MpTxBuffer->DataOffset, Buffer.DataOffset);
    TEST_TRUE(
        RtlEqualMemory(
            Buffer.VirtualAddress + Buffer.DataOffset,
            MpTxBuffer->VirtualAddress + MpTxBuffer->DataOffset,
            Buffer.DataLength));

    MpTxDequeueFrame(GenericMp, 0);
    MpTxFlush(GenericMp);
*/
}

EXTERN_C
VOID
LwfBasicOid()
{
/*
    OID_KEY OidKeys[2];
    UINT32 MpInfoBufferLength;
    unique_malloc_ptr<VOID> MpInfoBuffer;
    UINT32 LwfInfoBufferLength;
    ULONG LwfInfoBuffer;
    ULONG OriginalPacketFilter = 0;
    auto DefaultLwf = LwfOpenDefault(FnMpIf.GetIfIndex());

    //
    // Get the existing packet filter from NDIS so we can tweak it to make sure
    // the set OID makes it to the miniport. N.B. this get OID is handled by
    // NDIS, not the miniport.
    //
    InitializeOidKey(&OidKeys[0], OID_GEN_CURRENT_PACKET_FILTER, NdisRequestQueryInformation);
    LwfInfoBufferLength = sizeof(OriginalPacketFilter);
    TEST_FNLWFAPI(
        LwfOidSubmitRequest(DefaultLwf, OidKeys[0], &LwfInfoBufferLength, &OriginalPacketFilter));

    //
    // Get.
    //
    InitializeOidKey(&OidKeys[0], OID_GEN_RECEIVE_BLOCK_SIZE, NdisRequestQueryInformation);

    //
    // Set.
    //
    InitializeOidKey(&OidKeys[1], OID_GEN_CURRENT_PACKET_FILTER, NdisRequestSetInformation);

    for (UINT32 Index = 0; Index < RTL_NUMBER_OF(OidKeys); Index++) {
        auto ExclusiveMp = MpOpenExclusive(FnMpIf.GetIfIndex());

        MpOidFilter(ExclusiveMp, &OidKeys[Index], 1);

        LwfInfoBuffer = OriginalPacketFilter ^ (0x00000001);
        LwfInfoBufferLength = sizeof(LwfInfoBuffer);
        auto AsyncThread = std::async(
            std::launch::async,
            [&] {
                return
                    LwfOidSubmitRequest(
                        DefaultLwf, OidKeys[Index], &LwfInfoBufferLength, &LwfInfoBuffer);
            }
        );

        MpInfoBuffer = MpOidAllocateAndGetRequest(ExclusiveMp, OidKeys[Index], &MpInfoBufferLength);
        ExclusiveMp.reset();

        TEST_EQUAL(AsyncThread.wait_for(TEST_TIMEOUT_ASYNC), std::future_status::ready);
        TEST_FNMPAPI(AsyncThread.get());

        TEST_EQUAL(LwfInfoBufferLength, sizeof(ULONG));
    }
*/
}