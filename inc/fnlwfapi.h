//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include <fniotypes.h>
#if defined(_KERNEL_MODE)
#include <fnioctl_km.h>
#include <fnlwfapistatus_km.h>
#else
#include <fnioctl_um.h>
#include <fnlwfapistatus_um.h>
#endif
#include <fnlwfioctl.h>

EXTERN_C_START

#define FNLWFAPI inline

#define FNLWF_OPEN_EA_LENGTH \
    (sizeof(FILE_FULL_EA_INFORMATION) + \
        sizeof(FNLWF_OPEN_PACKET_NAME) + \
        sizeof(FNLWF_OPEN_PACKET))

#ifndef RTL_PTR_ADD
#define RTL_PTR_ADD(Pointer, Value) \
    ((VOID *)((ULONG_PTR)(Pointer) + (ULONG_PTR)(Value)))
#endif

typedef struct _FNLWF_LOAD_CONTEXT *FNLWF_LOAD_API_CONTEXT;
DECLARE_HANDLE(FNLWF_HANDLE);

FNLWFAPI
FNLWFAPI_STATUS
FnLwfLoadApi(
    _Out_ FNLWF_LOAD_API_CONTEXT *LoadApiContext
    )
{
    *LoadApiContext = (FNLWF_LOAD_API_CONTEXT)(ULONG_PTR)0x12345678;
    return FNLWFAPI_STATUS_SUCCESS;
}

FNLWFAPI
VOID
FnLwfUnloadApi(
    _In_ FNLWF_LOAD_API_CONTEXT LoadApiContext
    )
{
    if ((ULONG_PTR)LoadApiContext != 0x12345678) {
        __fastfail(FAST_FAIL_INVALID_ARG);
    }
}

VOID *
FnLwfInitializeEa(
    _In_ FNLWF_FILE_TYPE FileType,
    _Out_ VOID *EaBuffer,
    _In_ UINT32 EaLength
    )
{
    FILE_FULL_EA_INFORMATION *EaHeader = (FILE_FULL_EA_INFORMATION *)EaBuffer;
    FNLWF_OPEN_PACKET *OpenPacket;

    if (EaLength < FNLWF_OPEN_EA_LENGTH) {
        __fastfail(FAST_FAIL_INVALID_ARG);
    }

    RtlZeroMemory(EaHeader, sizeof(*EaHeader));
    EaHeader->EaNameLength = sizeof(FNLWF_OPEN_PACKET_NAME) - 1;
    RtlCopyMemory(EaHeader->EaName, FNLWF_OPEN_PACKET_NAME, sizeof(FNLWF_OPEN_PACKET_NAME));
    EaHeader->EaValueLength = (USHORT)(EaLength - sizeof(*EaHeader) - sizeof(FNLWF_OPEN_PACKET_NAME));

    OpenPacket = (FNLWF_OPEN_PACKET *)(EaHeader->EaName + sizeof(FNLWF_OPEN_PACKET_NAME));
    OpenPacket->ObjectType = FileType;

    return OpenPacket + 1;
}

FNLWFAPI
FNLWFAPI_STATUS
FnLwfOpenDefault(
    _In_ UINT32 IfIndex,
    _Out_ FNLWF_HANDLE *Handle
    )
{
    FNLWF_OPEN_DEFAULT *OpenDefault;
    CHAR EaBuffer[FNLWF_OPEN_EA_LENGTH + sizeof(*OpenDefault)];

    OpenDefault =
        (FNLWF_OPEN_DEFAULT *)
            FnLwfInitializeEa(FNLWF_FILE_TYPE_DEFAULT, EaBuffer, sizeof(EaBuffer));
    OpenDefault->IfIndex = IfIndex;

    return FnIoctlOpen(FNLWF_DEVICE_NAME, FILE_CREATE, EaBuffer, sizeof(EaBuffer), (FNIOCTL_HANDLE *)Handle);
}

FNLWFAPI
VOID
FnLwfClose(
    _In_ FNLWF_HANDLE Handle
    )
{
    FnIoctlClose(Handle);
}

FNLWFAPI
FNLWFAPI_STATUS
FnLwfTxEnqueue(
    _In_ FNLWF_HANDLE Handle,
    _In_ DATA_FRAME *Frame
    )
{
    DATA_ENQUEUE_IN In = {0};

    //
    // Enqueues one frame to the TX backlog.
    //

    In.Frame = *Frame;

    return FnIoctl(Handle, FNLWF_IOCTL_TX_ENQUEUE, &In, sizeof(In), NULL, 0, NULL, NULL);
}

FNLWFAPI
FNLWFAPI_STATUS
FnLwfTxFlush(
    _In_ FNLWF_HANDLE Handle,
    _In_opt_ DATA_FLUSH_OPTIONS *Options
    )
{
    DATA_FLUSH_IN In = {0};

    //
    // Indicates all frames from the TX backlog.
    //
    if (Options != NULL) {
        In.Options = *Options;
    }

    return FnIoctl(Handle, FNLWF_IOCTL_TX_FLUSH, &In, sizeof(In), NULL, 0, NULL, NULL);
}

