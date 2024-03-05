//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include <fniotypes.h>
#if defined(_KERNEL_MODE)
#include <fnioctl_km.h>
#include <fnmpapistatus_km.h>
#else
#include <fnioctl_um.h>
#include <fnmpapistatus_um.h>
#endif
#include <fnmpioctl.h>
#include <fnmpapiconfig.h>

EXTERN_C_START

#define FNMPAPI inline

#define FNMP_OPEN_EA_LENGTH \
    (sizeof(FILE_FULL_EA_INFORMATION) + \
        sizeof(FNMP_OPEN_PACKET_NAME) + \
        sizeof(FNMP_OPEN_PACKET))

#ifndef RTL_PTR_ADD
#define RTL_PTR_ADD(Pointer, Value) \
    ((VOID *)((ULONG_PTR)(Pointer) + (ULONG_PTR)(Value)))
#endif

typedef struct _FNMP_LOAD_CONTEXT *FNMP_LOAD_API_CONTEXT;
DECLARE_HANDLE(FNMP_HANDLE);

FNMPAPI
FNMPAPI_STATUS
FnMpLoadApi(
    _Out_ FNMP_LOAD_API_CONTEXT *LoadApiContext
    )
{
    *LoadApiContext = (FNMP_LOAD_API_CONTEXT)(ULONG_PTR)0x12345678;
    return S_OK;
}

FNMPAPI
VOID
FnMpUnloadApi(
    _In_ FNMP_LOAD_API_CONTEXT LoadApiContext
    )
{
    if ((ULONG_PTR)LoadApiContext != 0x12345678) {
        __fastfail(FAST_FAIL_INVALID_ARG);
    }
}

VOID *
FnMpInitializeEa(
    _In_ FNMP_FILE_TYPE FileType,
    _Out_ VOID *EaBuffer,
    _In_ UINT32 EaLength
    )
{
    FILE_FULL_EA_INFORMATION *EaHeader = (FILE_FULL_EA_INFORMATION *)EaBuffer;
    FNMP_OPEN_PACKET *OpenPacket;

    if (EaLength < FNMP_OPEN_EA_LENGTH) {
        __fastfail(FAST_FAIL_INVALID_ARG);
    }

    RtlZeroMemory(EaHeader, sizeof(*EaHeader));
    EaHeader->EaNameLength = sizeof(FNMP_OPEN_PACKET_NAME) - 1;
    RtlCopyMemory(EaHeader->EaName, FNMP_OPEN_PACKET_NAME, sizeof(FNMP_OPEN_PACKET_NAME));
    EaHeader->EaValueLength = (USHORT)(EaLength - sizeof(*EaHeader) - sizeof(FNMP_OPEN_PACKET_NAME));

    OpenPacket = (FNMP_OPEN_PACKET *)(EaHeader->EaName + sizeof(FNMP_OPEN_PACKET_NAME));
    OpenPacket->ObjectType = FileType;

    return OpenPacket + 1;
}

FNMPAPI
FNMPAPI_STATUS
FnMpOpenShared(
    _In_ UINT32 IfIndex,
    _Out_ FNMP_HANDLE *Handle
    )
{
    FNMP_OPEN_SHARED *OpenShared;
    CHAR EaBuffer[FNMP_OPEN_EA_LENGTH + sizeof(*OpenShared)];

    OpenShared =
        (FNMP_OPEN_SHARED *)
            FnMpInitializeEa(FNMP_FILE_TYPE_SHARED, EaBuffer, sizeof(EaBuffer));
    OpenShared->IfIndex = IfIndex;

    return FnIoctlOpen(FNMP_DEVICE_NAME, FILE_CREATE, EaBuffer, sizeof(EaBuffer), (FNIOCTL_HANDLE *)Handle);
}

FNMPAPI
FNMPAPI_STATUS
FnMpOpenExclusive(
    _In_ UINT32 IfIndex,
    _Out_ FNMP_HANDLE *Handle
    )
{
    FNMP_OPEN_EXCLUSIVE *OpenExclusive;
    CHAR EaBuffer[FNMP_OPEN_EA_LENGTH + sizeof(*OpenExclusive)];

    OpenExclusive =
        (FNMP_OPEN_EXCLUSIVE *)
            FnMpInitializeEa(FNMP_FILE_TYPE_EXCLUSIVE, EaBuffer, sizeof(EaBuffer));
    OpenExclusive->IfIndex = IfIndex;

    return FnIoctlOpen(FNMP_DEVICE_NAME, FILE_CREATE, EaBuffer, sizeof(EaBuffer), (FNIOCTL_HANDLE *)Handle);
}

