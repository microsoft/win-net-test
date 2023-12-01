//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include <chrono>
#include <cstdio>

// Windows and WIL includes need to be ordered in a certain way.
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <wil/resource.h>
#include <CppUnitTest.h>

#include <pkthlp.h>
#include <fnmpapi.h>
#include <fntrace.h>

#include "fnmptest.h"

#include "tests.tmh"

#define FNMP_IF_DESC "FNMP"
#define FNMP_IPV4_ADDRESS "192.168.200.1"
#define FNMP_NEIGHBOR_IPV4_ADDRESS "192.168.200.2"
#define FNMP_IPV6_ADDRESS "fc00::200:1"
#define FNMP_NEIGHBOR_IPV6_ADDRESS "fc00::200:2"

//
// A timeout value that allows for a little latency, e.g. async threads to
// execute.
//
#define TEST_TIMEOUT_ASYNC_MS 1000
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


template <typename T>
using unique_malloc_ptr = wistd::unique_ptr<T, wil::function_deleter<decltype(&::free), ::free>>;

static CONST CHAR *PowershellPrefix = "powershell -noprofile -ExecutionPolicy Bypass";

//
// Helper functions.
//

class TestInterface;

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
        HwAddress->Bytes[sizeof(_HwAddress) - 1]++;
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

    VOID
    Restart() const
    {
        CHAR CmdBuff[256];
        RtlZeroMemory(CmdBuff, sizeof(CmdBuff));
        sprintf_s(CmdBuff, "%s /c Restart-NetAdapter -ifDesc \"%s\"", PowershellPrefix, _IfDesc);
        TEST_EQUAL(0, system(CmdBuff));
    }
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
wil::unique_handle
MpOpenShared(
    _In_ UINT32 IfIndex
    )
{
    wil::unique_handle Handle;
    TEST_HRESULT(FnMpOpenShared(IfIndex, &Handle));
    return Handle;
}

[[nodiscard]]
static
HRESULT
MpRxEnqueueFrame(
    _In_ const wil::unique_handle& Handle,
    _In_ RX_FRAME *RxFrame
    )
{
    return FnMpRxEnqueue(Handle.get(), &RxFrame->Frame);
}

[[nodiscard]]
static
HRESULT
TryMpRxFlush(
    _In_ const wil::unique_handle& Handle,
    _In_opt_ DATA_FLUSH_OPTIONS *Options = nullptr
    )
{
    return FnMpRxFlush(Handle.get(), Options);
}

[[nodiscard]]
static
HRESULT
MpRxIndicateFrame(
    _In_ const wil::unique_handle& Handle,
    _In_ RX_FRAME *RxFrame
    )
{
    HRESULT Status = MpRxEnqueueFrame(Handle, RxFrame);
    if (!SUCCEEDED(Status)) {
        return Status;
    }
    return TryMpRxFlush(Handle);
}

static
VOID
MpTxFilter(
    _In_ const wil::unique_handle& Handle,
    _In_ const VOID *Pattern,
    _In_ const VOID *Mask,
    _In_ UINT32 Length
    )
{
    TEST_HRESULT(FnMpTxFilter(Handle.get(), Pattern, Mask, Length));
}

static
HRESULT
MpTxGetFrame(
    _In_ const wil::unique_handle& Handle,
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
    _In_ const wil::unique_handle& Handle,
    _In_ UINT32 Index,
    _In_opt_ UINT32 SubIndex = 0
    )
{
    unique_malloc_ptr<DATA_FRAME> FrameBuffer;
    UINT32 FrameLength = 0;
    HRESULT Result;
    Stopwatch<std::chrono::milliseconds> Watchdog(TEST_TIMEOUT_ASYNC);

    //
    // Poll FNMP for TX: the driver doesn't support overlapped IO.
    //
    do {
        Result = MpTxGetFrame(Handle, Index, &FrameLength, NULL, SubIndex);
        if (Result != HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) {
            break;
        }
    } while (Sleep(POLL_INTERVAL_MS), !Watchdog.IsExpired());

    TEST_EQUAL(HRESULT_FROM_WIN32(ERROR_MORE_DATA), Result);
    TEST_TRUE(FrameLength >= sizeof(DATA_FRAME));
    FrameBuffer.reset((DATA_FRAME *)malloc(FrameLength));
    TEST_TRUE(FrameBuffer != NULL);

    TEST_HRESULT(MpTxGetFrame(Handle, Index, &FrameLength, FrameBuffer.get(), SubIndex));

    return FrameBuffer;
}

