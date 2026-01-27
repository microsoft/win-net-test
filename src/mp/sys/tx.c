//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "tx.tmh"

typedef struct _SHARED_TX {
    SHARED_CONTEXT *Shared;
    LIST_ENTRY DataFilterLink;
    DATA_FILTER *DataFilter;
} SHARED_TX;

static
VOID
_Requires_lock_held_(&Tx->Shared->Adapter->Shared->Lock)
SharedTxClearFilter(
    _Inout_ SHARED_TX *Tx,
    _Inout_ NBL_COUNTED_QUEUE *NblQueue
    )
{
    if (!IsListEmpty(&Tx->DataFilterLink)) {
        RemoveEntryList(&Tx->DataFilterLink);
        InitializeListHead(&Tx->DataFilterLink);
    }

    if (Tx->DataFilter != NULL) {
        FnIoFlushAllFrames(Tx->DataFilter, NblQueue);
        FnIoDeleteFilter(Tx->DataFilter);
        Tx->DataFilter = NULL;
    }
}

VOID
SharedTxCompleteNbls(
    _In_ ADAPTER_SHARED *Shared,
    _In_ NET_BUFFER_LIST *NblChain,
    _In_ SIZE_T Count
    )
{
    ASSERT(Count <= MAXULONG);
    NdisMSendNetBufferListsComplete(Shared->Adapter->MiniportHandle, NblChain, 0);
    ExReleaseRundownProtectionEx(&Shared->NblRundown, (ULONG)Count);
}

VOID
SharedTxCleanup(
    _In_ SHARED_TX *Tx
    )
{
    ADAPTER_SHARED *AdapterShared = Tx->Shared->Adapter->Shared;
    NBL_COUNTED_QUEUE NblQueue;
    KIRQL OldIrql;

    NdisInitializeNblCountedQueue(&NblQueue);

    KeAcquireSpinLock(&AdapterShared->Lock, &OldIrql);

    SharedTxClearFilter(Tx, &NblQueue);

    KeReleaseSpinLock(&AdapterShared->Lock, OldIrql);

    ExFreePoolWithTag(Tx, POOLTAG_MP_SHARED_TX);

    if (!NdisIsNblCountedQueueEmpty(&NblQueue)) {
        SharedTxCompleteNbls(
            AdapterShared, NdisGetNblChainFromNblCountedQueue(&NblQueue), NblQueue.NblCount);
    }
}

SHARED_TX *
SharedTxCreate(
    _In_ SHARED_CONTEXT *Shared
    )
{
    SHARED_TX *Tx;
    NTSTATUS Status;

    Tx = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Tx), POOLTAG_MP_SHARED_TX);
    if (Tx == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Tx->Shared = Shared;
    InitializeListHead(&Tx->DataFilterLink);
    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        if (Tx != NULL) {
            SharedTxCleanup(Tx);
            Tx = NULL;
        }
    }

    return Tx;
}

