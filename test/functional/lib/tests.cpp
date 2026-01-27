//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#if defined(_KERNEL_MODE)
#include <ntddk.h>
#include <ntintsafe.h>
#include <ntstrsafe.h>
#include <ndis.h>
#else
// Windows and WIL includes need to be ordered in a certain way.
#include <winsock2.h>
#include <windows.h>
#endif

#pragma warning(push) // SAL issues in WIL header.
#pragma warning(disable:28157)
#pragma warning(disable:28158)
#pragma warning(disable:28167)
#pragma warning(disable:6001)
#include <wil/resource.h>
#pragma warning(pop)
#pragma warning(push)
#pragma warning(disable:4324) // structure was padded due to alignment specifier
#include <ws2def.h>
#include <ws2ipdef.h>
#include <netiodef.h>
#include <netioapi.h>
#include <mstcpip.h>
#pragma warning(pop)

#include <cxplat.h>
#include <fnsock.h>
#include <pkthlp.h>
#include <fnmpapi.h>
#include <fnlwfapi.h>
#if defined(_KERNEL_MODE)
#include <invokesystemrelay.h>
#endif
#include <qeo_ndis.h>
#include <fnoid.h>

#include "fntrace.h"
#include "fntest.h"
#include "tests.h"

#include "tests.tmh"

#define FNMP_IF_DESC "FNMP"
#define FNMP_IF_DESCW L"FNMP"
#define FNMP_IPV4_ADDRESS "192.168.200.1"
#define FNMP_NEIGHBOR_IPV4_ADDRESS "192.168.200.2"
#define FNMP_IPV6_ADDRESS "fc00::200:1"
#define FNMP_NEIGHBOR_IPV6_ADDRESS "fc00::200:2"

//
// A timeout value that allows for a little latency, e.g. async threads to
// execute.
//
#define TEST_TIMEOUT_ASYNC_MS 1000

//
// The expected maximum time needed for a network adapter to restart.
//
#define MP_RESTART_TIMEOUT_MS 15000

//
// Interval between polling attempts.
//
#define POLL_INTERVAL_MS 10
C_ASSERT(POLL_INTERVAL_MS * 5 <= TEST_TIMEOUT_ASYNC_MS);
C_ASSERT(POLL_INTERVAL_MS * 5 <= MP_RESTART_TIMEOUT_MS);

#define TEST_CXPLAT(condition) TEST_TRUE(CXPLAT_SUCCEEDED(condition))
#define TEST_CXPLAT_GOTO(condition, label) TEST_TRUE_GOTO(CXPLAT_SUCCEEDED(condition), label)
#define TEST_CXPLAT_RET(condition, retval) TEST_TRUE_RET(CXPLAT_SUCCEEDED(condition), retval)

#define POOL_TAG 'sTnF' // FnTs

template <typename T>
using unique_malloc_ptr = wistd::unique_ptr<T, wil::function_deleter<decltype(&::CxPlatFreeNoTag), ::CxPlatFreeNoTag>>;
using unique_fnmp_handle = wil::unique_any<FNMP_HANDLE, decltype(::FnMpClose), ::FnMpClose>;
using unique_fnlwf_handle = wil::unique_any<FNLWF_HANDLE, decltype(::FnLwfClose), ::FnLwfClose>;
using unique_fnsock_handle = wil::unique_any<FNSOCK_HANDLE, decltype(::FnSockClose), ::FnSockClose>;
using unique_cxplat_thread = wil::unique_any<CXPLAT_THREAD, decltype(::CxPlatThreadDelete), ::CxPlatThreadDelete>;

#if defined(_KERNEL_MODE)
#define TEST_FNMPAPI TEST_NTSTATUS
#define TEST_FNLWFAPI TEST_NTSTATUS
#define TEST_FNMPAPI_GOTO TEST_NTSTATUS_GOTO
#define TEST_FNLWFAPI_GOTO TEST_NTSTATUS_GOTO
#define TEST_FNMPAPI_RET TEST_NTSTATUS_RET
#define TEST_FNLWFAPI_RET TEST_NTSTATUS_RET
#if !defined(htons)
#define htons RtlUshortByteSwap
#define ntohs RtlUshortByteSwap
#define htonl RtlUlongByteSwap
#define ntohl RtlUlongByteSwap
#endif // !defined(htons)
#define system(x) InvokeSystemRelay(x)
#else
#define TEST_FNMPAPI TEST_HRESULT
#define TEST_FNLWFAPI TEST_HRESULT
#define TEST_FNMPAPI_GOTO TEST_HRESULT_GOTO
#define TEST_FNLWFAPI_GOTO TEST_HRESULT_GOTO
#define TEST_FNMPAPI_RET TEST_HRESULT_RET
#define TEST_FNLWFAPI_RET TEST_HRESULT_RET
#define RtlStringCbPrintfA(Dst, DstSize, Format, ...) \
    sprintf_s(Dst, Format, __VA_ARGS__)
#endif // defined(_KERNEL_MODE)

static CONST CHAR *PowershellPrefix = "powershell -noprofile -ExecutionPolicy Bypass";
static CONST CHAR *FirewallAddRuleString = "netsh advfirewall firewall add rule name=fnmptest dir=in action=allow protocol=any remoteip=any localip=any";
static CONST CHAR *FirewallDeleteRuleString = "netsh advfirewall firewall delete rule name=fnmptest";
static FNMP_LOAD_API_CONTEXT FnMpLoadApiContext;
static FNLWF_LOAD_API_CONTEXT FnLwfLoadApiContext;

//
// Helper functions.
//

static
INT
InvokeSystem(
    _In_z_ const CHAR *Command
    )
{
    INT Result;

    TraceVerbose("system(%s)", Command);
    Result = system(Command);
    TraceVerbose("%d returned by system(%s)", Result, Command);

    return Result;
}

typedef struct TestInterface {
private:
    CONST CHAR *_IfDesc;
    CONST WCHAR *_IfDescW;
    mutable UINT32 _IfIndex;
    mutable UCHAR _HwAddress[sizeof(ETHERNET_ADDRESS)]{ 0 };
    IN_ADDR _Ipv4Address;
    IN6_ADDR _Ipv6Address;

    bool
    Query() const
    {
        MIB_IF_TABLE2 *IfTable = NULL;

        if (ReadUInt32Acquire(&_IfIndex) != NET_IFINDEX_UNSPECIFIED) {
            return true;
        }

        //
        // Get information on all adapters.
        //
        TEST_EQUAL_RET((ULONG)NO_ERROR, GetIfTable2Ex(MibIfTableNormal, &IfTable), false);
        TEST_NOT_NULL_RET(IfTable, false);

        auto ScopeGuard = wil::scope_exit([&]
        {
            FreeMibTable(IfTable);
        });

        //
        // Search for the test adapter.
        //
        for (ULONG i = 0; i < IfTable->NumEntries; i++) {
            MIB_IF_ROW2 *Row = &IfTable->Table[i];
            if (!wcscmp(Row->Description, _IfDescW)) {
                TEST_EQUAL_RET(sizeof(_HwAddress), Row->PhysicalAddressLength, false);
                RtlCopyMemory(_HwAddress, Row->PhysicalAddress, sizeof(_HwAddress));

                WriteUInt32Release(&_IfIndex, Row->InterfaceIndex);
            }
        }

        TEST_NOT_EQUAL_RET(NET_IFINDEX_UNSPECIFIED, _IfIndex, false);
        return true;
    }

public:

    bool
    Initialize(
        _In_z_ CONST CHAR *IfDesc,
        _In_z_ CONST WCHAR *IfDescW,
        _In_z_ CONST CHAR *Ipv4Address,
        _In_z_ CONST CHAR *Ipv6Address
        )
    {
        CONST CHAR *Terminator;

        _IfDesc = IfDesc;
        _IfDescW = IfDescW;
        _IfIndex = NET_IFINDEX_UNSPECIFIED;

        TEST_NTSTATUS_RET(
            RtlIpv4StringToAddressA(Ipv4Address, FALSE, &Terminator, &_Ipv4Address), false);
        TEST_NTSTATUS_RET(
            RtlIpv6StringToAddressA(Ipv6Address, &Terminator, &_Ipv6Address), false);
        TEST_TRUE_RET(Query(), false);
        return true;
    }

    CONST CHAR*
    GetIfDesc() const
    {
        return _IfDesc;
    }

    NET_IFINDEX
    GetIfIndex() const
    {
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

    bool
    Restart() const
    {
        CHAR CmdBuff[256];
        RtlZeroMemory(CmdBuff, sizeof(CmdBuff));
        RtlStringCbPrintfA(
            CmdBuff, sizeof(CmdBuff), "%s /c Restart-NetAdapter -ifDesc \"%s\"",
            PowershellPrefix, _IfDesc);
        TEST_EQUAL_RET(0, InvokeSystem(CmdBuff), false);
        return true;
    }

} TestInterface;

class Stopwatch {
private:
    UINT64 _StartQpc;
    UINT64 _TimeoutInterval;

public:
    Stopwatch(
        _In_opt_ UINT64 TimeoutInterval = MAXUINT64
        )
        :
        _TimeoutInterval(TimeoutInterval)
    {
        _StartQpc = CxPlatTimePlat();
    }

    UINT64
    Elapsed()
    {
        UINT64 End;
        UINT64 ElapsedQpc;

        End = CxPlatTimePlat();
        ElapsedQpc = End - _StartQpc;

        return US_TO_MS(CxPlatTimePlatToUs64(ElapsedQpc));
    }

    UINT64
    Remaining()
    {
        UINT64 Remaining = _TimeoutInterval - Elapsed();

        if (Remaining > _TimeoutInterval) {
            return 0;
        } else {
            return Remaining;
        }
    }

    bool
    IsExpired()
    {
        return Elapsed() >= _TimeoutInterval;
    }

    bool
    ExpectElapsed(
        _In_ UINT64 ExpectedInterval,
        _In_opt_ UINT32 MarginPercent = 10
        )
    {
        UINT64 Fudge = (ExpectedInterval * MarginPercent) / 100;
        TEST_TRUE_RET(MarginPercent == 0 || Fudge > 0, false);
        TEST_TRUE_RET(Elapsed() >= ExpectedInterval - Fudge, false);
        TEST_TRUE_RET(Elapsed() <= ExpectedInterval + Fudge, false);
        return true;
    }

    void
    Reset()
    {
        _StartQpc = CxPlatTimePlat();
    }

    void
    Reset(
        _In_ UINT64 TimeoutInterval
        )
    {
        _TimeoutInterval = TimeoutInterval;
        Reset();
    }
};

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

static TestInterface *FnMpIf;

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
    TEST_FNMPAPI_RET(FnMpOpenShared(IfIndex, &Handle), Handle);
    return Handle;
}

