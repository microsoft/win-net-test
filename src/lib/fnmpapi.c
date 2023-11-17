//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

HRESULT
FnMpOpenShared(
    _In_ UINT32 IfIndex,
    _Out_ HANDLE *Handle
    )
{
    FNMP_OPEN_SHARED *OpenShared;
    CHAR EaBuffer[FNMP_OPEN_EA_LENGTH + sizeof(*OpenShared)];

    OpenShared = FnMpInitializeEa(FNMP_FILE_TYPE_SHARED, EaBuffer, sizeof(EaBuffer));
    OpenShared->IfIndex = IfIndex;

    return FnMpOpen(FILE_CREATE, EaBuffer, sizeof(EaBuffer), Handle);
}

HRESULT
FnMpOpenExclusive(
    _In_ UINT32 IfIndex,
    _Out_ HANDLE *Handle
    )
{
    FNMP_OPEN_EXCLUSIVE *OpenExclusive;
    CHAR EaBuffer[FNMP_OPEN_EA_LENGTH + sizeof(*OpenExclusive)];

    OpenExclusive = FnMpInitializeEa(FNMP_FILE_TYPE_EXCLUSIVE, EaBuffer, sizeof(EaBuffer));
    OpenExclusive->IfIndex = IfIndex;

    return FnMpOpen(FILE_CREATE, EaBuffer, sizeof(EaBuffer), Handle);
}

HRESULT
FnMpRxEnqueue(
    _In_ HANDLE Handle,
    _In_ DATA_FRAME *Frame
    )
{
    DATA_ENQUEUE_IN In = {0};

    //
    // Supports shared handles.
    // Enqueues one frame to the RX backlog.
    //

    In.Frame = *Frame;

    return FnMpIoctl(Handle, IOCTL_RX_ENQUEUE, &In, sizeof(In), NULL, 0, NULL, NULL);
}

HRESULT
FnMpRxFlush(
    _In_ HANDLE Handle,
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

    return FnMpIoctl(Handle, IOCTL_RX_FLUSH, &In, sizeof(In), NULL, 0, NULL, NULL);
}

HRESULT
FnMpTxFilter(
    _In_ HANDLE Handle,
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

    In.Pattern = Pattern;
    In.Mask = Mask;
    In.Length = Length;

    return FnMpIoctl(Handle, IOCTL_TX_FILTER, &In, sizeof(In), NULL, 0, NULL, NULL);
}

HRESULT
FnMpTxGetFrame(
    _In_ HANDLE Handle,
    _In_ UINT32 FrameIndex,
    _Inout_ UINT32 *FrameBufferLength,
    _Out_opt_ DATA_FRAME *Frame
    )
{
    DATA_GET_FRAME_IN In = {0};
    HRESULT Result;

    //
    // Returns the contents of a captured NBL.
    //

    In.Index = FrameIndex;

    Result =
        FnMpIoctl(
            Handle, IOCTL_TX_GET_FRAME, &In, sizeof(In), Frame, *FrameBufferLength,
            FrameBufferLength, NULL);

    if (SUCCEEDED(Result) && Frame != NULL) {
        //
        // The IOCTL returns pointers relative to the output address; adjust
        // each pointer into this address space.
        //

        Frame->Buffers = RTL_PTR_ADD(Frame, Frame->Buffers);

        for (UINT32 BufferIndex = 0; BufferIndex < Frame->BufferCount; BufferIndex++) {
            Frame->Buffers[BufferIndex].VirtualAddress =
                RTL_PTR_ADD(Frame, Frame->Buffers[BufferIndex].VirtualAddress);
        }
    }

    return Result;
}

HRESULT
FnMpTxDequeueFrame(
    _In_ HANDLE Handle,
    _In_ UINT32 FrameIndex
    )
{
    DATA_DEQUEUE_FRAME_IN In = {0};

    //
    // Dequeues a packet from the TX queue and appends it to the completion
    // queue.
    //

    In.Index = FrameIndex;

    return FnMpIoctl(Handle, IOCTL_TX_DEQUEUE_FRAME, &In, sizeof(In), NULL, 0, NULL, NULL);
}

HRESULT
FnMpTxFlush(
    _In_ HANDLE Handle
    )
{
    //
    // Completes all packets on the completion queue.
    //

    return FnMpIoctl(Handle, IOCTL_TX_FLUSH, NULL, 0, NULL, 0, NULL, NULL);
}

HRESULT
FnMpGetLastMiniportPauseTimestamp(
    _In_ HANDLE Handle,
    _Out_ LARGE_INTEGER *Timestamp
    )
{
    //
    // Supports shared handles only.
    // Returns the QPC timestamp of the latest miniport pause event.
    //
    return
        FnMpIoctl(
            Handle, IOCTL_MINIPORT_PAUSE_TIMESTAMP, NULL, 0, Timestamp,
            sizeof(*Timestamp), NULL, NULL);
}

HRESULT
FnMpSetMtu(
    _In_ HANDLE Handle,
    _In_ UINT32 Mtu
    )
{
    MINIPORT_SET_MTU_IN In = {0};

    //
    // Supports shared handles only. Updates the miniport MTU and requests
    // an NDIS data path restart.
    //

    In.Mtu = Mtu;

    return FnMpIoctl(Handle, IOCTL_MINIPORT_SET_MTU, &In, sizeof(In), NULL, 0, NULL, NULL);
}

HRESULT
FnMpOidFilter(
    _In_ HANDLE Handle,
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
        FnMpIoctl(
            Handle, IOCTL_OID_FILTER, (VOID *)Keys, sizeof(Keys[0]) * KeyCount, NULL, 0, NULL, NULL);
}

HRESULT
FnMpOidGetRequest(
    _In_ HANDLE Handle,
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
        FnMpIoctl(
            Handle, IOCTL_OID_GET_REQUEST, &In, sizeof(In), InformationBuffer,
            *InformationBufferLength, InformationBufferLength, NULL);
}

HRESULT
FnMpOidCompleteRequest(
    _In_ HANDLE Handle,
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

    return FnMpIoctl(Handle, IOCTL_OID_COMPLETE_REQUEST, &In, sizeof(In), NULL, 0, NULL, NULL);
}
