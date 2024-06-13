//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include "precomp.h"

typedef struct _FNLWF_OID_CLONE {
    NDIS_OID_REQUEST *OriginalRequest;
} FNLWF_OID_CLONE;

C_ASSERT(sizeof(FNLWF_OID_CLONE) <= RTL_FIELD_SIZE(NDIS_OID_REQUEST, SourceReserved));

typedef struct _FNLWF_OID_REQUEST {
    NDIS_OID_REQUEST Oid;
    NDIS_STATUS Status;
    KEVENT CompletionEvent;
} FNLWF_OID_REQUEST;

static
NDIS_STATUS
InvokeNdisFOidRequest(
    _In_ NDIS_HANDLE NdisFilterHandle,
    _In_ NDIS_OID_REQUEST *NdisRequest,
    _In_ OID_REQUEST_INTERFACE RequestInterface
    )
{
    NDIS_STATUS Status;

    switch (RequestInterface) {
    case OID_REQUEST_INTERFACE_REGULAR:
        Status = NdisFOidRequest(NdisFilterHandle, NdisRequest);
        break;

    case OID_REQUEST_INTERFACE_DIRECT:
        Status = NdisFDirectOidRequest(NdisFilterHandle, NdisRequest);
        break;

    default:
        ASSERT(FALSE);
        Status = NDIS_STATUS_INVALID_PARAMETER;
        break;
    }

    return Status;
}

static
VOID
InvokeNdisFOidRequestComplete(
    _In_ NDIS_HANDLE NdisFilterHandle,
    _In_ NDIS_OID_REQUEST *NdisRequest,
    _In_ NDIS_STATUS Status,
    _In_ OID_REQUEST_INTERFACE RequestInterface
    )
{
    switch (RequestInterface) {
    case OID_REQUEST_INTERFACE_REGULAR:
        NdisFOidRequestComplete(NdisFilterHandle, NdisRequest, Status);
        break;

    case OID_REQUEST_INTERFACE_DIRECT:
        NdisFDirectOidRequestComplete(NdisFilterHandle, NdisRequest, Status);
        break;

    default:
        FRE_ASSERT(FALSE);
        break;
    }
}

static
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
OidInternalRequestComplete(
    _In_ LWF_FILTER *Filter,
    _In_ NDIS_OID_REQUEST *Request,
    _In_ NDIS_STATUS Status
    )
{
    FNLWF_OID_REQUEST *FilterRequest;

    UNREFERENCED_PARAMETER(Filter);

    FilterRequest = CONTAINING_RECORD(Request, FNLWF_OID_REQUEST, Oid);
    FilterRequest->Status = Status;
    KeSetEvent(&FilterRequest->CompletionEvent, 0, FALSE);
}

VOID
FilterOidRequestCompleteCommon(
    _In_ LWF_FILTER *Filter,
    _In_ NDIS_OID_REQUEST *Request,
    _In_ NDIS_STATUS Status,
    _In_ OID_REQUEST_INTERFACE RequestInterface
    )
{
    NDIS_OID_REQUEST *OriginalRequest;
    FNLWF_OID_CLONE *Context;

    Context = (FNLWF_OID_CLONE *)(&Request->SourceReserved[0]);
    OriginalRequest = Context->OriginalRequest;

    if (OriginalRequest == NULL) {
        OidInternalRequestComplete(Filter, Request, Status);
        return;
    }

    //
    // Copy the information from the returned request to the original request
    //
    switch(Request->RequestType)
    {
    case NdisRequestMethod:
        OriginalRequest->DATA.METHOD_INFORMATION.OutputBufferLength =
            Request->DATA.METHOD_INFORMATION.OutputBufferLength;
        OriginalRequest->DATA.METHOD_INFORMATION.BytesRead =
            Request->DATA.METHOD_INFORMATION.BytesRead;
        OriginalRequest->DATA.METHOD_INFORMATION.BytesNeeded =
            Request->DATA.METHOD_INFORMATION.BytesNeeded;
        OriginalRequest->DATA.METHOD_INFORMATION.BytesWritten =
            Request->DATA.METHOD_INFORMATION.BytesWritten;
        break;

    case NdisRequestSetInformation:
        OriginalRequest->DATA.SET_INFORMATION.BytesRead =
            Request->DATA.SET_INFORMATION.BytesRead;
        OriginalRequest->DATA.SET_INFORMATION.BytesNeeded =
            Request->DATA.SET_INFORMATION.BytesNeeded;
        break;

    case NdisRequestQueryInformation:
    case NdisRequestQueryStatistics:
    default:
        OriginalRequest->DATA.QUERY_INFORMATION.BytesWritten =
            Request->DATA.QUERY_INFORMATION.BytesWritten;
        OriginalRequest->DATA.QUERY_INFORMATION.BytesNeeded =
            Request->DATA.QUERY_INFORMATION.BytesNeeded;
        break;
    }

    NdisFreeCloneOidRequest(Filter->NdisFilterHandle, Request);
    InvokeNdisFOidRequestComplete(
        Filter->NdisFilterHandle, OriginalRequest, Status, RequestInterface);
}