static
unique_fnmp_handle
MpOpenExclusive(
    _In_ UINT32 IfIndex
    )
{
    unique_fnmp_handle Handle;
    TEST_FNMPAPI_RET(FnMpOpenExclusive(IfIndex, &Handle), Handle);
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
bool
MpTxFilter(
    _In_ const unique_fnmp_handle& Handle,
    _In_opt_bytecount_(Length) const VOID *Pattern,
    _In_opt_bytecount_(Length) const VOID *Mask,
    _In_ UINT32 Length
    )
{
    TEST_FNMPAPI_RET(FnMpTxFilter(Handle.get(), Pattern, Mask, Length), false);
    return true;
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
    UINT32 FrameLength;
    FNMPAPI_STATUS Result;
    Stopwatch Watchdog(TEST_TIMEOUT_ASYNC_MS);

    //
    // Poll FNMP for TX: the driver doesn't support overlapped IO.
    //
    do {
        FrameLength = 0;
        Result = MpTxGetFrame(Handle, Index, &FrameLength, NULL, SubIndex);
        if (Result != FNMPAPI_STATUS_NOT_FOUND) {
            break;
        }
    } while (CxPlatSleep(POLL_INTERVAL_MS), !Watchdog.IsExpired());

    TEST_EQUAL_RET(FNMPAPI_STATUS_MORE_DATA, Result, FrameBuffer);
    TEST_TRUE_RET(FrameLength >= sizeof(DATA_FRAME), FrameBuffer);
    FrameBuffer.reset((DATA_FRAME *)CxPlatAllocNonPaged(FrameLength, POOL_TAG));
    TEST_TRUE_RET(FrameBuffer != NULL, FrameBuffer);

    if (FNMPAPI_FAILED(MpTxGetFrame(Handle, Index, &FrameLength, FrameBuffer.get(), SubIndex))) {
        FrameBuffer.reset();
    }

    return FrameBuffer;
}

static
FNMPAPI_STATUS
MpTxSetFrame(
    _In_ const unique_fnmp_handle& Handle,
    _In_ UINT32 Index,
    _In_ DATA_FRAME *Frame,
    _In_opt_ UINT32 SubIndex = 0
    )
{
    return FnMpTxSetFrame(Handle.get(), Index, SubIndex, Frame);
}

static
bool
MpTxDequeueFrame(
    _In_ const unique_fnmp_handle& Handle,
    _In_ UINT32 Index
    )
{
    FNMPAPI_STATUS Result;
    Stopwatch Watchdog(TEST_TIMEOUT_ASYNC_MS);

    //
    // Poll FNMP for TX: the driver doesn't support overlapped IO.
    //
    do {
        Result = FnMpTxDequeueFrame(Handle.get(), Index);
    } while (!Watchdog.IsExpired() && Result == FNMPAPI_STATUS_NOT_FOUND);

    TEST_FNMPAPI_RET(Result, false);
    return true;
}

static
bool
MpTxFlush(
    _In_ const unique_fnmp_handle& Handle
    )
{
    TEST_FNMPAPI_RET(FnMpTxFlush(Handle.get()), false);
    return true;
}

static
bool
MpUpdateTaskOffload(
    _In_ const unique_fnmp_handle& Handle,
    _In_ FN_OFFLOAD_TYPE OffloadType,
    _In_opt_ const NDIS_OFFLOAD_PARAMETERS *OffloadParameters
    )
{
    UINT32 Size = OffloadParameters != NULL ? sizeof(*OffloadParameters) : 0;

    TEST_FNMPAPI_RET(
        FnMpUpdateTaskOffload(Handle.get(), OffloadType, OffloadParameters, Size),
        false);
    return true;
}

static
bool
MpOidFilter(
    _In_ const unique_fnmp_handle& Handle,
    _In_ const OID_KEY *Keys,
    _In_ UINT32 KeyCount
    )
{
    TEST_FNMPAPI_RET(FnMpOidFilter(Handle.get(), Keys, KeyCount), false);
    return true;
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

static
unique_malloc_ptr<VOID>
MpOidAllocateAndGetRequest(
    _In_ const unique_fnmp_handle& Handle,
    _In_ OID_KEY Key,
    _Out_ UINT32 *InformationBufferLength,
    _In_opt_ UINT32 Timeout = TEST_TIMEOUT_ASYNC_MS
    )
{
    unique_malloc_ptr<VOID> InformationBuffer;
    UINT32 Length;
    FNMPAPI_STATUS Result;
    Stopwatch Watchdog(Timeout);

    //
    // Poll FNMP for an OID: the driver doesn't support overlapped IO.
    //
    do {
        Length = 0;
        Result = MpOidGetRequest(Handle, Key, &Length, NULL);
        if (Result != FNMPAPI_STATUS_NOT_FOUND) {
            break;
        }
    } while (CxPlatSleep(POLL_INTERVAL_MS), !Watchdog.IsExpired());

    TEST_EQUAL_RET(FNMPAPI_STATUS_MORE_DATA, Result, InformationBuffer);
    TEST_TRUE_RET(Length > 0, InformationBuffer);
    InformationBuffer.reset(CxPlatAllocNonPaged(Length, POOL_TAG));
    TEST_TRUE_RET(InformationBuffer != NULL, InformationBuffer);

    if (FNMPAPI_FAILED(MpOidGetRequest(Handle, Key, &Length, InformationBuffer.get()))) {
        InformationBuffer.reset();
    }

    *InformationBufferLength = Length;
    return InformationBuffer;
}

static
bool
MpOidCompleteRequest(
    _In_ const unique_fnmp_handle& Handle,
    _In_ OID_KEY Key,
    _In_ NDIS_STATUS CompletionStatus,
    _In_opt_ VOID *InformationBuffer,
    _In_ UINT32 InformationBufferLength
    )
{
    TEST_FNMPAPI_RET(
        FnMpOidCompleteRequest(
            Handle.get(), Key, CompletionStatus, InformationBuffer, InformationBufferLength),
        false);
    return true;
}

[[nodiscard]]
static
FNMPAPI_STATUS
MpAllocatePort(
    _In_ const unique_fnmp_handle& Handle,
    _Out_ NDIS_PORT_NUMBER *PortNumber
    )
{
    return FnMpAllocatePort(Handle.get(), PortNumber);
}

[[nodiscard]]
static
FNMPAPI_STATUS
MpFreePort(
    _In_ const unique_fnmp_handle& Handle,
    _In_ NDIS_PORT_NUMBER PortNumber
    )
{
    return FnMpFreePort(Handle.get(), PortNumber);
}

[[nodiscard]]
static
FNMPAPI_STATUS
MpActivatePort(
    _In_ const unique_fnmp_handle& Handle,
    _In_ NDIS_PORT_NUMBER PortNumber
    )
{
    return FnMpActivatePort(Handle.get(), PortNumber);
}

[[nodiscard]]
static
FNMPAPI_STATUS
MpDeactivatePort(
    _In_ const unique_fnmp_handle& Handle,
    _In_ NDIS_PORT_NUMBER PortNumber
    )
{
    return FnMpDeactivatePort(Handle.get(), PortNumber);
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

    Stopwatch Watchdog(TEST_TIMEOUT_ASYNC_MS);
    do {
        Result = FnLwfOpenDefault(IfIndex, &Handle);
        if (FNLWFAPI_SUCCEEDED(Result)) {
            break;
        } else {
            TEST_EQUAL_RET(FNLWFAPI_STATUS_NOT_FOUND, Result, Handle);
        }
    } while (CxPlatSleep(POLL_INTERVAL_MS), !Watchdog.IsExpired());

    TEST_FNLWFAPI_RET(Result, Handle);

    return Handle;
}

static
bool
LwfTxEnqueue(
    _In_ const unique_fnlwf_handle& Handle,
    _In_ DATA_FRAME *Frame
    )
{
    TEST_FNLWFAPI_RET(FnLwfTxEnqueue(Handle.get(), Frame), false);
    return true;
}

static
bool
LwfTxFlush(
    _In_ const unique_fnlwf_handle& Handle,
    _In_opt_ DATA_FLUSH_OPTIONS *Options = nullptr
    )
{
    FNLWFAPI_STATUS Result;
    Stopwatch Watchdog(TEST_TIMEOUT_ASYNC_MS);

    //
    // Retry if the interface is not ready: the NDIS data path may be paused.
    //
    do {
        Result = FnLwfTxFlush(Handle.get(), Options);
        if (Result != FNLWFAPI_STATUS_NOT_READY) {
            break;
        }
    } while (CxPlatSleep(POLL_INTERVAL_MS), !Watchdog.IsExpired());

    TEST_FNLWFAPI_RET(Result, false);
    return true;
}

static
bool
LwfRxFilter(
    _In_ const unique_fnlwf_handle& Handle,
    _In_ const VOID *Pattern,
    _In_ const VOID *Mask,
    _In_ UINT32 Length
    )
{
    TEST_FNLWFAPI_RET(FnLwfRxFilter(Handle.get(), Pattern, Mask, Length), false);
    return true;
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
    UINT32 FrameLength;
    FNLWFAPI_STATUS Result;
    Stopwatch Watchdog(TEST_TIMEOUT_ASYNC_MS);

    //
    // Poll FNLWF for RX: the driver doesn't support overlapped IO.
    //
    do {
        FrameLength = 0;
        Result = LwfRxGetFrame(Handle, Index, &FrameLength, NULL);
        if (Result != FNLWFAPI_STATUS_NOT_FOUND) {
            break;
        }
    } while (CxPlatSleep(POLL_INTERVAL_MS), !Watchdog.IsExpired());

    TEST_EQUAL_RET(FNLWFAPI_STATUS_MORE_DATA, Result, FrameBuffer);
    TEST_TRUE_RET(FrameLength >= sizeof(DATA_FRAME), FrameBuffer);
    FrameBuffer.reset((DATA_FRAME *)CxPlatAllocNonPaged(FrameLength, POOL_TAG));
    TEST_TRUE_RET(FrameBuffer.get() != NULL, FrameBuffer);

    if (FNLWFAPI_FAILED(LwfRxGetFrame(Handle, Index, &FrameLength, FrameBuffer.get()))) {
        FrameBuffer.reset();
    }

    return FrameBuffer;
}

static
bool
LwfRxDequeueFrame(
    _In_ const unique_fnlwf_handle& Handle,
    _In_ UINT32 Index
    )
{
    FNLWFAPI_STATUS Result;
    Stopwatch Watchdog(TEST_TIMEOUT_ASYNC_MS);

    //
    // Poll FNLWF for RX: the driver doesn't support overlapped IO.
    //
    do {
        Result = FnLwfRxDequeueFrame(Handle.get(), Index);
    } while (!Watchdog.IsExpired() && Result == FNLWFAPI_STATUS_NOT_FOUND);

    TEST_FNLWFAPI_RET(Result, false);
    return true;
}

static
bool
LwfRxFlush(
    _In_ const unique_fnlwf_handle& Handle
    )
{
    TEST_FNLWFAPI_RET(FnLwfRxFlush(Handle.get()), false);
    return true;
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


typedef struct LWF_OID_SUBMIT_REQUEST {
    FNLWF_HANDLE Handle;
    OID_KEY OidKey;
    UINT32 *InfoBufferLength;
    VOID *InfoBuffer;
    FNLWFAPI_STATUS Status;
} LWF_OID_SUBMIT_REQUEST;

CXPLAT_THREAD_RETURN_TYPE
LwfOidSubmitRequestFn(
    _In_ VOID* Context
    )
{
    LWF_OID_SUBMIT_REQUEST *Req = (LWF_OID_SUBMIT_REQUEST *)Context;
    Req->Status =
        FnLwfOidSubmitRequest(
            Req->Handle, Req->OidKey, Req->InfoBufferLength, Req->InfoBuffer);
    CXPLAT_THREAD_RETURN(0);
}

template <typename T>
static
unique_malloc_ptr<T>
LwfOidAllocateAndSubmitRequest(
    _In_ const unique_fnlwf_handle &Handle,
    _In_ OID_KEY Key,
    _Out_ UINT32 *BytesReturned
    )
{
    unique_malloc_ptr<T> InformationBuffer;
    UINT32 InformationBufferLength = 0;
    FNLWFAPI_STATUS Result;

    Result = LwfOidSubmitRequest(Handle, Key, &InformationBufferLength, NULL);
    TEST_EQUAL_RET(FNLWFAPI_STATUS_MORE_DATA, Result, InformationBuffer);
    TEST_TRUE_RET(InformationBufferLength > 0, InformationBuffer);

    InformationBuffer.reset((T *)CxPlatAllocNonPaged(InformationBufferLength, POOL_TAG));
    TEST_NOT_NULL_RET(InformationBuffer.get(), InformationBuffer);

    Result = LwfOidSubmitRequest(Handle, Key, &InformationBufferLength, InformationBuffer.get());
    TEST_FNLWFAPI_RET(Result, InformationBuffer);
    TEST_TRUE_RET(InformationBufferLength > 0, InformationBuffer);
    *BytesReturned = InformationBufferLength;

    return InformationBuffer;
}

static
bool
WaitForWfpQuarantine(
    _In_ const TestInterface *If
    );

static
unique_fnsock_handle
CreateUdpSocket(
    _In_ ADDRESS_FAMILY Af,
    _In_opt_ const TestInterface *If,
    _Out_ UINT16 *LocalPort
    )
{
    unique_fnsock_handle Socket;

    *LocalPort = 0;

    if (If != NULL) {
        //
        // Ensure the local UDP stack has finished initializing the interface.
        //
        TEST_TRUE_RET(WaitForWfpQuarantine(If), Socket);
    }

    FnSockCreate(Af, SOCK_DGRAM, IPPROTO_UDP, &Socket);
    TEST_NOT_NULL_RET(Socket.get(), Socket);

    SOCKADDR_INET Address = {0};
    Address.si_family = Af;
    TEST_CXPLAT_RET(
        FnSockBind(Socket.get(), (SOCKADDR *)&Address, sizeof(Address)),
        Socket);

    INT AddressLength = sizeof(Address);
    TEST_CXPLAT_RET(
        FnSockGetSockName(Socket.get(), (SOCKADDR *)&Address, &AddressLength),
        Socket);

    INT TimeoutMs = TEST_TIMEOUT_ASYNC_MS;
    TEST_CXPLAT_RET(
        FnSockSetSockOpt(
            Socket.get(), SOL_SOCKET, SO_RCVTIMEO, (CHAR *)&TimeoutMs, sizeof(TimeoutMs)),
        Socket);

    *LocalPort = SS_PORT(&Address);
    return Socket;
}

static
bool
WaitForWfpQuarantine(
    _In_ const TestInterface *If
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
    auto SharedMp = MpOpenShared(If->GetIfIndex());

    TEST_NOT_NULL_RET(UdpSocket.get(), false);
    TEST_NOT_NULL_RET(SharedMp.get(), false);

    RemotePort = htons(1234);
    If->GetHwAddress(&LocalHw);
    If->GetRemoteHwAddress(&RemoteHw);
    If->GetIpv4Address(&LocalIp.Ipv4);
    If->GetRemoteIpv4Address(&RemoteIp.Ipv4);

    UCHAR UdpPayload[] = "WaitForWfpQuarantine";
    CHAR RecvPayload[sizeof(UdpPayload)];
    UCHAR UdpFrame[UDP_HEADER_STORAGE + sizeof(UdpPayload)];
    UINT32 UdpFrameLength = sizeof(UdpFrame);
    TEST_TRUE_RET(
        PktBuildUdpFrame(
            UdpFrame, &UdpFrameLength, UdpPayload, sizeof(UdpPayload), &LocalHw,
            &RemoteHw, AF_INET, &LocalIp, &RemoteIp, LocalPort, RemotePort),
        false);

    //
    // On older Windows builds, WFP takes a very long time to de-quarantine.
    //
    Stopwatch Watchdog(30 * 1000);
    DWORD Bytes;
    do {
        RX_FRAME RxFrame;
        RxInitializeFrame(&RxFrame, If->GetQueueId(), UdpFrame, UdpFrameLength);
        if (SUCCEEDED(MpRxIndicateFrame(SharedMp, &RxFrame))) {
            Bytes = FnSockRecv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), FALSE, 0);
        } else {
            Bytes = (DWORD)-1;
        }

        if (Bytes == sizeof(UdpPayload)) {
            break;
        }
    } while (CxPlatSleep(POLL_INTERVAL_MS), !Watchdog.IsExpired());
    TEST_EQUAL_RET(Bytes, sizeof(UdpPayload), false);

    return true;
}

bool
SetSockAddr(
    _In_ PCSTR Addr,
    _In_ USHORT Port,
    _In_ ADDRESS_FAMILY Af,
    _Out_ PSOCKADDR_STORAGE SockAddr
    )
{
    RtlZeroMemory(SockAddr, sizeof(*SockAddr));
    SockAddr->ss_family = Af;
    SS_PORT(SockAddr) = htons(Port);
    if (Af == AF_INET6) {
        ULONG Scope = 0;
        IN6_ADDR In6 = {0};
        TEST_NTSTATUS_RET(RtlIpv6StringToAddressExA(Addr, &In6, &Scope, &Port), false);
        RtlCopyMemory(&((PSOCKADDR_IN6)SockAddr)->sin6_addr, &In6, sizeof(IN6_ADDR));
    } else {
        IN_ADDR In = {0};
        TEST_NTSTATUS_RET(RtlIpv4StringToAddressExA(Addr, TRUE, &In, &Port), false);
        ((PSOCKADDR_IN)SockAddr)->sin_addr.s_addr = In.s_addr;
    }
    return true;
}

//
// Test framework agnostic test suite(s).
//

EXTERN_C
bool
TestSetup()
{
    BOOLEAN FirewallInitialized = FALSE;
    BOOLEAN CxPlatInitialized = FALSE;
    BOOLEAN FnSockInitialized = FALSE;
    BOOLEAN FnMpApiInitialized = FALSE;
    BOOLEAN FnLwfApiInitialized = FALSE;

    TEST_EQUAL_GOTO(0, InvokeSystem(FirewallAddRuleString), Error);
    FirewallInitialized = TRUE;

    TEST_TRUE_GOTO(CXPLAT_SUCCEEDED(CxPlatInitialize()), Error);
    CxPlatInitialized = TRUE;

    TEST_TRUE_GOTO(CXPLAT_SUCCEEDED(FnSockInitialize()), Error);
    FnSockInitialized = TRUE;

    TEST_FNMPAPI_GOTO(FnMpLoadApi(&FnMpLoadApiContext), Error);
    FnMpApiInitialized = TRUE;

    TEST_FNLWFAPI_GOTO(FnLwfLoadApi(&FnLwfLoadApiContext), Error);
    FnLwfApiInitialized = TRUE;

    FnMpIf = (TestInterface*)CxPlatAllocNonPaged(sizeof(*FnMpIf), POOL_TAG);
    TEST_NOT_NULL_GOTO(FnMpIf, Error);

    TEST_TRUE_GOTO(
        FnMpIf->Initialize(
            FNMP_IF_DESC, FNMP_IF_DESCW, FNMP_IPV4_ADDRESS, FNMP_IPV6_ADDRESS),
        Error);

    TEST_TRUE_GOTO(WaitForWfpQuarantine(FnMpIf), Error);

    return true;

Error:

    if (FnMpIf != NULL) {
        CxPlatFree(FnMpIf, POOL_TAG);
    }
    if (FnLwfApiInitialized) {
        FnLwfUnloadApi(FnLwfLoadApiContext);
    }
    if (FnMpApiInitialized) {
        FnMpUnloadApi(FnMpLoadApiContext);
    }
    if (FnSockInitialized) {
        FnSockUninitialize();
    }
    if (CxPlatInitialized) {
        CxPlatUninitialize();
    }
    if (FirewallInitialized) {
        InvokeSystem(FirewallDeleteRuleString);
    }

    return false;
}

EXTERN_C
bool
TestCleanup()
{
    CxPlatFree(FnMpIf, POOL_TAG);
    FnLwfUnloadApi(FnLwfLoadApiContext);
    FnMpUnloadApi(FnMpLoadApiContext);
    FnSockUninitialize();
    CxPlatUninitialize();
    TEST_EQUAL_RET(0, InvokeSystem(FirewallDeleteRuleString), false);
    return true;
}

EXTERN_C
VOID
MpBasicRx()
{
    UINT16 LocalPort, RemotePort;
    ETHERNET_ADDRESS LocalHw, RemoteHw;
    INET_ADDR LocalIp, RemoteIp;
    auto UdpSocket = CreateUdpSocket(AF_INET, NULL, &LocalPort);
    auto SharedMp = MpOpenShared(FnMpIf->GetIfIndex());

    TEST_NOT_NULL(UdpSocket.get());
    TEST_NOT_NULL(SharedMp.get());

    RemotePort = htons(1234);
    FnMpIf->GetHwAddress(&LocalHw);
    FnMpIf->GetRemoteHwAddress(&RemoteHw);
    FnMpIf->GetIpv4Address(&LocalIp.Ipv4);
    FnMpIf->GetRemoteIpv4Address(&RemoteIp.Ipv4);

    UCHAR UdpPayload[] = "BasicRx";
    CHAR RecvPayload[sizeof(UdpPayload)];
    UCHAR UdpFrame[UDP_HEADER_STORAGE + sizeof(UdpPayload)];
    UINT32 UdpFrameLength = sizeof(UdpFrame);
    TEST_TRUE(
        PktBuildUdpFrame(
            UdpFrame, &UdpFrameLength, UdpPayload, sizeof(UdpPayload), &LocalHw,
            &RemoteHw, AF_INET, &LocalIp, &RemoteIp, LocalPort, RemotePort));

    RX_FRAME RxFrame;
    RxInitializeFrame(&RxFrame, FnMpIf->GetQueueId(), UdpFrame, UdpFrameLength);
    TEST_FNMPAPI(MpRxIndicateFrame(SharedMp, &RxFrame));
    TEST_EQUAL(
        sizeof(UdpPayload),
        FnSockRecv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), FALSE, 0));
    TEST_TRUE(RtlEqualMemory(UdpPayload, RecvPayload, sizeof(UdpPayload)));
}