FNMPAPI
VOID
FnMpClose(
    _In_ FNMP_HANDLE Handle
    )
{
    FnIoctlClose(Handle);
}

FNMPAPI
FNMPAPI_STATUS
FnMpRxEnqueue(
    _In_ FNMP_HANDLE Handle,
    _In_ DATA_FRAME *Frame
    )
{
    DATA_ENQUEUE_IN In = {0};

    //
    // Supports shared handles.
    // Enqueues one frame to the RX backlog.
    //

    In.Frame = *Frame;

    return FnIoctl(Handle, FNMP_IOCTL_RX_ENQUEUE, &In, sizeof(In), NULL, 0, NULL, NULL);
}

FNMPAPI
FNMPAPI_STATUS
FnMpRxFlush(
    _In_ FNMP_HANDLE Handle,
    _In_opt_ DATA_FLUSH_OPTIONS *Options
    )
{
    DATA_FLUSH_IN In = {0};

    //
    // Supports shared handles.
    // Indicates all RX frames from the backlog.
    //
    if (Options != NULL) {
        In.Options = *Options;
    }

    return FnIoctl(Handle, FNMP_IOCTL_RX_FLUSH, &In, sizeof(In), NULL, 0, NULL, NULL);
}

FNMPAPI
FNMPAPI_STATUS
FnMpTxFilter(
    _In_ FNMP_HANDLE Handle,
    _In_ const VOID *Pattern,
    _In_ const VOID *Mask,
    _In_ UINT32 Length
    )
{
    DATA_FILTER_IN In = {0};

    //
    // Supports shared handles.
    // Sets a packet filter on the TX handle. If an NBL matches the packet
    // filter, it is captured by the TX context and added to the tail of the
    // packet queue.
    //
    // Captured NBLs are returned to NDIS either by closing the TX handle or by
    // dequeuing the packet and flushing the TX context.
    //
    // Zero-length filters disable packet captures.
    //

    In.Pattern = (const UCHAR *)Pattern;
    In.Mask = (const UCHAR *)Mask;
    In.Length = Length;

    return FnIoctl(Handle, FNMP_IOCTL_TX_FILTER, &In, sizeof(In), NULL, 0, NULL, NULL);
}

FNMPAPI
FNMPAPI_STATUS
FnMpTxGetFrame(
    _In_ FNMP_HANDLE Handle,
    _In_ UINT32 FrameIndex,
    _In_ UINT32 FrameSubIndex,
    _Inout_ UINT32 *FrameBufferLength,
    _Out_opt_ DATA_FRAME *Frame
    )
{
    DATA_GET_FRAME_IN In = {0};
    FNMPAPI_STATUS Result;

    //
    // Returns the contents of a captured NBL (Index) and NB (SubIndex).
    //

    In.Index = FrameIndex;
    In.SubIndex = FrameSubIndex;

    Result =
        FnIoctl(
            Handle, FNMP_IOCTL_TX_GET_FRAME, &In, sizeof(In), Frame, *FrameBufferLength,
            FrameBufferLength, NULL);

    if (SUCCEEDED(Result) && Frame != NULL) {
        //
        // The IOCTL returns pointers relative to the output address; adjust
        // each pointer into this address space.
        //

        Frame->Buffers = (DATA_BUFFER *)RTL_PTR_ADD(Frame, Frame->Buffers);

        for (UINT32 BufferIndex = 0; BufferIndex < Frame->BufferCount; BufferIndex++) {
            Frame->Buffers[BufferIndex].VirtualAddress =
                (const UCHAR *)RTL_PTR_ADD(Frame, Frame->Buffers[BufferIndex].VirtualAddress);
        }
    }

    return Result;
}

FNMPAPI
FNMPAPI_STATUS
FnMpTxDequeueFrame(
    _In_ FNMP_HANDLE Handle,
    _In_ UINT32 FrameIndex
    )
{
    DATA_DEQUEUE_FRAME_IN In = {0};

    //
    // Dequeues a packet from the TX queue and appends it to the completion
    // queue.
    //

    In.Index = FrameIndex;

    return FnIoctl(Handle, FNMP_IOCTL_TX_DEQUEUE_FRAME, &In, sizeof(In), NULL, 0, NULL, NULL);
}

FNMPAPI
FNMPAPI_STATUS
FnMpTxFlush(
    _In_ FNMP_HANDLE Handle
    )
{
    //
    // Completes all packets on the completion queue.
    //

    return FnIoctl(Handle, FNMP_IOCTL_TX_FLUSH, NULL, 0, NULL, 0, NULL, NULL);
}

