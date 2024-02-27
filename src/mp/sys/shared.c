//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"
#include "shared.tmh"

NDIS_STATUS
MiniportRestartHandler(
   _In_ NDIS_HANDLE MiniportAdapterContext,
   _In_ NDIS_MINIPORT_RESTART_PARAMETERS *RestartParameters
   )
{
    ADAPTER_CONTEXT *Adapter = (ADAPTER_CONTEXT *)MiniportAdapterContext;

    TraceEnter(TRACE_CONTROL, "Adapter=%p", Adapter);

    UNREFERENCED_PARAMETER(RestartParameters);

    ExReInitializeRundownProtection(&Adapter->Shared->NblRundown);

    TraceExitSuccess(TRACE_CONTROL);

    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
MiniportPauseHandler(
   _In_ NDIS_HANDLE MiniportAdapterContext,
   _In_ NDIS_MINIPORT_PAUSE_PARAMETERS *PauseParameters
   )
{
    ADAPTER_CONTEXT *Adapter = (ADAPTER_CONTEXT *)MiniportAdapterContext;

    TraceEnter(TRACE_CONTROL, "Adapter=%p", Adapter);

    UNREFERENCED_PARAMETER(PauseParameters);

    Adapter->LastPauseTimestamp = KeQueryPerformanceCounter(NULL);
    ExWaitForRundownProtectionRelease(&Adapter->Shared->NblRundown);

    TraceExitSuccess(TRACE_CONTROL);

    return NDIS_STATUS_SUCCESS;
}

VOID
MiniportCancelSendHandler(
   _In_ NDIS_HANDLE MiniportAdapterContext,
   _In_ VOID *CancelId
   )
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(CancelId);
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
SharedIrpGetMiniportPauseTimestamp(
    _In_ SHARED_CONTEXT *Shared,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    NTSTATUS Status;
    LARGE_INTEGER Timestamp = Shared->Adapter->LastPauseTimestamp;

    if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(Timestamp)) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    *(LARGE_INTEGER *)Irp->AssociatedIrp.SystemBuffer = Timestamp;
    Status = STATUS_SUCCESS;

    Irp->IoStatus.Information = sizeof(Timestamp);

Exit:

    return Status;
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
SharedIrpSetMtu(
    _In_ SHARED_CONTEXT *Shared,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    CONST MINIPORT_SET_MTU_IN *In = Irp->AssociatedIrp.SystemBuffer;
    UINT32 NewMtu;
    NDIS_STATUS_INDICATION Indication = {0};
    NTSTATUS Status;

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(*In)) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    if (In->Mtu < FNMP_MIN_MTU || In->Mtu > FNMP_MAX_MTU) {
        //
        // Existing components (e.g. LLDP) perform dangerous arithmetic on
        // MTU values. Ensure the MTU is within tested-safe bounds.
        //
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    //
    // The MTU provided to NDIS does not include the Ethernet header length.
    // The FNMP API specifies the MTU as the total frame length, so adjust
    // for the Ethernet size here.
    //
    NewMtu = In->Mtu - ETH_HDR_LEN;
    Shared->Adapter->MtuSize = NewMtu;

    //
    // Use an undocumented API to change the MTU of an initialized miniport.
    // Unfortunately there is no documented mechanism for NDIS6 adapters.
    //
    Indication.Header.Type = NDIS_OBJECT_TYPE_STATUS_INDICATION;
    Indication.Header.Revision = NDIS_STATUS_INDICATION_REVISION_1;
    Indication.Header.Size = sizeof(Indication);
    Indication.SourceHandle = Shared->Adapter->MiniportHandle;
    Indication.StatusCode = NDIS_STATUS_L2_MTU_SIZE_CHANGE;
    Indication.StatusBuffer = &NewMtu;
    Indication.StatusBufferSize = sizeof(NewMtu);
    NdisMIndicateStatusEx(Shared->Adapter->MiniportHandle, &Indication);
    Status = STATUS_SUCCESS;

Exit:

    return Status;
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
SharedIrpUpdateTaskOffload(
    _In_ SHARED_CONTEXT *Shared,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    CONST MINIPORT_UPDATE_TASK_OFFLOAD_IN *In = Irp->AssociatedIrp.SystemBuffer;
    ADAPTER_CONTEXT *Adapter = Shared->Adapter;
    NDIS_HANDLE ConfigHandle = NULL;
    BOUNCE_BUFFER OffloadParameters;
    NTSTATUS Status;
    NDIS_OFFLOAD Offload;
    UINT32 IndicationStatus;

    BounceInitialize(&OffloadParameters);

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(*In)) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    if (In->OffloadType != FnOffloadCurrentConfig &&
        In->OffloadType != FnOffloadHardwareCapabilities) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    IndicationStatus =
        In->OffloadType == FnOffloadCurrentConfig ?
            NDIS_STATUS_TASK_OFFLOAD_CURRENT_CONFIG :
            NDIS_STATUS_TASK_OFFLOAD_HARDWARE_CAPABILITIES;

    if (In->OffloadParametersLength > 0) {
        Status =
            BounceBuffer(
                &OffloadParameters, In->OffloadParameters, In->OffloadParametersLength,
                __alignof(NDIS_OFFLOAD_PARAMETERS));
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }

        Status =
            MpSetOffloadParameters(
                Adapter, MpGetOffload(Adapter, In->OffloadType), OffloadParameters.Buffer,
                In->OffloadParametersLength, IndicationStatus);
        if (Status != NDIS_STATUS_SUCCESS) {
            goto Exit;
        }
    } else {
        Status = MpOpenConfiguration(&ConfigHandle, Adapter);
        if (Status != NDIS_STATUS_SUCCESS) {
            goto Exit;
        }

        Status = MpReadOffload(Adapter, ConfigHandle, In->OffloadType);
        if (Status != NDIS_STATUS_SUCCESS) {
            goto Exit;
        }

        MpFillOffload(&Offload, Adapter, MpGetOffload(Adapter, In->OffloadType));
        MpIndicateStatus(Adapter, &Offload, sizeof(Offload), IndicationStatus);
    }

    Status = STATUS_SUCCESS;

Exit:

    if (ConfigHandle != NULL) {
        NdisCloseConfiguration(ConfigHandle);
    }

    BounceCleanup(&OffloadParameters);

    return Status;
}