EXTERN_C
VOID
MpBasicTx()
{
    UINT16 LocalPort;
    auto UdpSocket = CreateUdpSocket(AF_INET, NULL, &LocalPort);
    auto SharedMp = MpOpenShared(FnMpIf->GetIfIndex());
    BOOLEAN Uso = TRUE;

    TEST_NOT_NULL(UdpSocket.get());
    TEST_NOT_NULL(SharedMp.get());

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

    TEST_TRUE(MpTxFilter(SharedMp, Pattern, Mask, sizeof(Pattern)));

    SOCKADDR_STORAGE RemoteAddr;
    TEST_TRUE(SetSockAddr(FNMP_NEIGHBOR_IPV4_ADDRESS, 1234, AF_INET, &RemoteAddr));

    if (CXPLAT_FAILED(
            FnSockSetSockOpt(
                UdpSocket.get(), IPPROTO_UDP, UDP_SEND_MSG_SIZE,
                &ExpectedUdpPayloadSize, sizeof(ExpectedUdpPayloadSize)))) {
        TEST_EQUAL(WSAEINVAL, FnSockGetLastError());
        Uso = FALSE;
        SendSize = ExpectedUdpPayloadSize;
    }

    TEST_EQUAL(
        (int)SendSize,
        FnSockSendto(
            UdpSocket.get(), (PCHAR)UdpPayload, SendSize, FALSE, 0,
            (PSOCKADDR)&RemoteAddr, sizeof(RemoteAddr)));

    auto MpTxFrame = MpTxAllocateAndGetFrame(SharedMp, 0);
    TEST_NOT_NULL(MpTxFrame.get());

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
        TEST_NOT_NULL(MpTxFrame.get());

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

    TEST_TRUE(MpTxDequeueFrame(SharedMp, 0));
    FrameLength = 0;
    TEST_EQUAL(
        FNMPAPI_STATUS_NOT_FOUND,
        MpTxGetFrame(SharedMp, 0, &FrameLength, NULL, 0));
    FrameLength = 0;
    TEST_EQUAL(
        FNMPAPI_STATUS_NOT_FOUND,
        MpTxGetFrame(SharedMp, 0, &FrameLength, NULL, 1));

    TEST_TRUE(MpTxFlush(SharedMp));

    //
    // Verify clearing the filter also completes all filtered NBLs.
    //

    TEST_EQUAL(
        (int)SendSize,
        FnSockSendto(
            UdpSocket.get(), (PCHAR)UdpPayload, SendSize, FALSE, 0,
            (PSOCKADDR)&RemoteAddr, sizeof(RemoteAddr)));

    MpTxFrame = MpTxAllocateAndGetFrame(SharedMp, 0);
    TEST_NOT_NULL(MpTxFrame.get());

    TEST_TRUE(MpTxFilter(SharedMp, NULL, NULL, 0));
    FrameLength = 0;
    TEST_EQUAL(
        FNMPAPI_STATUS_NOT_FOUND,
        MpTxGetFrame(SharedMp, 0, &FrameLength, NULL, 0));
}

