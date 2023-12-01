//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "tx.tmh"

typedef struct _SHARED_TX {
    SHARED_CONTEXT *Shared;
    //
    // This driver allows NBLs to be held indefinitely by user mode, which is a
    // bad practice. Unfortunately, it is necessary to hold NBLs for some
    // configurable interval for test purposes so the only question is whether a
    // watchdog is also necessary. For now, don't bother.
    //
    LIST_ENTRY DataFilterLink;
    DATA_FILTER *DataFilter;
} SHARED_TX;

static
VOID
_Requires_lock_held_(&Tx->Shared->Adapter->Shared->Lock)
SharedTxClearFilter(
    _Inout_ SHARED_TX *Tx
    )
{
    if (!IsListEmpty(&Tx->DataFilterLink)) {
        RemoveEntryList(&Tx->DataFilterLink);
        InitializeListHead(&Tx->DataFilterLink);
    }

    if (Tx->DataFilter != NULL) {
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
    NET_BUFFER_LIST *NblChain = NULL;
    SIZE_T NblCount = 0;
    KIRQL OldIrql;

    KeAcquireSpinLock(&AdapterShared->Lock, &OldIrql);

    if (Tx->DataFilter != NULL) {
        NblCount = FnIoFlushAllFrames(Tx->DataFilter, &NblChain);
    }

    SharedTxClearFilter(Tx);

    KeReleaseSpinLock(&AdapterShared->Lock, OldIrql);

    ExFreePoolWithTag(Tx, POOLTAG_SHARED_TX);

    if (NblCount > 0) {
        SharedTxCompleteNbls(AdapterShared, NblChain, NblCount);
    }
}

SHARED_TX *
SharedTxCreate(
    _In_ SHARED_CONTEXT *Shared
    )
{
    SHARED_TX *Tx;
    NTSTATUS Status;

    Tx = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Tx), POOLTAG_SHARED_TX);
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

    while (Entry != &Shared->TxFilterList) {
        SHARED_TX *Tx = CONTAINING_RECORD(Entry, SHARED_TX, DataFilterLink);
        Entry = Entry->Flink;
        if (FnIoFilterNbl(Tx->DataFilter, Nbl)) {
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
    BOOLEAN ClearOnly = FALSE;

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(*In)) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    if (In->Length == 0) {
        ClearOnly = TRUE;
    } else {
        DataFilter =
            FnIoCreateFilter(
                Irp->AssociatedIrp.SystemBuffer,
                IrpSp->Parameters.DeviceIoControl.InputBufferLength);
        if (DataFilter == NULL) {
            Status = STATUS_NO_MEMORY;
            goto Exit;
        }
    }

    KeAcquireSpinLock(&AdapterShared->Lock, &OldIrql);

    SharedTxClearFilter(Tx);

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
    NET_BUFFER_LIST *NblChain;
    SIZE_T NblCount;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(Irp);
    UNREFERENCED_PARAMETER(IrpSp);

    KeAcquireSpinLock(&AdapterShared->Lock, &OldIrql);

    if (Tx->DataFilter == NULL) {
        KeReleaseSpinLock(&AdapterShared->Lock, OldIrql);
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

    NblCount = FnIoFlushDequeuedFrames(Tx->DataFilter, &NblChain);

    KeReleaseSpinLock(&AdapterShared->Lock, OldIrql);

    if (NblCount > 0) {
        Status = STATUS_SUCCESS;
        SharedTxCompleteNbls(AdapterShared, NblChain, NblCount);
    } else {
        Status = STATUS_NOT_FOUND;
    }

Exit:

    return Status;
}