_Use_decl_annotations_
VOID
FilterOidRequestComplete(
    NDIS_HANDLE FilterModuleContext,
    NDIS_OID_REQUEST *Request,
    NDIS_STATUS Status
    )
{
    LWF_FILTER *Filter = (LWF_FILTER *)FilterModuleContext;

    FilterOidRequestCompleteCommon(Filter, Request, Status, OID_REQUEST_INTERFACE_REGULAR);
}

_Use_decl_annotations_
VOID
FilterDirectOidRequestComplete(
    NDIS_HANDLE FilterModuleContext,
    NDIS_OID_REQUEST *Request,
    NDIS_STATUS Status
    )
{
    LWF_FILTER *Filter = (LWF_FILTER *)FilterModuleContext;

    FilterOidRequestCompleteCommon(Filter, Request, Status, OID_REQUEST_INTERFACE_DIRECT);
}

static
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
OidInternalRequest(
    _In_ LWF_FILTER *Filter,
    _In_ NDIS_REQUEST_TYPE RequestType,
    _In_ NDIS_OID Oid,
    _In_ OID_REQUEST_INTERFACE RequestInterface,
    _Inout_updates_bytes_to_(InformationBufferLength, *BytesProcessed)
        VOID *InformationBuffer,
    _In_ ULONG InformationBufferLength,
    _In_opt_ ULONG OutputBufferLength,
    _In_ ULONG MethodId,
    _Out_ ULONG *BytesProcessed
    )
{
    FNLWF_OID_REQUEST FilterRequest;
    NDIS_OID_REQUEST *NdisRequest = &FilterRequest.Oid;
    NDIS_STATUS Status;

    *BytesProcessed = 0;
    RtlZeroMemory(NdisRequest, sizeof(NDIS_OID_REQUEST));

    KeInitializeEvent(&FilterRequest.CompletionEvent, NotificationEvent, FALSE);

    NdisRequest->Header.Type = NDIS_OBJECT_TYPE_OID_REQUEST;
    NdisRequest->Header.Revision = NDIS_OID_REQUEST_REVISION_2;
    NdisRequest->Header.Size = sizeof(NDIS_OID_REQUEST);
    NdisRequest->RequestType = RequestType;

    switch (RequestType) {
    case NdisRequestQueryInformation:
        NdisRequest->DATA.QUERY_INFORMATION.Oid = Oid;
        NdisRequest->DATA.QUERY_INFORMATION.InformationBuffer =
            InformationBuffer;
        NdisRequest->DATA.QUERY_INFORMATION.InformationBufferLength =
            InformationBufferLength;
        break;

    case NdisRequestSetInformation:
        NdisRequest->DATA.SET_INFORMATION.Oid = Oid;
        NdisRequest->DATA.SET_INFORMATION.InformationBuffer =
            InformationBuffer;
        NdisRequest->DATA.SET_INFORMATION.InformationBufferLength =
            InformationBufferLength;
        break;

    case NdisRequestMethod:
        NdisRequest->DATA.METHOD_INFORMATION.Oid = Oid;
        NdisRequest->DATA.METHOD_INFORMATION.MethodId = MethodId;
        NdisRequest->DATA.METHOD_INFORMATION.InformationBuffer =
            InformationBuffer;
        NdisRequest->DATA.METHOD_INFORMATION.InputBufferLength =
            InformationBufferLength;
        NdisRequest->DATA.METHOD_INFORMATION.OutputBufferLength =
            OutputBufferLength;
        break;

    default:
        ASSERT(FALSE);
        break;
    }

    NdisRequest->RequestId = (VOID *)OidInternalRequest;

    if (ExAcquireRundownProtection(&Filter->OidRundown)) {
        Status = InvokeNdisFOidRequest(Filter->NdisFilterHandle, NdisRequest, RequestInterface);

        //
        // NDIS will not detach the filter until all outstanding OIDs are
        // completed, so we can release the rundown as soon as NdisFOidRequest
        // returns.
        //
        ExReleaseRundownProtection(&Filter->OidRundown);
    } else {
        Status = NDIS_STATUS_ADAPTER_REMOVED;
    }

    if (Status == NDIS_STATUS_PENDING) {
        KeWaitForSingleObject(
            &FilterRequest.CompletionEvent, Executive, KernelMode, FALSE, NULL);
        Status = FilterRequest.Status;
    }

    if (Status == NDIS_STATUS_INVALID_LENGTH) {
        // Map NDIS status to STATUS_BUFFER_TOO_SMALL.
        Status = NDIS_STATUS_BUFFER_TOO_SHORT;
    }

    if (Status == NDIS_STATUS_SUCCESS) {
        if (RequestType == NdisRequestSetInformation) {
            *BytesProcessed = NdisRequest->DATA.SET_INFORMATION.BytesRead;
        }

        if (RequestType == NdisRequestQueryInformation) {
            *BytesProcessed = NdisRequest->DATA.QUERY_INFORMATION.BytesWritten;
        }

        if (RequestType == NdisRequestMethod) {
            *BytesProcessed = NdisRequest->DATA.METHOD_INFORMATION.BytesWritten;
        }
    } else if (Status == NDIS_STATUS_BUFFER_TOO_SHORT) {
        if (RequestType == NdisRequestSetInformation) {
            *BytesProcessed = NdisRequest->DATA.SET_INFORMATION.BytesNeeded;
        }

        if (RequestType == NdisRequestQueryInformation) {
            *BytesProcessed = NdisRequest->DATA.QUERY_INFORMATION.BytesNeeded;
        }

        if (RequestType == NdisRequestMethod) {
            *BytesProcessed = NdisRequest->DATA.METHOD_INFORMATION.BytesNeeded;
        }
    }

    return FnConvertNdisStatusToNtStatus(Status);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
OidIrpSubmitRequest(
    _In_ LWF_FILTER *Filter,
    _In_ IRP *Irp,
    _In_ IO_STACK_LOCATION *IrpSp
    )
{
    OID_SUBMIT_REQUEST_IN *In = Irp->AssociatedIrp.SystemBuffer;
    UINT32 OutputBufferLength =
        IrpSp->Parameters.DeviceIoControl.OutputBufferLength;
    VOID *OutputBuffer = Irp->AssociatedIrp.SystemBuffer;
    SIZE_T *BytesReturned = &Irp->IoStatus.Information;
    NTSTATUS Status;
    BOUNCE_BUFFER InfoBuffer;
    ULONG RequiredSize = 0;

    BounceInitialize(&InfoBuffer);
    *BytesReturned = 0;

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(*In)) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    if ((UINT32)In->Key.RequestInterface >= (UINT32)OID_REQUEST_INTERFACE_MAX) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    Status =
        BounceBuffer(
            &InfoBuffer, Irp->RequestorMode, In->InformationBuffer,
            In->InformationBufferLength, __alignof(UCHAR));
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status =
        OidInternalRequest(
            Filter, In->Key.RequestType, In->Key.Oid, In->Key.RequestInterface, InfoBuffer.Buffer,
            In->InformationBufferLength, OutputBufferLength, 0, &RequiredSize);

    if (Status == STATUS_BUFFER_TOO_SMALL &&
        (OutputBufferLength == 0) && (Irp->Flags & IRP_INPUT_OPERATION) == 0) {
        *BytesReturned = RequiredSize;
        Status = STATUS_BUFFER_OVERFLOW;
        goto Exit;
    }

    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    if (OutputBufferLength < RequiredSize) {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    *BytesReturned = RequiredSize;

    RtlCopyMemory(OutputBuffer, InfoBuffer.Buffer, RequiredSize);

Exit:

    BounceCleanup(&InfoBuffer);

    return Status;
}

static
NDIS_STATUS
FilterOidRequestCommon(
    _In_ LWF_FILTER *Filter,
    _In_ NDIS_OID_REQUEST *Request,
    _In_ OID_REQUEST_INTERFACE RequestInterface
    )
{
    NDIS_STATUS Status;
    NDIS_OID_REQUEST *ClonedRequest = NULL;
    FNLWF_OID_CLONE *Context;

    Status =
        NdisAllocateCloneOidRequest(
            Filter->NdisFilterHandle, Request, POOLTAG_LWF_OID, &ClonedRequest);
    if (Status != NDIS_STATUS_SUCCESS) {
        goto Exit;
    }

    Context = (FNLWF_OID_CLONE *)(&ClonedRequest->SourceReserved[0]);
    Context->OriginalRequest = Request;
    ClonedRequest->RequestId = Request->RequestId;

    Status = InvokeNdisFOidRequest(Filter->NdisFilterHandle, ClonedRequest, RequestInterface);
    if (Status != NDIS_STATUS_PENDING) {
        FilterOidRequestCompleteCommon(Filter, ClonedRequest, Status, RequestInterface);
        Status = NDIS_STATUS_PENDING;
    }

Exit:

    return Status;
}

_Use_decl_annotations_
NDIS_STATUS
FilterOidRequest(
    NDIS_HANDLE FilterModuleContext,
    NDIS_OID_REQUEST *Request
    )
{
    LWF_FILTER *Filter = (LWF_FILTER *)FilterModuleContext;

    return FilterOidRequestCommon(Filter, Request, OID_REQUEST_INTERFACE_REGULAR);
}

_Use_decl_annotations_
NDIS_STATUS
FilterDirectOidRequest(
    NDIS_HANDLE FilterModuleContext,
    NDIS_OID_REQUEST *Request
    )
{
    LWF_FILTER *Filter = (LWF_FILTER *)FilterModuleContext;

    return FilterOidRequestCommon(Filter, Request, OID_REQUEST_INTERFACE_DIRECT);
}