static
VOID
InitializeOffloadParameters(
    _Out_ NDIS_OFFLOAD_PARAMETERS *OffloadParameters
    )
{
    RtlZeroMemory(OffloadParameters, sizeof(*OffloadParameters));
    OffloadParameters->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    OffloadParameters->Header.Size = NDIS_SIZEOF_OFFLOAD_PARAMETERS_REVISION_5;
    OffloadParameters->Header.Revision = NDIS_OFFLOAD_PARAMETERS_REVISION_5;
}

EXTERN_C
VOID
MpBasicRxOffload()
{
    UINT16 LocalPort, RemotePort;
    ETHERNET_ADDRESS LocalHw, RemoteHw;
    INET_ADDR LocalIp, RemoteIp;
    auto UdpSocket = CreateUdpSocket(AF_INET, NULL, &LocalPort);
    auto SharedMp = MpOpenShared(FnMpIf->GetIfIndex());
    auto MpTaskOffloadCleanup = wil::scope_exit([&]() {
        MpUpdateTaskOffload(SharedMp, FnOffloadCurrentConfig, NULL);
    });

    TEST_NOT_NULL(UdpSocket.get());
    TEST_NOT_NULL(SharedMp.get());

    RemotePort = htons(1234);
    FnMpIf->GetHwAddress(&LocalHw);
    FnMpIf->GetRemoteHwAddress(&RemoteHw);
    FnMpIf->GetIpv4Address(&LocalIp.Ipv4);
    FnMpIf->GetRemoteIpv4Address(&RemoteIp.Ipv4);

    UCHAR UdpPayload[] = "BasicRxOffload";
    CHAR RecvPayload[sizeof(UdpPayload)];
    UCHAR UdpFrame[UDP_HEADER_STORAGE + sizeof(UdpPayload)];
    UINT32 UdpFrameLength = sizeof(UdpFrame);
    TEST_TRUE(
        PktBuildUdpFrame(
            UdpFrame, &UdpFrameLength, UdpPayload, sizeof(UdpPayload), &LocalHw,
            &RemoteHw, AF_INET, &LocalIp, &RemoteIp, LocalPort, RemotePort));

    RX_FRAME RxFrame;
    RxInitializeFrame(&RxFrame, FnMpIf->GetQueueId(), UdpFrame, UdpFrameLength);
    TEST_FNMPAPI(MpRxIndicateFrame(SharedMp, &RxFrame));
    TEST_EQUAL(
        sizeof(UdpPayload),
        FnSockRecv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), FALSE, 0));
    TEST_TRUE(RtlEqualMemory(UdpPayload, RecvPayload, sizeof(UdpPayload)));

    NDIS_OFFLOAD_PARAMETERS OffloadParams;
    InitializeOffloadParameters(&OffloadParams);
    OffloadParams.UDPIPv4Checksum = NDIS_OFFLOAD_PARAMETERS_RX_ENABLED_TX_DISABLED;
    TEST_TRUE(MpUpdateTaskOffload(SharedMp, FnOffloadCurrentConfig, &OffloadParams));

    //
    // Set the checksum succeeded OOB bit, and mangle the checksum. If the OOB
    // is being respected, the UDP datagram will still be delivered.
    //
    RxFrame.Frame.Input.Checksum.Receive.UdpChecksumSucceeded = TRUE;
    UDP_HDR *UdpHdr = (UDP_HDR *)&UdpFrame[UDP_HEADER_BACKFILL(AF_INET) - sizeof(*UdpHdr)];
    UdpHdr->uh_sum++;
    TEST_FNMPAPI(MpRxIndicateFrame(SharedMp, &RxFrame));
    TEST_EQUAL(
        sizeof(UdpPayload),
        FnSockRecv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), FALSE, 0));
    TEST_TRUE(RtlEqualMemory(UdpPayload, RecvPayload, sizeof(UdpPayload)));
    RxFrame.Frame.Input.Checksum.Value = 0;
    UdpHdr->uh_sum--;
    TEST_TRUE(MpUpdateTaskOffload(SharedMp, FnOffloadCurrentConfig, NULL));

    //
    // Try the same thing with the IP header.
    //
    InitializeOffloadParameters(&OffloadParams);
    OffloadParams.IPv4Checksum = NDIS_OFFLOAD_PARAMETERS_RX_ENABLED_TX_DISABLED;
    TEST_TRUE(MpUpdateTaskOffload(SharedMp, FnOffloadCurrentConfig, &OffloadParams));

    RxFrame.Frame.Input.Checksum.Receive.IpChecksumSucceeded = TRUE;
    RxFrame.Frame.Input.Checksum.Receive.IpChecksumValueInvalid = TRUE;
    IPV4_HEADER *IpHdr = (IPV4_HEADER *)&UdpFrame[sizeof(ETHERNET_HEADER)];
    IpHdr->HeaderChecksum++;
    TEST_FNMPAPI(MpRxIndicateFrame(SharedMp, &RxFrame));
    TEST_EQUAL(
        sizeof(UdpPayload),
        FnSockRecv(UdpSocket.get(), RecvPayload, sizeof(RecvPayload), FALSE, 0));
    TEST_TRUE(RtlEqualMemory(UdpPayload, RecvPayload, sizeof(UdpPayload)));
    RxFrame.Frame.Input.Checksum.Value = 0;
    IpHdr->HeaderChecksum--;

    //
    // Validate timestamping.
    //
    TIMESTAMPING_CONFIG TimestampingConfig = {0};
    TimestampingConfig.Flags = TIMESTAMPING_FLAG_RX;
    ULONG BytesReturned;
    TEST_CXPLAT(
        FnSockIoctl(
            UdpSocket.get(), SIO_TIMESTAMPING, (char*)&TimestampingConfig,
            sizeof(TimestampingConfig), NULL, 0, &BytesReturned));

    RxFrame.Frame.Input.Timestamp.Timestamp = 0x123456789ABCDEF0;
    TEST_FNMPAPI(MpRxIndicateFrame(SharedMp, &RxFrame));

    CHAR ControlBuffer[CMSG_SPACE(sizeof(UINT64))];
    INT ControlBufferLength = sizeof(ControlBuffer);
    INT Flags = 0;
    TEST_EQUAL(
        sizeof(UdpPayload),
        FnSockRecvMsg(
            UdpSocket.get(), RecvPayload, sizeof(RecvPayload), FALSE, (CMSGHDR *)ControlBuffer,
            &ControlBufferLength, &Flags));
    TEST_TRUE(RtlEqualMemory(UdpPayload, RecvPayload, sizeof(UdpPayload)));
    TEST_FALSE(Flags & MSG_CTRUNC);

    WSAMSG Msg = {0};
    Msg.Control.buf = ControlBuffer;
    Msg.Control.len = ControlBufferLength;
    UINT64 Timestamp = 0;

    for (WSACMSGHDR* CMsg = CMSG_FIRSTHDR(&Msg);
        CMsg != NULL;
        CMsg = CMSG_NXTHDR(&Msg, CMsg)) {
        if (CMsg->cmsg_level == SOL_SOCKET && CMsg->cmsg_type == SO_TIMESTAMP) {
            Timestamp = *(PUINT64)WSA_CMSG_DATA(CMsg);
        }
    }

    TEST_EQUAL(Timestamp, RxFrame.Frame.Input.Timestamp.Timestamp);
}