static
VOID
MpTxDequeueFrame(
    _In_ const wil::unique_handle& Handle,
    _In_ UINT32 Index
    )
{
    HRESULT Result;
    Stopwatch<std::chrono::milliseconds> Watchdog(TEST_TIMEOUT_ASYNC);

    //
    // Poll FNMP for TX: the driver doesn't support overlapped IO.
    //
    do {
        Result = FnMpTxDequeueFrame(Handle.get(), Index);
    } while (!Watchdog.IsExpired() && Result == HRESULT_FROM_WIN32(ERROR_NOT_FOUND));

    TEST_HRESULT(Result);
}

static
VOID
MpTxFlush(
    _In_ const wil::unique_handle& Handle
    )
{
    TEST_HRESULT(FnMpTxFlush(Handle.get()));
}

static
VOID
WaitForWfpQuarantine(
    _In_ const TestInterface& If
    );

static
wil::unique_socket
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

    wil::unique_socket Socket(socket(Af, SOCK_DGRAM, IPPROTO_UDP));
    TEST_NOT_NULL(Socket.get());

    SOCKADDR_INET Address = {0};
    Address.si_family = Af;
    TEST_EQUAL(0, bind(Socket.get(), (SOCKADDR *)&Address, sizeof(Address)));

    INT AddressLength = sizeof(Address);
    TEST_EQUAL(0, getsockname(Socket.get(), (SOCKADDR *)&Address, &AddressLength));

    INT TimeoutMs = (INT)std::chrono::milliseconds(TEST_TIMEOUT_ASYNC).count();
    TEST_EQUAL(
        0,
        setsockopt(Socket.get(), SOL_SOCKET, SO_RCVTIMEO, (CHAR *)&TimeoutMs, sizeof(TimeoutMs)));

    *LocalPort = SS_PORT(&Address);
    return Socket;
}

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
            Bytes = recv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), 0);
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

bool
TestSetup()
{
    WSADATA WsaData;
    WPP_INIT_TRACING(NULL);
    TEST_EQUAL(0, WSAStartup(MAKEWORD(2,2), &WsaData));
    TEST_EQUAL(0, InvokeSystem("netsh advfirewall firewall add rule name=fnmptest dir=in action=allow protocol=any remoteip=any localip=any"));
    WaitForWfpQuarantine(FnMpIf);
    return true;
}

bool
TestCleanup()
{
    TEST_EQUAL(0, InvokeSystem("netsh advfirewall firewall delete rule name=fnmptest"));
    TEST_EQUAL(0, WSACleanup());
    WPP_CLEANUP();
    return true;
}

VOID
BasicRx()
{
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
    TEST_HRESULT(MpRxIndicateFrame(SharedMp, &RxFrame));
    TEST_EQUAL(sizeof(UdpPayload), recv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), 0));
    TEST_TRUE(RtlEqualMemory(UdpPayload, RecvPayload, sizeof(UdpPayload)));
}