BOOLEAN
_Requires_lock_held_(&Shared->Lock)
SharedTxFilterNbl(
    _In_ ADAPTER_SHARED *Shared,
    _In_ NET_BUFFER_LIST *Nbl
    )
{
    LIST_ENTRY *Entry = Shared->TxFilterList.Flink;
    NTSTATUS Status;

    while (Entry != &Shared->TxFilterList) {
        SHARED_TX *Tx = CONTAINING_RECORD(Entry, SHARED_TX, DataFilterLink);
        Entry = Entry->Flink;

        Status = FnIoFilterNbl(Tx->DataFilter, Nbl);

        if (!NT_SUCCESS(Status)) {
            TraceError(
                TRACE_DATAPATH, "FnIoFilterNbl failed Adapter=%p Status=%!STATUS!",
                Shared->Adapter, Status);
        }

        if (Status == STATUS_PENDING) {
            return TRUE;
        }
    }

    return FALSE;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(MINIPORT_SEND_NET_BUFFER_LISTS)
VOID
MpSendNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NET_BUFFER_LIST *NetBufferLists,
    _In_ ULONG PortNumber,
    _In_ ULONG SendFlags
    )
{
    ADAPTER_CONTEXT *Adapter = (ADAPTER_CONTEXT *)MiniportAdapterContext;
    NBL_COUNTED_QUEUE NblChain, ReturnChain;
    KIRQL OldIrql;

    TraceEnter(TRACE_DATAPATH, "Adapter=%p", Adapter);
    TraceNbls(NetBufferLists);

    UNREFERENCED_PARAMETER(PortNumber);
    UNREFERENCED_PARAMETER(SendFlags);

    NdisInitializeNblCountedQueue(&NblChain);
    NdisInitializeNblCountedQueue(&ReturnChain);
    NdisAppendNblChainToNblCountedQueue(&NblChain, NetBufferLists);

    if (!ExAcquireRundownProtectionEx(&Adapter->Shared->NblRundown, (ULONG)NblChain.NblCount)) {
        NdisMSendNetBufferListsComplete(Adapter->MiniportHandle, NetBufferLists, 0);
        return;
    }

    KeAcquireSpinLock(&Adapter->Shared->Lock, &OldIrql);

    while (!NdisIsNblCountedQueueEmpty(&NblChain)) {
        NET_BUFFER_LIST *Nbl = NdisPopFirstNblFromNblCountedQueue(&NblChain);

        if (!SharedTxFilterNbl(Adapter->Shared, Nbl)) {
            NdisAppendSingleNblToNblCountedQueue(&ReturnChain, Nbl);
        }
    }

    KeReleaseSpinLock(&Adapter->Shared->Lock, OldIrql);

    if (!NdisIsNblCountedQueueEmpty(&ReturnChain)) {
        SharedTxCompleteNbls(
            Adapter->Shared, NdisGetNblChainFromNblCountedQueue(&ReturnChain),
            ReturnChain.NblCount);
    }

    TraceExitSuccess(TRACE_DATAPATH);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
SharedTxWatchdogTimeout(
    _In_ ADAPTER_SHARED *AdapterShared
    )
{
    ADAPTER_CONTEXT *Adapter = AdapterShared->Adapter;
    LIST_ENTRY *Entry;
    KIRQL OldIrql;
    NBL_COUNTED_QUEUE NblQueue;

    NdisInitializeNblCountedQueue(&NblQueue);

    KeAcquireSpinLock(&AdapterShared->Lock, &OldIrql);

    Entry = AdapterShared->TxFilterList.Flink;

    while (Entry != &AdapterShared->TxFilterList) {
        SHARED_TX *Tx = CONTAINING_RECORD(Entry, SHARED_TX, DataFilterLink);
        Entry = Entry->Flink;

        if (FnIoIsFilterWatchdogExpired(Tx->DataFilter)) {
            MpWatchdogFailure(Adapter, "NBL");
            SharedTxClearFilter(Tx, &NblQueue);
        }
    }

    KeReleaseSpinLock(&AdapterShared->Lock, OldIrql);

    if (!NdisIsNblCountedQueueEmpty(&NblQueue)) {
        SharedTxCompleteNbls(
            AdapterShared, NdisGetNblChainFromNblCountedQueue(&NblQueue), NblQueue.NblCount);
    }
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
SharedIrpTxFilter(
    _In_ SHARED_TX *Tx,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    ADAPTER_SHARED *AdapterShared = Tx->Shared->Adapter->Shared;
    CONST DATA_FILTER_IN *In = Irp->AssociatedIrp.SystemBuffer;
    DATA_FILTER *DataFilter = NULL;
    KIRQL OldIrql;
    NBL_COUNTED_QUEUE NblQueue;
    BOOLEAN ClearOnly = FALSE;

    NdisInitializeNblCountedQueue(&NblQueue);

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(*In)) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    if (In->Length == 0) {
        ClearOnly = TRUE;
    } else {
        DataFilter =
            FnIoCreateFilter(
                Irp->RequestorMode, Irp->AssociatedIrp.SystemBuffer,
                IrpSp->Parameters.DeviceIoControl.InputBufferLength);
        if (DataFilter == NULL) {
            Status = STATUS_NO_MEMORY;
            goto Exit;
        }
    }

    KeAcquireSpinLock(&AdapterShared->Lock, &OldIrql);

    SharedTxClearFilter(Tx, &NblQueue);

    if (!ClearOnly) {
        Tx->DataFilter = DataFilter;
        DataFilter = NULL;
        InsertTailList(&AdapterShared->TxFilterList, &Tx->DataFilterLink);
    }

    KeReleaseSpinLock(&AdapterShared->Lock, OldIrql);

    Status = STATUS_SUCCESS;

Exit:

    if (DataFilter != NULL) {
        FnIoDeleteFilter(DataFilter);
    }

    if (!NdisIsNblCountedQueueEmpty(&NblQueue)) {
        Status = STATUS_SUCCESS;
        SharedTxCompleteNbls(
            AdapterShared, NdisGetNblChainFromNblCountedQueue(&NblQueue), NblQueue.NblCount);
    }

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
SharedIrpTxGetFrame(
    _In_ SHARED_TX *Tx,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    ADAPTER_SHARED *AdapterShared = Tx->Shared->Adapter->Shared;
    DATA_GET_FRAME_IN *In = Irp->AssociatedIrp.SystemBuffer;
    KIRQL OldIrql;

    KeAcquireSpinLock(&AdapterShared->Lock, &OldIrql);

    if (Tx->DataFilter == NULL) {
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(*In)) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    Status = FnIoGetFilteredFrame(Tx->DataFilter, In->Index, In->SubIndex, Irp, IrpSp);

Exit:

    KeReleaseSpinLock(&AdapterShared->Lock, OldIrql);

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
SharedIrpTxSetFrame(
    _In_ SHARED_TX *Tx,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    ADAPTER_SHARED *AdapterShared = Tx->Shared->Adapter->Shared;
    DATA_SET_FRAME_IN *In = Irp->AssociatedIrp.SystemBuffer;
    KIRQL OldIrql;

    KeAcquireSpinLock(&AdapterShared->Lock, &OldIrql);

    if (Tx->DataFilter == NULL) {
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(*In)) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    Status = FnIoSetFilteredFrame(Tx->DataFilter, In->Index, In->SubIndex, &In->Frame);

Exit:

    KeReleaseSpinLock(&AdapterShared->Lock, OldIrql);

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
SharedIrpTxDequeueFrame(
    _In_ SHARED_TX *Tx,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    ADAPTER_SHARED *AdapterShared = Tx->Shared->Adapter->Shared;
    DATA_DEQUEUE_FRAME_IN *In = Irp->AssociatedIrp.SystemBuffer;
    KIRQL OldIrql;

    KeAcquireSpinLock(&AdapterShared->Lock, &OldIrql);

    if (Tx->DataFilter == NULL) {
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(*In)) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    Status = FnIoDequeueFilteredFrame(Tx->DataFilter, In->Index);

Exit:

    KeReleaseSpinLock(&AdapterShared->Lock, OldIrql);

    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
SharedIrpTxFlush(
    _In_ SHARED_TX *Tx,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    ADAPTER_SHARED *AdapterShared = Tx->Shared->Adapter->Shared;
    NBL_COUNTED_QUEUE NblQueue;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(Irp);
    UNREFERENCED_PARAMETER(IrpSp);

    NdisInitializeNblCountedQueue(&NblQueue);

    KeAcquireSpinLock(&AdapterShared->Lock, &OldIrql);

    if (Tx->DataFilter == NULL) {
        KeReleaseSpinLock(&AdapterShared->Lock, OldIrql);
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

    FnIoFlushDequeuedFrames(Tx->DataFilter, &NblQueue);

    KeReleaseSpinLock(&AdapterShared->Lock, OldIrql);

    if (!NdisIsNblCountedQueueEmpty(&NblQueue)) {
        Status = STATUS_SUCCESS;
        SharedTxCompleteNbls(
            AdapterShared, NdisGetNblChainFromNblCountedQueue(&NblQueue), NblQueue.NblCount);
    } else {
        Status = STATUS_NOT_FOUND;
    }

Exit:

    return Status;
}