EXTERN_C
VOID
MpBasicTxOffload()
{
    UINT16 LocalPort;
    auto UdpSocket = CreateUdpSocket(AF_INET, NULL, &LocalPort);
    auto SharedMp = MpOpenShared(FnMpIf->GetIfIndex());
    auto MpTaskOffloadCleanup = wil::scope_exit([&]() {
        MpUpdateTaskOffload(SharedMp, FnOffloadCurrentConfig, NULL);
    });

    TEST_NOT_NULL(UdpSocket.get());
    TEST_NOT_NULL(SharedMp.get());

    UCHAR UdpPayload[] = "BasicTxOffload";
    UCHAR Pattern[UDP_HEADER_BACKFILL(AF_INET) + sizeof(UdpPayload)];
    UCHAR Mask[sizeof(Pattern)];

    RtlZeroMemory(Pattern, sizeof(Pattern));
    RtlCopyMemory(Pattern + UDP_HEADER_BACKFILL(AF_INET), UdpPayload, sizeof(UdpPayload));

    RtlZeroMemory(Mask, sizeof(Mask));
    for (int i = UDP_HEADER_BACKFILL(AF_INET); i < sizeof(Mask); i++) {
        Mask[i] = 0xff;
    }

    TEST_TRUE(MpTxFilter(SharedMp, Pattern, Mask, sizeof(Pattern)));

    SOCKADDR_STORAGE RemoteAddr;
    TEST_TRUE(SetSockAddr(FNMP_NEIGHBOR_IPV4_ADDRESS, 1234, AF_INET, &RemoteAddr));

    TEST_EQUAL(
        (int)sizeof(UdpPayload),
        FnSockSendto(
            UdpSocket.get(), (PCHAR)UdpPayload, sizeof(UdpPayload), FALSE, 0,
            (PSOCKADDR)&RemoteAddr, sizeof(RemoteAddr)));

    auto MpTxFrame = MpTxAllocateAndGetFrame(SharedMp, 0);
    TEST_NOT_NULL(MpTxFrame.get());
    TEST_FALSE(MpTxFrame->Output.Checksum.Transmit.UdpChecksum);
    TEST_TRUE(MpTxDequeueFrame(SharedMp, 0));
    TEST_TRUE(MpTxFlush(SharedMp));

    NDIS_OFFLOAD_PARAMETERS OffloadParams;
    InitializeOffloadParameters(&OffloadParams);
    OffloadParams.UDPIPv4Checksum = NDIS_OFFLOAD_PARAMETERS_TX_ENABLED_RX_DISABLED;
    TEST_TRUE(MpUpdateTaskOffload(SharedMp, FnOffloadCurrentConfig, &OffloadParams));

    TEST_EQUAL(
        (int)sizeof(UdpPayload),
        FnSockSendto(
            UdpSocket.get(), (PCHAR)UdpPayload, sizeof(UdpPayload), FALSE, 0,
            (PSOCKADDR)&RemoteAddr, sizeof(RemoteAddr)));

    MpTxFrame = MpTxAllocateAndGetFrame(SharedMp, 0);
    TEST_NOT_NULL(MpTxFrame.get());
    TEST_TRUE(MpTxFrame->Output.Checksum.Transmit.UdpChecksum);
    TEST_TRUE(MpTxDequeueFrame(SharedMp, 0));
    TEST_TRUE(MpTxFlush(SharedMp));

    //
    // Validate timestamping.
    //
    TIMESTAMPING_CONFIG TimestampingConfig = {0};
    TimestampingConfig.Flags = TIMESTAMPING_FLAG_TX;
    TimestampingConfig.TxTimestampsBuffered = 1;
    ULONG BytesReturned;
    TEST_CXPLAT(
        FnSockIoctl(
            UdpSocket.get(), SIO_TIMESTAMPING, (char*)&TimestampingConfig,
            sizeof(TimestampingConfig), NULL, 0, &BytesReturned));

    UINT32 *TimestampId;
    CHAR ControlBuffer[CMSG_SPACE(sizeof(*TimestampId))];
    CMSGHDR *Cmsg = (CMSGHDR *)ControlBuffer;
    Cmsg->cmsg_len = CMSG_LEN(sizeof(*TimestampId));
    Cmsg->cmsg_level = SOL_SOCKET;
    Cmsg->cmsg_type = SO_TIMESTAMP_ID;
    TimestampId = (UINT32 *)WSA_CMSG_DATA(Cmsg);
    *TimestampId = 0xABCDEF01;

    TEST_EQUAL(
        (int)sizeof(UdpPayload),
        FnSockSendMsg(
            UdpSocket.get(), (PCHAR)UdpPayload, sizeof(UdpPayload), FALSE, 0,
            (PSOCKADDR)&RemoteAddr, sizeof(RemoteAddr), (CMSGHDR *)ControlBuffer,
            sizeof(ControlBuffer)));

    MpTxFrame = MpTxAllocateAndGetFrame(SharedMp, 0);
    TEST_NOT_NULL(MpTxFrame.get());

    DATA_FRAME UpdateFrame = {0};
    UpdateFrame.Input.Timestamp.Timestamp = 0x0123456789ABCDEF0;
    UpdateFrame.Input.Flags.Timestamp = TRUE;
    TEST_CXPLAT(MpTxSetFrame(SharedMp, 0, &UpdateFrame));
    TEST_TRUE(MpTxDequeueFrame(SharedMp, 0));
    TEST_TRUE(MpTxFlush(SharedMp));

    Stopwatch Watchdog(TEST_TIMEOUT_ASYNC_MS);
    UINT64 Timestamp = 0;

    do {
        FNSOCK_STATUS Status =
            FnSockIoctl(
                UdpSocket.get(), SIO_GET_TX_TIMESTAMP, TimestampId, sizeof(*TimestampId),
                &Timestamp, sizeof(Timestamp), &BytesReturned);

        if (FNSOCK_SUCCEEDED(Status)) {
            break;
        }
    } while (CxPlatSleep(POLL_INTERVAL_MS), !Watchdog.IsExpired());

    TEST_EQUAL(UpdateFrame.Input.Timestamp.Timestamp, Timestamp);
}

