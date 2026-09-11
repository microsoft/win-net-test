//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Child PDO creation. The power capabilities reported here are the entire
// reason this driver exists: they are what the PnP manager intersects to decide
// whether the child's stack may be sent device power IRPs at all.
//

#include "precomp.h"
#include "pdo.tmh"

typedef struct _FNBUS_POWER_REQUEST {
    KEVENT CompletionEvent;
    NTSTATUS Status;
} FNBUS_POWER_REQUEST;

static
VOID
FnBusPowerIrpComplete(
    _In_ DEVICE_OBJECT *DeviceObject,
    _In_ UCHAR MinorFunction,
    _In_ POWER_STATE PowerState,
    _In_opt_ VOID *Context,
    _In_ IO_STATUS_BLOCK *IoStatus
    )
{
    FNBUS_POWER_REQUEST *Request = (FNBUS_POWER_REQUEST *)Context;

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(MinorFunction);
    UNREFERENCED_PARAMETER(PowerState);

    if (Request != NULL) {
        Request->Status = IoStatus->Status;
        KeSetEvent(&Request->CompletionEvent, IO_NO_INCREMENT, FALSE);
    }
}

_Use_decl_annotations_
NTSTATUS
FnBusPdoCreate(
    WDFCHILDLIST ChildList,
    PWDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER IdentificationDescription,
    PWDFDEVICE_INIT ChildInit
    )
{
    NTSTATUS Status;
    FNBUS_PDO_IDENTIFICATION *Identification;
    FNBUS_PDO_CONTEXT *PdoContext;
    WDF_OBJECT_ATTRIBUTES Attributes;
    WDF_DEVICE_PNP_CAPABILITIES PnpCapabilities;
    WDF_DEVICE_POWER_CAPABILITIES PowerCapabilities;
    WDFDEVICE ChildDevice;
    DECLARE_UNICODE_STRING_SIZE(DeviceId, 64);
    DECLARE_UNICODE_STRING_SIZE(HardwareId, 64);
    DECLARE_UNICODE_STRING_SIZE(InstanceId, 32);
    DECLARE_UNICODE_STRING_SIZE(DeviceText, 64);

    UNREFERENCED_PARAMETER(ChildList);

    Identification =
        CONTAINING_RECORD(IdentificationDescription, FNBUS_PDO_IDENTIFICATION, Header);

    TraceEnter(TRACE_CONTROL, "SerialNumber=%u", Identification->SerialNumber);

    //
    // The device ID is enumerator qualified so PnP roots the child under this
    // bus; an unqualified ID names an enumerator that does not exist and the
    // child is silently dropped from the bus relations.
    //
    Status = RtlUnicodeStringPrintf(&DeviceId, L"%ws\\%ws", FNBUS_ENUMERATOR, FNBUS_CHILD_HARDWARE_ID);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = WdfPdoInitAssignDeviceID(ChildInit, &DeviceId);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    //
    // The hardware ID must match fnmp.inf's [Manufacturer] entry verbatim so
    // FNMP is selected as the function driver for this child.
    //
    Status = RtlUnicodeStringPrintf(&HardwareId, L"%ws", FNBUS_CHILD_HARDWARE_ID);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = WdfPdoInitAddHardwareID(ChildInit, &HardwareId);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = RtlUnicodeStringPrintf(&InstanceId, L"%u", Identification->SerialNumber);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = WdfPdoInitAssignInstanceID(ChildInit, &InstanceId);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status =
        RtlUnicodeStringPrintf(
            &DeviceText, L"FNMP Adapter %u", Identification->SerialNumber);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = WdfPdoInitAddDeviceText(ChildInit, &DeviceText, &DeviceText, 0x409);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    WdfPdoInitSetDefaultLocale(ChildInit, 0x409);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, FNBUS_PDO_CONTEXT);

    Status = WdfDeviceCreate(&ChildInit, &Attributes, &ChildDevice);
    if (!NT_SUCCESS(Status)) {
        TraceError(TRACE_CONTROL, "WdfDeviceCreate failed Status=%!STATUS!", Status);
        goto Exit;
    }

    PdoContext = FnBusGetPdoContext(ChildDevice);
    PdoContext->SerialNumber = Identification->SerialNumber;

    WDF_DEVICE_PNP_CAPABILITIES_INIT(&PnpCapabilities);
    PnpCapabilities.Removable = WdfTrue;
    PnpCapabilities.EjectSupported = WdfTrue;
    PnpCapabilities.SurpriseRemovalOK = WdfTrue;
    PnpCapabilities.Address = Identification->SerialNumber;
    PnpCapabilities.UINumber = Identification->SerialNumber;
    WdfDeviceSetPnpCapabilities(ChildDevice, &PnpCapabilities);

    //
    // Advertising D1/D2/D3 plus wake is what makes the devnode power capable.
    // Without it the PnP manager reports D0 only, so the function driver's
    // stack never receives a device power IRP and NDIS has no low-power
    // transition to report to protocols.
    //
    WDF_DEVICE_POWER_CAPABILITIES_INIT(&PowerCapabilities);
    PowerCapabilities.DeviceD1 = WdfTrue;
    PowerCapabilities.DeviceD2 = WdfTrue;
    PowerCapabilities.WakeFromD0 = WdfTrue;
    PowerCapabilities.WakeFromD1 = WdfTrue;
    PowerCapabilities.WakeFromD2 = WdfTrue;
    PowerCapabilities.WakeFromD3 = WdfTrue;
    PowerCapabilities.DeviceWake = PowerDeviceD3;
    PowerCapabilities.SystemWake = PowerSystemSleeping3;

    PowerCapabilities.DeviceState[PowerSystemWorking] = PowerDeviceD0;
    PowerCapabilities.DeviceState[PowerSystemSleeping1] = PowerDeviceD1;
    PowerCapabilities.DeviceState[PowerSystemSleeping2] = PowerDeviceD2;
    PowerCapabilities.DeviceState[PowerSystemSleeping3] = PowerDeviceD3;
    PowerCapabilities.DeviceState[PowerSystemHibernate] = PowerDeviceD3;
    PowerCapabilities.DeviceState[PowerSystemShutdown] = PowerDeviceD3;

    WdfDeviceSetPowerCapabilities(ChildDevice, &PowerCapabilities);

    TraceInfo(
        TRACE_CONTROL, "Child created SerialNumber=%u Device=%p",
        Identification->SerialNumber, ChildDevice);

