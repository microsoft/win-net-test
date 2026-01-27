//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

typedef struct _DATA_FILTER {
    DATA_FILTER_IN Params;
    UCHAR *ContiguousBuffer;
    NBL_COUNTED_QUEUE NblQueue;
    NBL_COUNTED_QUEUE NblReturn;
} DATA_FILTER;

typedef struct _DATA_FILTER_NBL_CONTEXT {
    UINT32 Signature;
    PROCESSOR_NUMBER ProcessorNumber;
    ULONGLONG Timestamp;
} DATA_FILTER_NBL_CONTEXT;

#define DATA_FILTER_NBL_CONTEXT_SIGNATURE 'cnfD' // Dfnc

static
DATA_FILTER_NBL_CONTEXT *
FnIoFilterNblContext(
    _In_ NET_BUFFER_LIST *Nbl
    )
{
    DATA_FILTER_NBL_CONTEXT *Context = Nbl->Scratch;

    ASSERT(Context != NULL);
    ASSERT(Context->Signature == DATA_FILTER_NBL_CONTEXT_SIGNATURE);

    return Context;
}

static
VOID
FnIoFilterNblSetContext(
    _In_ NET_BUFFER_LIST *Nbl,
    _In_opt_ DATA_FILTER_NBL_CONTEXT *Context
    )
{
    if (Context != NULL) {
        Context->Signature = DATA_FILTER_NBL_CONTEXT_SIGNATURE;
    }

    Nbl->Scratch = Context;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
DATA_FILTER *
FnIoCreateFilter(
    _In_ KPROCESSOR_MODE RequestorMode,
    _In_ VOID *InputBuffer,
    _In_ UINT32 InputBufferLength
    )
{
    NTSTATUS Status;
    DATA_FILTER *Filter;

    Filter = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Filter), POOLTAG_FNIO_FILTER);
    if (Filter == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Status = FnIoIoctlBounceFilter(RequestorMode, InputBuffer, InputBufferLength, &Filter->Params);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    if (Filter->Params.Length == 0) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    Filter->ContiguousBuffer =
        ExAllocatePoolZero(NonPagedPoolNx, Filter->Params.Length, POOLTAG_FNIO_BUFFER);
    if (Filter->ContiguousBuffer == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    NdisInitializeNblCountedQueue(&Filter->NblQueue);
    NdisInitializeNblCountedQueue(&Filter->NblReturn);

Exit:

    if (!NT_SUCCESS(Status) && Filter != NULL) {
        FnIoDeleteFilter(Filter);
        Filter = NULL;
    }

    return Filter;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
FnIoDeleteFilter(
    _In_ DATA_FILTER *Filter
    )
{
    if (Filter->ContiguousBuffer != NULL) {
        ExFreePoolWithTag(Filter->ContiguousBuffer, POOLTAG_FNIO_BUFFER);
        Filter->ContiguousBuffer = NULL;
    }

    FnIoIoctlCleanupFilter(&Filter->Params);

    //
    // Clients are required to flush all frames prior to filter delete.
    //
    ASSERT(NdisIsNblCountedQueueEmpty(&Filter->NblQueue));
    ASSERT(NdisIsNblCountedQueueEmpty(&Filter->NblReturn));

    ExFreePoolWithTag(Filter, POOLTAG_FNIO_FILTER);
}

static
_IRQL_requires_max_(DISPATCH_LEVEL)
BOOLEAN
FnIoFilterNb(
    _In_ DATA_FILTER *Filter,
    _In_ NET_BUFFER *NetBuffer
    )
{
    UINT32 DataLength = min(NET_BUFFER_DATA_LENGTH(NetBuffer), Filter->Params.Length);
    UCHAR *Buffer = NdisGetDataBuffer(NetBuffer, DataLength, Filter->ContiguousBuffer, 1, 0);

    ASSERT(Buffer != NULL);

    for (UINT32 Index = 0; Index < DataLength; Index++) {
        if (((Buffer[Index] ^ Filter->Params.Pattern[Index]) & Filter->Params.Mask[Index])) {
            return FALSE;
        }
    }

    return TRUE;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
FnIoFilterNbl(
    _In_ DATA_FILTER *Filter,
    _In_ NET_BUFFER_LIST *Nbl
    )
{
    NET_BUFFER *NetBuffer = NET_BUFFER_LIST_FIRST_NB(Nbl);
    BOOLEAN MatchAnyNbs = FALSE;
    NTSTATUS Status = STATUS_SUCCESS;

    ASSERT(NetBuffer != NULL);

    do {
        if (FnIoFilterNb(Filter, NetBuffer)) {
            MatchAnyNbs = TRUE;
            break;
        }
    } while ((NetBuffer = NetBuffer->Next) != NULL);

    if (MatchAnyNbs) {
        DATA_FILTER_NBL_CONTEXT *Context;

        Context = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Context), POOLTAG_FNIO_FILTER);
        if (Context == NULL) {
            Status = STATUS_NO_MEMORY;
            goto Exit;
        }

        Context->Timestamp = KeQueryUnbiasedInterruptTime();
        KeGetCurrentProcessorNumberEx(&Context->ProcessorNumber);
        FnIoFilterNblSetContext(Nbl, Context);
        NdisAppendSingleNblToNblCountedQueue(&Filter->NblQueue, Nbl);

        Status = STATUS_PENDING;
    }

Exit:

    return Status;
}

static
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
FnIoGetFilteredFrameNb(
    _In_ DATA_FILTER *Filter,
    _In_ UINT32 Index,
    _In_ UINT32 SubIndex,
    _Out_ NET_BUFFER_LIST **Nbl,
    _Out_ NET_BUFFER **Nb
    )
{
    NTSTATUS Status;

    *Nbl = Filter->NblQueue.Queue.First;
    for (UINT32 NblIndex = 0; NblIndex < Index; NblIndex++) {
        if (*Nbl == NULL) {
            Status = STATUS_NOT_FOUND;
            goto Exit;
        }

        *Nbl = (*Nbl)->Next;
    }

    if (*Nbl == NULL) {
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

#pragma warning(suppress:6001) // '**Nbl' is not uninitialized, assigned above.
    *Nb = NET_BUFFER_LIST_FIRST_NB(*Nbl);
    ASSERT(*Nb != NULL);

    do {
        //
        // Only count NBs that match the filter.
        //
        if (FnIoFilterNb(Filter, *Nb)) {
            if (SubIndex == 0) {
                break;
            } else {
                SubIndex--;
            }
        }

        *Nb = (*Nb)->Next;
        if (*Nb == NULL) {
            Status = STATUS_NOT_FOUND;
            goto Exit;
        }
    } while (TRUE);

    Status = STATUS_SUCCESS;

Exit:

    return Status;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
FnIoGetFilteredFrame(
    _In_ DATA_FILTER *Filter,
    _In_ UINT32 Index,
    _In_ UINT32 SubIndex,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    NET_BUFFER_LIST *Nbl;
    NET_BUFFER *NetBuffer;
    MDL *Mdl;
    UINT32 OutputSize;
    UINT32 DataBytes;
    UINT8 BufferCount;
    DATA_FRAME *Frame;
    DATA_BUFFER *Buffer;
    UCHAR *Data;
    UINT32 OutputBufferLength =
        IrpSp->Parameters.DeviceIoControl.OutputBufferLength;
    VOID *OutputBuffer = Irp->AssociatedIrp.SystemBuffer;
    SIZE_T *BytesReturned = &Irp->IoStatus.Information;

    *BytesReturned = 0;

    Status = FnIoGetFilteredFrameNb(Filter, Index, SubIndex, &Nbl, &NetBuffer);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    BufferCount = 0;
    OutputSize = sizeof(*Frame);

    for (Mdl = NET_BUFFER_CURRENT_MDL(NetBuffer); Mdl != NULL; Mdl = Mdl->Next) {
        OutputSize += sizeof(*Buffer);
        OutputSize += Mdl->ByteCount;

        if (!NT_SUCCESS(RtlUInt8Add(BufferCount, 1, &BufferCount))) {
            Status = STATUS_INTEGER_OVERFLOW;
            goto Exit;
        }
    }

    if ((OutputBufferLength == 0) && (Irp->Flags & IRP_INPUT_OPERATION) == 0) {
        *BytesReturned = OutputSize;
        Status = STATUS_BUFFER_OVERFLOW;
        goto Exit;
    }

    if (OutputBufferLength < OutputSize) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    Frame = OutputBuffer;
    Buffer = (DATA_BUFFER *)(Frame + 1);
    Data = (UCHAR *)(Buffer + BufferCount);

    Frame->Output.ProcessorNumber = FnIoFilterNblContext(Nbl)->ProcessorNumber;
    Frame->Output.RssHash = NET_BUFFER_LIST_GET_HASH_VALUE(Nbl);
    Frame->Output.Checksum.Value = NET_BUFFER_LIST_INFO(Nbl, TcpIpChecksumNetBufferListInfo);
    Frame->Output.Lso.Value = NET_BUFFER_LIST_INFO(Nbl, TcpLargeSendNetBufferListInfo);
    Frame->Output.Uso.Value = NET_BUFFER_LIST_INFO(Nbl, UdpSegmentationOffloadInfo);
    Frame->BufferCount = BufferCount;
    Frame->Buffers = RTL_PTR_SUBTRACT(Buffer, Frame);

    Mdl = NET_BUFFER_CURRENT_MDL(NetBuffer);
    DataBytes = NET_BUFFER_DATA_LENGTH(NetBuffer);

    for (UINT32 BufferIndex = 0; BufferIndex < BufferCount; BufferIndex++) {
        UCHAR *MdlBuffer;

        if (Mdl == NULL) {
            Status = STATUS_UNSUCCESSFUL;
            goto Exit;
        }

        MdlBuffer = MmGetSystemAddressForMdlSafe(Mdl, LowPagePriority);
        if (MdlBuffer == NULL) {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Exit;
        }

        if (BufferIndex == 0) {
            Buffer[BufferIndex].DataOffset = NET_BUFFER_CURRENT_MDL_OFFSET(NetBuffer);
        } else {
            Buffer[BufferIndex].DataOffset = 0;
        }

        Buffer[BufferIndex].DataLength =
            min(Mdl->ByteCount - Buffer[BufferIndex].DataOffset, DataBytes);
        Buffer[BufferIndex].BufferLength =
            Buffer[BufferIndex].DataOffset + Buffer[BufferIndex].DataLength;

        RtlCopyMemory(
            Data + Buffer[BufferIndex].DataOffset,
            MdlBuffer + Buffer[BufferIndex].DataOffset,
            Buffer[BufferIndex].DataLength);
        Buffer[BufferIndex].VirtualAddress = RTL_PTR_SUBTRACT(Data, Frame);

        Data += Buffer[BufferIndex].DataOffset;
        Data += Buffer[BufferIndex].DataLength;
        DataBytes -= Buffer[BufferIndex].DataLength;
        Mdl = Mdl->Next;
    }

    *BytesReturned = OutputSize;
    Status = STATUS_SUCCESS;

Exit:

    return Status;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
FnIoSetFilteredFrame(
    _In_ DATA_FILTER *Filter,
    _In_ UINT32 Index,
    _In_ UINT32 SubIndex,
    _In_ const DATA_FRAME *Frame
    )
{
    NTSTATUS Status;
    NET_BUFFER_LIST *Nbl;
    NET_BUFFER *NetBuffer;

    if (Frame->BufferCount > 0) {
        //
        // Rewriting payload is not implemented yet.
        //
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    Status = FnIoGetFilteredFrameNb(Filter, Index, SubIndex, &Nbl, &NetBuffer);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    if (Frame->Input.Flags.RssHashQueueId) {
        //
        // Rewriting RSS hash is not implemented yet.
        //
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    if (Frame->Input.Flags.Checksum) {
        NET_BUFFER_LIST_INFO(Nbl, TcpIpChecksumNetBufferListInfo) =
            Frame->Input.Checksum.Value;
    }

    if (Frame->Input.Flags.Rsc) {
        if (Frame->Input.Rsc.Info.CoalescedSegCount < 1) {
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        NET_BUFFER_LIST_INFO(Nbl, TcpRecvSegCoalesceInfo) = Frame->Input.Rsc.Value;
    }

    if (Frame->Input.Flags.Timestamp) {
        NdisSetNblTimestampInfo(Nbl, &Frame->Input.Timestamp);
    }

    Status = STATUS_SUCCESS;

Exit:

    return Status;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
FnIoDequeueFilteredFrame(
    _In_ DATA_FILTER *Filter,
    _In_ UINT32 Index
    )
{
    NTSTATUS Status;
    NBL_COUNTED_QUEUE NblChain;
    NET_BUFFER_LIST *Nbl;

    NdisInitializeNblCountedQueue(&NblChain);

    Status = STATUS_NOT_FOUND;

    for (UINT32 NblIndex = 0; !NdisIsNblCountedQueueEmpty(&Filter->NblQueue); NblIndex++) {
        Nbl = NdisPopFirstNblFromNblCountedQueue(&Filter->NblQueue);

        if (NblIndex == Index) {
            NdisAppendSingleNblToNblCountedQueue(&Filter->NblReturn, Nbl);
            Status = STATUS_SUCCESS;
        } else {
            NdisAppendSingleNblToNblCountedQueue(&NblChain, Nbl);
        }
    }

    NdisAppendNblCountedQueueToNblCountedQueueFast(&Filter->NblQueue, &NblChain);

    return Status;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
FnIoFreeFlushedFrameContexts(
    _Inout_ NET_BUFFER_LIST *NblChain
    )
{
    while (NblChain != NULL) {
        ExFreePoolWithTag(FnIoFilterNblContext(NblChain), POOLTAG_FNIO_FILTER);
        FnIoFilterNblSetContext(NblChain, NULL);

        NblChain = NblChain->Next;
    }
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
FnIoFlushDequeuedFrames(
    _In_ DATA_FILTER *Filter,
    _Inout_ NBL_COUNTED_QUEUE *FlushQueue
    )
{
    NdisAppendNblCountedQueueToNblCountedQueueFast(FlushQueue, &Filter->NblReturn);

    FnIoFreeFlushedFrameContexts(NdisGetNblChainFromNblCountedQueue(FlushQueue));
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
FnIoFlushAllFrames(
    _In_ DATA_FILTER *Filter,
    _Inout_ NBL_COUNTED_QUEUE *FlushQueue
    )
{
    NdisAppendNblCountedQueueToNblCountedQueueFast(FlushQueue, &Filter->NblQueue);
    NdisAppendNblCountedQueueToNblCountedQueueFast(FlushQueue, &Filter->NblReturn);

    FnIoFreeFlushedFrameContexts(NdisGetNblChainFromNblCountedQueue(FlushQueue));
}

static
BOOLEAN
FnIoIsNblQueueWatchdogExpired(
    _In_ const NBL_COUNTED_QUEUE *NblQueue
    )
{
    static const ULONGLONG WatchdogGracePeriod = RTL_SEC_TO_100NANOSEC(5);
    ULONGLONG CurrentTime = KeQueryUnbiasedInterruptTime();

    for (NET_BUFFER_LIST *Nbl = NdisGetNblChainFromNblCountedQueue(NblQueue);
        Nbl != NULL;
        Nbl = Nbl->Next) {

        if (CurrentTime > FnIoFilterNblContext(Nbl)->Timestamp + WatchdogGracePeriod) {
            return TRUE;
        }
    }

    return FALSE;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
BOOLEAN
FnIoIsFilterWatchdogExpired(
    _In_ const DATA_FILTER *Filter
    )
{
    return
        FnIoIsNblQueueWatchdogExpired(&Filter->NblQueue) ||
        FnIoIsNblQueueWatchdogExpired(&Filter->NblReturn);
}