EXTERN_C
VOID
MpBasicWatchdog()
{
    UINT16 LocalPort;
    OID_KEY OidKey;
    auto UdpSocket = CreateUdpSocket(AF_INET, NULL, &LocalPort);
    auto SharedMp = MpOpenShared(FnMpIf->GetIfIndex());
    auto ExclusiveMp = MpOpenExclusive(FnMpIf->GetIfIndex());
    auto DefaultLwf = LwfOpenDefault(FnMpIf->GetIfIndex());

    TEST_NOT_NULL(UdpSocket.get());
    TEST_NOT_NULL(SharedMp.get());
    TEST_NOT_NULL(ExclusiveMp.get());
    TEST_NOT_NULL(DefaultLwf.get());

    InitializeOidKey(
        &OidKey, OID_QUIC_CONNECTION_ENCRYPTION_PROTOTYPE, NdisRequestMethod,
        OID_REQUEST_INTERFACE_DIRECT);
    TEST_TRUE(MpOidFilter(ExclusiveMp, &OidKey, 1));

    UCHAR UdpPayload[] = "MpBasicWatchdog";
    UCHAR Pattern[UDP_HEADER_BACKFILL(AF_INET) + sizeof(UdpPayload)];
    UCHAR Mask[sizeof(Pattern)];
    UINT32 SendSize = sizeof(UdpPayload);

    RtlZeroMemory(Pattern, sizeof(Pattern));
    RtlCopyMemory(Pattern + UDP_HEADER_BACKFILL(AF_INET), UdpPayload, sizeof(UdpPayload));

    RtlZeroMemory(Mask, sizeof(Mask));
    for (int i = UDP_HEADER_BACKFILL(AF_INET); i < sizeof(Mask); i++) {
        Mask[i] = 0xff;
    }

    TEST_TRUE(MpTxFilter(SharedMp, Pattern, Mask, sizeof(Pattern)));

    SOCKADDR_STORAGE RemoteAddr;
    TEST_TRUE(SetSockAddr(FNMP_NEIGHBOR_IPV4_ADDRESS, 1234, AF_INET, &RemoteAddr));

    TEST_EQUAL(
        (int)SendSize,
        FnSockSendto(
            UdpSocket.get(), (PCHAR)UdpPayload, SendSize, FALSE, 0,
            (PSOCKADDR)&RemoteAddr, sizeof(RemoteAddr)));

    //
    // Verify frame can be retrieved, and is idempotent.
    //
    MpTxAllocateAndGetFrame(SharedMp, 0);
    MpTxAllocateAndGetFrame(SharedMp, 0);

    UINT32 LwfInfoBuffer = 0xBADF00D;
    UINT32 LwfInfoBufferLength = sizeof(LwfInfoBuffer);

    LWF_OID_SUBMIT_REQUEST Req;
    Req.Handle = DefaultLwf.get();
    Req.OidKey = OidKey;
    Req.InfoBufferLength = &LwfInfoBufferLength;
    Req.InfoBuffer = &LwfInfoBuffer;

    CXPLAT_THREAD_CONFIG ThreadConfig {
        0, 0, NULL, LwfOidSubmitRequestFn, &Req
    };
    unique_cxplat_thread AsyncThread;
    TEST_CXPLAT(CxPlatThreadCreate(&ThreadConfig, &AsyncThread));

    TEST_FALSE(CxPlatThreadWait(AsyncThread.get(), TEST_TIMEOUT_ASYNC_MS));

    //
    // Wait 10 seconds for the watchdog.
    //
    CxPlatSleep(10000);

    //
    // The NBLs and OIDs should be released by the watchdog.
    //
    UINT32 FrameLength = 0;
    TEST_EQUAL(
        FNMPAPI_STATUS_NOT_FOUND,
        MpTxGetFrame(SharedMp, 0, &FrameLength, NULL, 0));

    TEST_TRUE(CxPlatThreadWait(AsyncThread.get(), TEST_TIMEOUT_ASYNC_MS));
}