Exit:

    TraceExitStatus(TRACE_CONTROL);

    return Status;
}

//
// Finds a live child by serial number. The caller must hold no child list
// iteration of its own.
//
static
_IRQL_requires_(PASSIVE_LEVEL)
WDFDEVICE
FnBusPdoFind(
    _In_ WDFDEVICE Fdo,
    _In_ UINT32 SerialNumber
    )
{
    WDFCHILDLIST ChildList = WdfFdoGetDefaultChildList(Fdo);
    WDF_CHILD_LIST_ITERATOR Iterator;
    WDFDEVICE Found = NULL;

    WDF_CHILD_LIST_ITERATOR_INIT(&Iterator, WdfRetrievePresentChildren);
    WdfChildListBeginIteration(ChildList, &Iterator);

    for (;;) {
        WDF_CHILD_RETRIEVE_INFO Info;
        FNBUS_PDO_IDENTIFICATION Identification;
        WDFDEVICE ChildDevice;
        NTSTATUS Status;

        WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER_INIT(
            &Identification.Header, sizeof(Identification));
        WDF_CHILD_RETRIEVE_INFO_INIT(&Info, &Identification.Header);

        Status =
            WdfChildListRetrieveNextDevice(ChildList, &Iterator, &ChildDevice, &Info);
        if (!NT_SUCCESS(Status) || Status == STATUS_NO_MORE_ENTRIES) {
            break;
        }

        if (Identification.SerialNumber == SerialNumber) {
            Found = ChildDevice;
            break;
        }
    }

    WdfChildListEndIteration(ChildList, &Iterator);

    return Found;
}

_Use_decl_annotations_
NTSTATUS
FnBusPdoSetPower(
    WDFDEVICE Fdo,
    UINT32 SerialNumber,
    DEVICE_POWER_STATE DevicePowerState,
    BOOLEAN Wait
    )
{
    NTSTATUS Status;
    WDFDEVICE ChildDevice;
    DEVICE_OBJECT *Pdo;
    POWER_STATE PowerState;
    FNBUS_POWER_REQUEST Request;

    TraceEnter(
        TRACE_POWER, "SerialNumber=%u DevicePowerState=%u Wait=%u",
        SerialNumber, DevicePowerState, Wait);

    ChildDevice = FnBusPdoFind(Fdo, SerialNumber);
    if (ChildDevice == NULL) {
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

    Pdo = WdfDeviceWdmGetPhysicalDevice(ChildDevice);
    if (Pdo == NULL) {
        Status = STATUS_NOT_FOUND;
        goto Exit;
    }

    KeInitializeEvent(&Request.CompletionEvent, NotificationEvent, FALSE);
    Request.Status = STATUS_UNSUCCESSFUL;

    PowerState.DeviceState = DevicePowerState;

    //
    // Routing the request through the power manager rather than building an IRP
    // by hand is deliberate: the target stack sees a genuine device power IRP,
    // subject to the same handling as any other.
    //
    Status =
        PoRequestPowerIrp(
            Pdo,
            IRP_MN_SET_POWER,
            PowerState,
            Wait ? FnBusPowerIrpComplete : NULL,
            Wait ? &Request : NULL,
            NULL);
    if (Status == STATUS_PENDING && Wait) {
        KeWaitForSingleObject(
            &Request.CompletionEvent, Executive, KernelMode, FALSE, NULL);
        Status = Request.Status;
    } else if (Status == STATUS_PENDING) {
        Status = STATUS_SUCCESS;
    }

Exit:

    TraceExitStatus(TRACE_POWER);

    return Status;
}