FNMPAPI
FNMPAPI_STATUS
FnMpGetLastMiniportPauseTimestamp(
    _In_ FNMP_HANDLE Handle,
    _Out_ LARGE_INTEGER *Timestamp
    )
{
    //
    // Supports shared handles only.
    // Returns the QPC timestamp of the latest miniport pause event.
    //
    return
        FnIoctl(
            Handle, FNMP_IOCTL_MINIPORT_PAUSE_TIMESTAMP, NULL, 0, Timestamp,
            sizeof(*Timestamp), NULL, NULL);
}

FNMPAPI
FNMPAPI_STATUS
FnMpSetMtu(
    _In_ FNMP_HANDLE Handle,
    _In_ UINT32 Mtu
    )
{
    MINIPORT_SET_MTU_IN In = {0};

    //
    // Supports shared handles only. Updates the miniport MTU and requests
    // an NDIS data path restart.
    //

    In.Mtu = Mtu;

    return FnIoctl(Handle, FNMP_IOCTL_MINIPORT_SET_MTU, &In, sizeof(In), NULL, 0, NULL, NULL);
}

FNMPAPI
FNMPAPI_STATUS
FnMpOidFilter(
    _In_ FNMP_HANDLE Handle,
    _In_ const OID_KEY *Keys,
    _In_ UINT32 KeyCount
    )
{
    //
    // Supports exclusive handles only. Sets an OID filter for OID inspection.
    // Filtered OIDs are pended and can be fetched with FnMpOidGetRequest.
    // Handle closure will trigger the processing and completion of any
    // outstanding OIDs.
    //
    return
        FnIoctl(
            Handle, FNMP_IOCTL_OID_FILTER, (VOID *)Keys, sizeof(Keys[0]) * KeyCount, NULL, 0, NULL, NULL);
}

FNMPAPI
FNMPAPI_STATUS
FnMpOidGetRequest(
    _In_ FNMP_HANDLE Handle,
    _In_ OID_KEY Key,
    _Inout_ UINT32 *InformationBufferLength,
    _Out_opt_ VOID *InformationBuffer
    )
{
    OID_GET_REQUEST_IN In = {0};

    //
    // Supports exclusive handles only. Gets the information buffer of an OID
    // request previously pended by the OID filter set via FnMpOidFilter.
    //

    In.Key = Key;

    return
        FnIoctl(
            Handle, FNMP_IOCTL_OID_GET_REQUEST, &In, sizeof(In), InformationBuffer,
            *InformationBufferLength, InformationBufferLength, NULL);
}

FNMPAPI
FNMPAPI_STATUS
FnMpOidCompleteRequest(
    _In_ FNMP_HANDLE Handle,
    _In_ OID_KEY Key,
    _In_ NDIS_STATUS Status,
    _In_opt_ const VOID *InformationBuffer,
    _In_ UINT32 InformationBufferLength
    )
{
    OID_COMPLETE_REQUEST_IN In = {0};

    //
    // Supports exclusive handles only. Completes an OID request previously pended
    // by the OID filter set via FnMpOidFilter. If the completion status is set
    // to NDIS_STATUS_PENDING, then the information buffer parameter is ignored
    // and the regular FNMP OID processing path is invoked.
    //

    In.Key = Key;
    In.Status = Status;
    In.InformationBuffer = InformationBuffer;
    In.InformationBufferLength = InformationBufferLength;

    return FnIoctl(Handle, FNMP_IOCTL_OID_COMPLETE_REQUEST, &In, sizeof(In), NULL, 0, NULL, NULL);
}

FNMPAPI
FNMPAPI_STATUS
FnMpUpdateTaskOffload(
    _In_ FNMP_HANDLE Handle,
    _In_ FN_OFFLOAD_TYPE OffloadType,
    _In_opt_ const NDIS_OFFLOAD_PARAMETERS *OffloadParameters,
    _In_ UINT32 OffloadParametersLength
    )
{
    MINIPORT_UPDATE_TASK_OFFLOAD_IN In = {0};

    //
    // Supports shared handles only. Updates the miniport task offload state and
    // indicates the change to NDIS.
    //
    // If the offload parameters are not specified, the miniport reloads its
    // configuration from the registry. If the offload parameters are specfied,
    // the miniport handles this as if it were set via OID.
    //

    In.OffloadType = OffloadType;
    In.OffloadParameters = OffloadParameters;
    In.OffloadParametersLength = OffloadParametersLength;

    return
        FnIoctl(Handle, FNMP_IOCTL_MINIPORT_UPDATE_TASK_OFFLOAD, &In, sizeof(In), NULL, 0, NULL, NULL);
}

EXTERN_C_END