static
VOID
MpVerifyPortState(
    _In_ const unique_fnlwf_handle &LwfHandle,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ BOOLEAN ExpectFound
    )
{
    OID_KEY OidKey;
    InitializeOidKey(&OidKey, OID_GEN_ENUMERATE_PORTS, NdisRequestQueryInformation);
    NDIS_PORT_STATE PortState = {0};
    UINT32 BytesReturned;
    BOOLEAN Found = FALSE;

    //
    // Verify the port state is accurately reported to NDIS components.
    //

    auto PortArray =
        LwfOidAllocateAndSubmitRequest<NDIS_PORT_ARRAY>(LwfHandle, OidKey, &BytesReturned);
    TEST_NOT_NULL(PortArray.get());

    for (UINT64 i = 0; i < PortArray->NumberOfPorts; i++) {
        const NDIS_PORT_CHARACTERISTICS *PortCharacteristics = (const NDIS_PORT_CHARACTERISTICS *)
            RTL_PTR_ADD(PortArray.get(), PortArray->OffsetFirstPort + PortArray->ElementSize * i);

        Found |= PortCharacteristics->PortNumber == PortNumber;
    }

    TEST_EQUAL(ExpectFound, Found);
}

EXTERN_C
VOID
MpBasicPort()
{
    auto ExclusiveMp = MpOpenExclusive(FnMpIf->GetIfIndex());
    auto DefaultLwf = LwfOpenDefault(FnMpIf->GetIfIndex());

    NDIS_PORT_NUMBER Number;
    TEST_FNMPAPI(MpAllocatePort(ExclusiveMp, &Number));

    NDIS_PORT_NUMBER LeakAllocatedNumber;
    TEST_FNMPAPI(MpAllocatePort(ExclusiveMp, &LeakAllocatedNumber));
    TEST_NOT_EQUAL(FNMPAPI_STATUS_SUCCESS, MpDeactivatePort(ExclusiveMp, Number));

    TEST_NOT_EQUAL(Number, LeakAllocatedNumber);

    TEST_FNMPAPI(MpFreePort(ExclusiveMp, Number));
    TEST_EQUAL(FNMPAPI_STATUS_NOT_FOUND, MpFreePort(ExclusiveMp, Number));

    TEST_FNMPAPI(MpAllocatePort(ExclusiveMp, &Number));
    MpVerifyPortState(DefaultLwf, Number, FALSE);

    TEST_FNMPAPI(MpActivatePort(ExclusiveMp, Number));
    MpVerifyPortState(DefaultLwf, Number, TRUE);

    TEST_NOT_EQUAL(FNMPAPI_STATUS_SUCCESS, MpActivatePort(ExclusiveMp, Number));
    TEST_NOT_EQUAL(FNMPAPI_STATUS_SUCCESS, MpFreePort(ExclusiveMp, Number));

    TEST_FNMPAPI(MpDeactivatePort(ExclusiveMp, Number));
    MpVerifyPortState(DefaultLwf, Number, FALSE);

    TEST_FNMPAPI(MpActivatePort(ExclusiveMp, Number));
    MpVerifyPortState(DefaultLwf, Number, TRUE);

    TEST_FNMPAPI(MpDeactivatePort(ExclusiveMp, Number));
    MpVerifyPortState(DefaultLwf, Number, FALSE);

    TEST_FNMPAPI(MpFreePort(ExclusiveMp, Number));
    TEST_EQUAL(FNMPAPI_STATUS_NOT_FOUND, MpActivatePort(ExclusiveMp, Number));
    TEST_EQUAL(FNMPAPI_STATUS_NOT_FOUND, MpDeactivatePort(ExclusiveMp, Number));
    MpVerifyPortState(DefaultLwf, Number, FALSE);

    NDIS_PORT_NUMBER LeakActivatedNumber;
    TEST_FNMPAPI(MpAllocatePort(ExclusiveMp, &LeakActivatedNumber));
    TEST_FNMPAPI(MpActivatePort(ExclusiveMp, LeakActivatedNumber));

    NDIS_PORT_NUMBER LeakDeactivatedNumber;
    TEST_FNMPAPI(MpAllocatePort(ExclusiveMp, &LeakDeactivatedNumber));
    TEST_FNMPAPI(MpActivatePort(ExclusiveMp, LeakDeactivatedNumber));
    TEST_FNMPAPI(MpDeactivatePort(ExclusiveMp, LeakDeactivatedNumber));
    TEST_NOT_EQUAL(Number, LeakAllocatedNumber);
    TEST_NOT_EQUAL(LeakActivatedNumber, LeakDeactivatedNumber);
}

EXTERN_C
VOID
LwfBasicRx()
{
    auto GenericMp = MpOpenShared(FnMpIf->GetIfIndex());
    auto DefaultLwf = LwfOpenDefault(FnMpIf->GetIfIndex());

    TEST_NOT_NULL(GenericMp.get());
    TEST_NOT_NULL(DefaultLwf.get());

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

    TEST_TRUE(LwfRxFilter(DefaultLwf, &Pattern, &Mask, sizeof(Pattern)));

    RX_FRAME Frame;
    RxInitializeFrame(&Frame, FnMpIf->GetQueueId(), &Buffer);
    TEST_FNMPAPI(MpRxEnqueueFrame(GenericMp, &Frame));
    TEST_FNMPAPI(TryMpRxFlush(GenericMp));

    auto LwfRxFrame = LwfRxAllocateAndGetFrame(DefaultLwf, 0);
    TEST_NOT_NULL(LwfRxFrame.get());
    TEST_EQUAL(LwfRxFrame->BufferCount, Frame.Frame.BufferCount);

    const DATA_BUFFER *LwfRxBuffer = &LwfRxFrame->Buffers[0];
    TEST_EQUAL(LwfRxBuffer->BufferLength, Buffer.BufferLength);
    TEST_EQUAL(LwfRxBuffer->DataOffset, Buffer.DataOffset);
    TEST_TRUE(
        RtlEqualMemory(
            Buffer.VirtualAddress + Buffer.DataOffset,
            LwfRxBuffer->VirtualAddress + LwfRxBuffer->DataOffset,
            Buffer.DataLength));

    TEST_TRUE(LwfRxDequeueFrame(DefaultLwf, 0));
    TEST_TRUE(LwfRxFlush(DefaultLwf));
}

EXTERN_C
VOID
LwfBasicTx()
{
    auto GenericMp = MpOpenShared(FnMpIf->GetIfIndex());
    auto DefaultLwf = LwfOpenDefault(FnMpIf->GetIfIndex());

    TEST_NOT_NULL(GenericMp.get());
    TEST_NOT_NULL(DefaultLwf.get());

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

    TEST_TRUE(MpTxFilter(GenericMp, &Pattern, &Mask, sizeof(Pattern)));

    TEST_TRUE(LwfTxEnqueue(DefaultLwf, &Frame));
    TEST_TRUE(LwfTxFlush(DefaultLwf));

    auto MpTxFrame = MpTxAllocateAndGetFrame(GenericMp, 0);
    TEST_NOT_NULL(MpTxFrame.get());
    TEST_EQUAL(MpTxFrame->BufferCount, Frame.BufferCount);

    const DATA_BUFFER *MpTxBuffer = &MpTxFrame->Buffers[0];
    TEST_EQUAL(MpTxBuffer->BufferLength, Buffer.BufferLength);
    TEST_EQUAL(MpTxBuffer->DataOffset, Buffer.DataOffset);
    TEST_TRUE(
        RtlEqualMemory(
            Buffer.VirtualAddress + Buffer.DataOffset,
            MpTxBuffer->VirtualAddress + MpTxBuffer->DataOffset,
            Buffer.DataLength));

    TEST_TRUE(MpTxDequeueFrame(GenericMp, 0));
    TEST_TRUE(MpTxFlush(GenericMp));
}