FNLWFAPI
FNLWFAPI_STATUS
FnLwfRxFilter(
    _In_ FNLWF_HANDLE Handle,
    _In_opt_bytecount_(Length) const VOID *Pattern,
    _In_opt_bytecount_(Length) const VOID *Mask,
    _In_ UINT32 Length
    )
{
    DATA_FILTER_IN In = {0};

    //
    // Sets a packet filter on the RX handle. If an NBL matches
    // the packet filter, it is captured by the RX context and added to the tail
    // of the packet queue.
    //
    // Captured NBLs are returned to NDIS either by closing the RX handle or by
    // dequeuing the packet and flushing the RX context.
    //
    // Zero-length filters disable packet captures and release all frames.
    //

    In.Pattern = (const UCHAR *)Pattern;
    In.Mask = (const UCHAR *)Mask;
    In.Length = Length;

    return FnIoctl(Handle, FNLWF_IOCTL_RX_FILTER, &In, sizeof(In), NULL, 0, NULL, NULL);
}

FNLWFAPI
FNLWFAPI_STATUS
FnLwfRxGetFrame(
    _In_ FNLWF_HANDLE Handle,
    _In_ UINT32 FrameIndex,
    _Inout_ UINT32 *FrameBufferLength,
    _Out_opt_ DATA_FRAME *Frame
    )
{
    DATA_GET_FRAME_IN In = {0};
    FNLWFAPI_STATUS Result;

    //
    // Returns the contents of a captured NBL.
    //

    In.Index = FrameIndex;

    Result =
        FnIoctl(
            Handle, FNLWF_IOCTL_RX_GET_FRAME, &In, sizeof(In), Frame, *FrameBufferLength,
            FrameBufferLength, NULL);

    if (Result == 0 && Frame != NULL) {
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

FNLWFAPI
FNLWFAPI_STATUS
FnLwfRxDequeueFrame(
    _In_ FNLWF_HANDLE Handle,
    _In_ UINT32 FrameIndex
    )
{
    DATA_DEQUEUE_FRAME_IN In = {0};

    //
    // Dequeues a packet from the RX queue and appends it to the completion
    // queue.
    //

    In.Index = FrameIndex;

    return FnIoctl(Handle, FNLWF_IOCTL_RX_DEQUEUE_FRAME, &In, sizeof(In), NULL, 0, NULL, NULL);
}

FNLWFAPI
FNLWFAPI_STATUS
FnLwfRxFlush(
    _In_ FNLWF_HANDLE Handle
    )
{
    //
    // Completes all packets on the completion queue.
    //

    return FnIoctl(Handle, FNLWF_IOCTL_RX_FLUSH, NULL, 0, NULL, 0, NULL, NULL);
}

FNLWFAPI
FNLWFAPI_STATUS
FnLwfOidSubmitRequest(
    _In_ FNLWF_HANDLE Handle,
    _In_ OID_KEY Key,
    _Inout_ UINT32 *InformationBufferLength,
    _Inout_opt_ VOID *InformationBuffer
    )
{
    OID_SUBMIT_REQUEST_IN In = {0};

    //
    // Issues an OID and waits for completion.
    //

    In.Key = Key;
    In.InformationBuffer = InformationBuffer;
    In.InformationBufferLength = *InformationBufferLength;

    return
        FnIoctl(
            Handle, FNLWF_IOCTL_OID_SUBMIT_REQUEST, &In, sizeof(In), InformationBuffer,
            *InformationBufferLength, InformationBufferLength, NULL);
}

FNLWFAPI
FNLWFAPI_STATUS
FnLwfStatusSetFilter(
    _In_ FNLWF_HANDLE Handle,
    _In_ NDIS_STATUS StatusCode,
    _In_ BOOLEAN BlockIndications,
    _In_ BOOLEAN QueueIndications
    )
{
    STATUS_FILTER_IN In = {0};

    In.StatusCode = StatusCode;
    In.BlockIndications = BlockIndications;
    In.QueueIndications = QueueIndications;

    return FnIoctl(Handle, FNLWF_IOCTL_STATUS_SET_FILTER, &In, sizeof(In), NULL, 0, 0, NULL);
}

FNLWFAPI
FNLWFAPI_STATUS
FnLwfStatusGetIndication(
    _In_ FNLWF_HANDLE Handle,
    _Inout_ UINT32 *StatusBufferLength,
    _Out_writes_bytes_opt_(*StatusBufferLength) VOID *StatusBuffer
    )
{
    return
        FnIoctl(
            Handle, FNLWF_IOCTL_STATUS_GET_INDICATION, NULL, 0, StatusBuffer, *StatusBufferLength,
            StatusBufferLength, NULL);
}

FNLWFAPI
FNLWFAPI_STATUS
FnLwfDatapathGetState(
    _In_ FNLWF_HANDLE Handle,
    BOOLEAN *IsDatapathActive
    )
{
    return
        FnIoctl(
            Handle, FNLWF_IOCTL_DATAPATH_GET_STATE, NULL, 0, IsDatapathActive, sizeof(*IsDatapathActive),
            NULL, NULL);
}

EXTERN_C_END