VOID
BasicTx()
{
    UINT16 LocalPort;
    auto UdpSocket = CreateUdpSocket(AF_INET, NULL, &LocalPort);
    auto SharedMp = MpOpenShared(FnMpIf.GetIfIndex());
    BOOLEAN Uso = TRUE;

    UCHAR UdpPayload[] = "BasicTx0BasicTx1";
    UCHAR Pattern[UDP_HEADER_BACKFILL(AF_INET) + (sizeof(UdpPayload) - 1) / 2];
    UCHAR Mask[sizeof(Pattern)];
    UINT32 ExpectedUdpPayloadSize = sizeof(UdpPayload) / 2;
    UINT32 SendSize = sizeof(UdpPayload);

    RtlZeroMemory(Pattern, sizeof(Pattern));
    RtlCopyMemory(Pattern + UDP_HEADER_BACKFILL(AF_INET), UdpPayload, (sizeof(UdpPayload) - 1) / 2);

    RtlZeroMemory(Mask, sizeof(Mask));
    for (int i = UDP_HEADER_BACKFILL(AF_INET); i < sizeof(Mask); i++) {
        Mask[i] = 0xff;
    }

    MpTxFilter(SharedMp, Pattern, Mask, sizeof(Pattern));

    SOCKADDR_STORAGE RemoteAddr;
    SetSockAddr(FNMP_NEIGHBOR_IPV4_ADDRESS, 1234, AF_INET, &RemoteAddr);

    if (SOCKET_ERROR == WSASetUdpSendMessageSize(UdpSocket.get(), ExpectedUdpPayloadSize)) {
        TEST_EQUAL(WSAEINVAL, WSAGetLastError());
        Uso = FALSE;
        SendSize = ExpectedUdpPayloadSize;
    }

    TEST_EQUAL((int)SendSize, sendto(UdpSocket.get(), (PCHAR)UdpPayload, SendSize, 0, (PSOCKADDR)&RemoteAddr, sizeof(RemoteAddr)));

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
        HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
        MpTxGetFrame(SharedMp, 0, &FrameLength, NULL, 3));

    MpTxDequeueFrame(SharedMp, 0);
    TEST_EQUAL(
        HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
        MpTxGetFrame(SharedMp, 0, &FrameLength, NULL, 0));
    TEST_EQUAL(
        HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
        MpTxGetFrame(SharedMp, 0, &FrameLength, NULL, 1));

    MpTxFlush(SharedMp);
}

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

VOID
StopTest()
{
    Assert::Fail(L"Stop test execution.");
}

VOID
LogTestFailure(
    _In_z_ PCWSTR File,
    _In_z_ PCWSTR Function,
    INT Line,
    _Printf_format_string_ PCWSTR Format,
    ...
    )
{
    static const INT Size = 128;
    WCHAR Buffer[Size];

    UNREFERENCED_PARAMETER(File);
    UNREFERENCED_PARAMETER(Function);
    UNREFERENCED_PARAMETER(Line);

    va_list Args;
    va_start(Args, Format);
    _vsnwprintf_s(Buffer, Size, _TRUNCATE, Format, Args);
    va_end(Args);

    TraceError("%S", Buffer);
    Logger::WriteMessage(Buffer);
}

VOID
LogTestWarning(
    _In_z_ PCWSTR File,
    _In_z_ PCWSTR Function,
    INT Line,
    _Printf_format_string_ PCWSTR Format,
    ...
    )
{
    static const INT Size = 128;
    WCHAR Buffer[Size];

    UNREFERENCED_PARAMETER(File);
    UNREFERENCED_PARAMETER(Function);
    UNREFERENCED_PARAMETER(Line);

    va_list Args;
    va_start(Args, Format);
    _vsnwprintf_s(Buffer, Size, _TRUNCATE, Format, Args);
    va_end(Args);

    TraceWarn("%S", Buffer);
    Logger::WriteMessage(Buffer);
}

//
// Test framework specific.
//

TEST_MODULE_INITIALIZE(ModuleSetup)
{
    Assert::IsTrue(TestSetup());
}

TEST_MODULE_CLEANUP(ModuleCleanup)
{
    Assert::IsTrue(TestCleanup());
}

TEST_CLASS(fnmpfunctionaltests)
{
public:
    TEST_METHOD(BasicRx) {
        ::BasicRx();
    }

    TEST_METHOD(BasicTx) {
        ::BasicTx();
    }
};