EXTERN_C
VOID
LwfBasicOid()
{
    OID_KEY OidKeys[3];
    auto DefaultLwf = LwfOpenDefault(FnMpIf->GetIfIndex());

    TEST_NOT_NULL(DefaultLwf.get());

    //
    // Get.
    //
    InitializeOidKey(&OidKeys[0], OID_GEN_RECEIVE_BLOCK_SIZE, NdisRequestQueryInformation);

    //
    // Set.
    //
    InitializeOidKey(&OidKeys[1], OID_FNMP_SET_NOP, NdisRequestSetInformation);

    //
    // Method. (Direct OID)
    //
    InitializeOidKey(
        &OidKeys[2], OID_FNMP_METHOD_DIRECT_NOP, NdisRequestMethod, OID_REQUEST_INTERFACE_DIRECT);

    for (UINT32 Index = 0; Index < RTL_NUMBER_OF(OidKeys); Index++) {
        for (UINT32 Port = 0; Port <= 1; Port++) {
            OID_KEY OidKey = OidKeys[Index];
            ULONG LwfInfoBuffer = 0x12345678;
            const UINT32 CompletionSize = sizeof(LwfInfoBuffer) / 2;
            UINT32 MpInfoBufferLength;
            unique_malloc_ptr<VOID> MpInfoBuffer;
            auto ExclusiveMp = MpOpenExclusive(FnMpIf->GetIfIndex());
            TEST_NOT_NULL(ExclusiveMp.get());

            OidKey.PortNumber = Port;

            TEST_TRUE(MpOidFilter(ExclusiveMp, &OidKey, 1));

            UINT32 LwfInfoBufferLength = sizeof(LwfInfoBuffer);

            //
            // Verify OIDs are filtered only if port numbers match.
            //
            OID_KEY WrongPortKey = OidKey;
            WrongPortKey.PortNumber = !WrongPortKey.PortNumber;
            ULONG WrongInfoBuffer = LwfInfoBuffer;
            UINT32 WrongInfoBufferLength = LwfInfoBufferLength;
            TEST_FNLWFAPI(
                LwfOidSubmitRequest(
                    DefaultLwf, WrongPortKey, &WrongInfoBufferLength, &WrongInfoBuffer));
            MpInfoBufferLength = 0;
            TEST_EQUAL(
                FNMPAPI_STATUS_NOT_FOUND,
                MpOidGetRequest(ExclusiveMp, OidKey, &MpInfoBufferLength, NULL));

            LWF_OID_SUBMIT_REQUEST Req;
            Req.Handle = DefaultLwf.get();
            Req.OidKey = OidKey;
            Req.InfoBufferLength = &LwfInfoBufferLength;
            Req.InfoBuffer = &LwfInfoBuffer;

            CXPLAT_THREAD_CONFIG ThreadConfig {
                0, 0, NULL, LwfOidSubmitRequestFn, &Req
            };
            unique_cxplat_thread AsyncThread;
            TEST_CXPLAT(CxPlatThreadCreate(&ThreadConfig, &AsyncThread));

            MpInfoBuffer = MpOidAllocateAndGetRequest(ExclusiveMp, OidKey, &MpInfoBufferLength);
            TEST_NOT_NULL(MpInfoBuffer.get());

            TEST_TRUE(MpOidCompleteRequest(
                ExclusiveMp, OidKey, STATUS_SUCCESS, &LwfInfoBuffer, CompletionSize));

            TEST_TRUE(CxPlatThreadWait(AsyncThread.get(), TEST_TIMEOUT_ASYNC_MS));
            TEST_FNMPAPI(Req.Status);

            TEST_EQUAL(LwfInfoBufferLength, CompletionSize);
        }
    }
}

EXTERN_C
VOID
SockBasicTcp(
    USHORT AddressFamily
    )
{

    unique_fnsock_handle ListenSocket;
    TEST_CXPLAT(
        FnSockCreate(
            AddressFamily,
            SOCK_STREAM,
            IPPROTO_TCP,
            &ListenSocket));
    TEST_NOT_NULL(ListenSocket.get());

    SOCKADDR_INET Address = {0};
    Address.si_family = AddressFamily;

    TEST_CXPLAT(
        FnSockBind(
            ListenSocket.get(),
            (SOCKADDR *)&Address,
            sizeof(Address)));

    INT AddressLength = sizeof(Address);
    TEST_CXPLAT(FnSockGetSockName(ListenSocket.get(), (SOCKADDR *)&Address, &AddressLength));

    TEST_CXPLAT(FnSockListen(ListenSocket.get(), 32));

    unique_fnsock_handle ClientSocket;
    TEST_CXPLAT(
        FnSockCreate(
            AddressFamily,
            SOCK_STREAM,
            IPPROTO_TCP,
            &ClientSocket));
    TEST_NOT_NULL(ClientSocket.get());

    if (AddressFamily == AF_INET) {
        Address.Ipv4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    } else {
        IN6_SET_ADDR_LOOPBACK(&Address.Ipv6.sin6_addr);
    }

    INT TimeoutMs = TEST_TIMEOUT_ASYNC_MS;
    TEST_CXPLAT(
        FnSockSetSockOpt(
            ClientSocket.get(), SOL_SOCKET, SO_RCVTIMEO, (CHAR *)&TimeoutMs, sizeof(TimeoutMs)));
    TEST_CXPLAT(
        FnSockSetSockOpt(
            ListenSocket.get(), SOL_SOCKET, SO_RCVTIMEO, (CHAR *)&TimeoutMs, sizeof(TimeoutMs)));

    TEST_CXPLAT(FnSockConnect(ClientSocket.get(), (SOCKADDR *)&Address, AddressLength));

    unique_fnsock_handle AcceptSocket(FnSockAccept(ListenSocket.get(), (SOCKADDR *)&Address, &AddressLength));
    TEST_NOT_NULL(AcceptSocket.get());

    TEST_CXPLAT(
        FnSockSetSockOpt(
            AcceptSocket.get(), SOL_SOCKET, SO_RCVTIMEO, (CHAR *)&TimeoutMs, sizeof(TimeoutMs)));

    CONST CHAR *Msg1 = "SockBasicTcp-Request";
    CONST INT Msg1Len = (CONST INT)strlen(Msg1);
    TEST_EQUAL(Msg1Len, FnSockSend(ClientSocket.get(), Msg1, Msg1Len, FALSE, 0));

    CHAR RecvBuffer[64];
    TEST_EQUAL(Msg1Len, FnSockRecv(AcceptSocket.get(), RecvBuffer, sizeof(RecvBuffer), FALSE, 0));
    TEST_TRUE(RtlEqualMemory(RecvBuffer, Msg1, Msg1Len));

    CONST CHAR *Msg2 = "SockBasicTcp-Response";
    CONST INT Msg2Len = (CONST INT)strlen(Msg2);
    TEST_EQUAL(Msg2Len, FnSockSend(AcceptSocket.get(), Msg2, Msg2Len, FALSE, 0));

    TEST_EQUAL(Msg2Len, FnSockRecv(ClientSocket.get(), RecvBuffer, sizeof(RecvBuffer), FALSE, 0));
    TEST_TRUE(RtlEqualMemory(RecvBuffer, Msg2, Msg2Len));

    DWORD Opt;
    SIZE_T OptLen = sizeof(Opt);
    TEST_CXPLAT(FnSockGetSockOpt(ClientSocket.get(), IPPROTO_TCP, TCP_KEEPCNT, &Opt, &OptLen));

#ifndef _KERNEL_MODE
    LINGER lingerInfo;
    lingerInfo.l_onoff = 1;
    lingerInfo.l_linger = 0;
    TEST_CXPLAT(
        FnSockSetSockOpt(
            AcceptSocket.get(), SOL_SOCKET, SO_LINGER, (CHAR *)&lingerInfo, sizeof(lingerInfo)));
#endif
}

EXTERN_C
VOID
SockBasicRaw(
    USHORT AddressFamily
    )
{
    unique_fnsock_handle Socket;
    TEST_CXPLAT(FnSockCreate(AddressFamily, SOCK_RAW, IPPROTO_IP, &Socket));
    TEST_NOT_NULL(Socket.get());

    SOCKADDR_INET Address = {0};
    Address.si_family = AddressFamily;

    if (AddressFamily == AF_INET) {
        PCSTR Terminator;
        TEST_NTSTATUS(
            RtlIpv4StringToAddressA(FNMP_IPV4_ADDRESS, FALSE, &Terminator, &Address.Ipv4.sin_addr));
    } else {
        PCSTR Terminator;
        TEST_NTSTATUS(
            RtlIpv6StringToAddressA(FNMP_IPV6_ADDRESS, &Terminator, &Address.Ipv6.sin6_addr));
    }

    TEST_CXPLAT(FnSockBind(Socket.get(), (SOCKADDR *)&Address, sizeof(Address)));

    DWORD Opt = RCVALL_ON;
    DWORD BytesReturned;
    TEST_CXPLAT(
        FnSockIoctl(Socket.get(), SIO_RCVALL, &Opt, sizeof(Opt), NULL, 0, &BytesReturned));

    //
    // The TX datapath will form non-contiguous data indications, which FnSock
    // discards on the RX path. Skipping datapath validation for now.
    //
}