VOID
SharedAdapterCleanup(
    _In_ ADAPTER_SHARED *AdapterShared
    )
{
    if (AdapterShared->NblPool != NULL) {
        NdisFreeNetBufferListPool(AdapterShared->NblPool);
    }

    ExFreePoolWithTag(AdapterShared, POOLTAG_MP_SHARED);
}

ADAPTER_SHARED *
SharedAdapterCreate(
    _In_ ADAPTER_CONTEXT *Adapter
    )
{
    ADAPTER_SHARED *AdapterShared;
    NTSTATUS Status;

    AdapterShared = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*AdapterShared), POOLTAG_MP_SHARED);
    if (AdapterShared == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    AdapterShared->Adapter = Adapter;
    ExInitializeRundownProtection(&AdapterShared->NblRundown);
    ExWaitForRundownProtectionRelease(&AdapterShared->NblRundown);
    KeInitializeSpinLock(&AdapterShared->Lock);
    InitializeListHead(&AdapterShared->TxFilterList);

    NET_BUFFER_LIST_POOL_PARAMETERS PoolParams = {0};
    PoolParams.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    PoolParams.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParams.Header.Size = sizeof(PoolParams);
    PoolParams.fAllocateNetBuffer = TRUE;
    PoolParams.PoolTag = POOLTAG_MP_SHARED_RX;
    PoolParams.ContextSize = FNIO_ENQUEUE_NBL_CONTEXT_SIZE;

    AdapterShared->NblPool = NdisAllocateNetBufferListPool(NULL, &PoolParams);
    if (AdapterShared->NblPool == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        if (AdapterShared != NULL) {
            SharedAdapterCleanup(AdapterShared);
            AdapterShared = NULL;
        }
    }

    return AdapterShared;
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
SharedIrpDeviceIoControl(
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    SHARED_CONTEXT *Shared = IrpSp->FileObject->FsContext;
    NTSTATUS Status;

    switch (IrpSp->Parameters.DeviceIoControl.IoControlCode) {

    case IOCTL_RX_ENQUEUE:
        Status = SharedIrpRxEnqueue(Shared->Rx, Irp, IrpSp);
        break;

    case IOCTL_RX_FLUSH:
        Status = SharedIrpRxFlush(Shared->Rx, Irp, IrpSp);
        break;

    case IOCTL_TX_FILTER:
        Status = SharedIrpTxFilter(Shared->Tx, Irp, IrpSp);
        break;

    case IOCTL_TX_GET_FRAME:
        Status = SharedIrpTxGetFrame(Shared->Tx, Irp, IrpSp);
        break;

    case IOCTL_TX_DEQUEUE_FRAME:
        Status = SharedIrpTxDequeueFrame(Shared->Tx, Irp, IrpSp);
        break;

    case IOCTL_TX_FLUSH:
        Status = SharedIrpTxFlush(Shared->Tx, Irp, IrpSp);
        break;

    case IOCTL_MINIPORT_PAUSE_TIMESTAMP:
        Status = SharedIrpGetMiniportPauseTimestamp(Shared, Irp, IrpSp);
        break;

    case IOCTL_MINIPORT_SET_MTU:
        Status = SharedIrpSetMtu(Shared, Irp, IrpSp);
        break;

    case IOCTL_MINIPORT_UPDATE_TASK_OFFLOAD:
        Status = SharedIrpUpdateTaskOffload(Shared, Irp, IrpSp);
        break;

    default:
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

Exit:

    return Status;
}

static
VOID
SharedCleanup(
    _In_ SHARED_CONTEXT *Shared
    )
{
    if (Shared->Tx != NULL) {
        SharedTxCleanup(Shared->Tx);
    }

    if (Shared->Rx != NULL) {
        SharedRxCleanup(Shared->Rx);
    }

    if (Shared->Adapter != NULL) {
        MpDereferenceAdapter(Shared->Adapter);
    }

    ExFreePoolWithTag(Shared, POOLTAG_MP_SHARED);
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
SharedIrpClose(
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    UNREFERENCED_PARAMETER(Irp);

    SharedCleanup(IrpSp->FileObject->FsContext);

    return STATUS_SUCCESS;
}

static CONST FILE_DISPATCH SharedFileDispatch = {
    .IoControl = SharedIrpDeviceIoControl,
    .Close = SharedIrpClose,
};

NTSTATUS
SharedIrpCreate(
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp,
    _In_ UCHAR Disposition,
    _In_ VOID *InputBuffer,
    _In_ SIZE_T InputBufferLength
    )
{
    NTSTATUS Status;
    SHARED_CONTEXT *Shared = NULL;
    FNMP_OPEN_SHARED *OpenShared;

    UNREFERENCED_PARAMETER(Irp);
    UNREFERENCED_PARAMETER(Disposition);

    if (InputBufferLength < sizeof(*OpenShared)) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }
    OpenShared = InputBuffer;

    Shared = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Shared), POOLTAG_MP_SHARED);
    if (Shared == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    Shared->Header.ObjectType = FNMP_FILE_TYPE_SHARED;
    Shared->Header.Dispatch = &SharedFileDispatch;
    KeInitializeSpinLock(&Shared->Lock);

    Shared->Adapter = MpFindAdapter(OpenShared->IfIndex);
    if (Shared->Adapter == NULL) {
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

    Shared->Rx = SharedRxCreate(Shared);
    if (Shared->Rx == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    Shared->Tx = SharedTxCreate(Shared);
    if (Shared->Tx == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    IrpSp->FileObject->FsContext = Shared;
    Status = STATUS_SUCCESS;

Exit:

    if (!NT_SUCCESS(Status)) {
        if (Shared != NULL) {
            SharedCleanup(Shared);
        }
    }

    return Status;
}
