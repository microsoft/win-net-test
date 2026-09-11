//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// FNBUS is a synthetic bus enumerator whose only purpose is to give FNMP a
// devnode that supports device power states.
//
// FNMP is normally installed on a software-enumerated (SWD) devnode. The
// software device enumerator reports no power capabilities, so the resulting
// stack can never receive an IRP_MN_SET_POWER for a D-state, which makes the
// NDIS power paths (NetEventSetPower, OID_PNP_SET_POWER, idle power-down)
// unreachable in a VM. A bus driver that reports real power capabilities for
// its children removes that restriction without any hardware.
//

#include "precomp.h"
#include "driver.tmh"

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_UNLOAD FnBusEvtDriverUnload;

static
VOID
FnBusEvtDriverUnload(
    _In_ WDFDRIVER Driver
    )
{
    TraceInfo(TRACE_CONTROL, "Driver unload");
    WPP_CLEANUP(WdfDriverWdmGetDriverObject(Driver));
}

_Use_decl_annotations_
NTSTATUS
DriverEntry(
    DRIVER_OBJECT *DriverObject,
    UNICODE_STRING *RegistryPath
    )
{
    NTSTATUS Status;
    WDF_DRIVER_CONFIG Config;

    WPP_INIT_TRACING(DriverObject, RegistryPath);

    WDF_DRIVER_CONFIG_INIT(&Config, FnBusEvtDeviceAdd);
    Config.EvtDriverUnload = FnBusEvtDriverUnload;
    Config.DriverPoolTag = 'SBnF';

    Status =
        WdfDriverCreate(
            DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &Config, WDF_NO_HANDLE);
    if (!NT_SUCCESS(Status)) {
        TraceError(TRACE_CONTROL, "WdfDriverCreate failed Status=%!STATUS!", Status);
        WPP_CLEANUP(DriverObject);
        goto Exit;
    }

    TraceInfo(TRACE_CONTROL, "FNBUS loaded");

Exit:

    return Status;
}

//
// Number of children to enumerate at start, from the driver's Parameters key.
// Absent or zero means one.
//
static
_IRQL_requires_(PASSIVE_LEVEL)
UINT32
FnBusReadChildCount(
    VOID
    )
{
    NTSTATUS Status;
    WDFKEY Key = NULL;
    ULONG Value = 0;
    DECLARE_CONST_UNICODE_STRING(ValueName, L"ChildCount");

    Status =
        WdfDriverOpenParametersRegistryKey(
            WdfGetDriver(), KEY_READ, WDF_NO_OBJECT_ATTRIBUTES, &Key);
    if (NT_SUCCESS(Status)) {
        Status = WdfRegistryQueryULong(Key, &ValueName, &Value);
        WdfRegistryClose(Key);
    }

    if (!NT_SUCCESS(Status) || Value == 0) {
        Value = 1;
    }

    return (UINT32)Value;
}

_Use_decl_annotations_
NTSTATUS
FnBusEvtDeviceAdd(
    WDFDRIVER Driver,
    PWDFDEVICE_INIT DeviceInit
    )
{
    NTSTATUS Status;
    WDF_CHILD_LIST_CONFIG ChildListConfig;
    WDF_OBJECT_ATTRIBUTES Attributes;
    WDF_IO_QUEUE_CONFIG QueueConfig;
    WDFDEVICE Device;
    WDFQUEUE Queue;
    FNBUS_FDO_CONTEXT *FdoContext;
    UINT32 ChildCount;
    DECLARE_CONST_UNICODE_STRING(DeviceName, FNBUS_DEVICE_NAME);
    DECLARE_CONST_UNICODE_STRING(SymbolicLink, L"\\DosDevices\\FNBUS");

    UNREFERENCED_PARAMETER(Driver);

    WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_BUS_EXTENDER);
    WdfDeviceInitSetExclusive(DeviceInit, FALSE);

    //
    // The dynamic child list is what lets a test add and remove NICs at
    // runtime; each entry becomes a PDO in FnBusEvtChildListCreateDevice.
    //
    WDF_CHILD_LIST_CONFIG_INIT(
        &ChildListConfig, sizeof(FNBUS_PDO_IDENTIFICATION), FnBusEvtChildListCreateDevice);
    WdfFdoInitSetDefaultChildListConfig(DeviceInit, &ChildListConfig, WDF_NO_OBJECT_ATTRIBUTES);

    Status = WdfDeviceInitAssignName(DeviceInit, &DeviceName);
    if (!NT_SUCCESS(Status)) {
        TraceError(TRACE_CONTROL, "WdfDeviceInitAssignName failed Status=%!STATUS!", Status);
        goto Exit;
    }

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, FNBUS_FDO_CONTEXT);

    Status = WdfDeviceCreate(&DeviceInit, &Attributes, &Device);
    if (!NT_SUCCESS(Status)) {
        TraceError(TRACE_CONTROL, "WdfDeviceCreate failed Status=%!STATUS!", Status);
        goto Exit;
    }

    FdoContext = FnBusGetFdoContext(Device);
    FdoContext->Device = Device;

    Status = WdfDeviceCreateSymbolicLink(Device, &SymbolicLink);
    if (!NT_SUCCESS(Status)) {
        TraceError(TRACE_CONTROL, "WdfDeviceCreateSymbolicLink failed Status=%!STATUS!", Status);
        goto Exit;
    }

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&QueueConfig, WdfIoQueueDispatchSequential);
    QueueConfig.EvtIoDeviceControl = FnBusEvtIoDeviceControl;

    Status = WdfIoQueueCreate(Device, &QueueConfig, WDF_NO_OBJECT_ATTRIBUTES, &Queue);
    if (!NT_SUCCESS(Status)) {
        TraceError(TRACE_CONTROL, "WdfIoQueueCreate failed Status=%!STATUS!", Status);
        goto Exit;
    }

    //
    // Children created up front let a deployment script bring up adapters
    // without needing a user mode tool; tests add and remove more at runtime
    // over the IOCTL interface.
    //
    ChildCount = FnBusReadChildCount();

    for (UINT32 i = 0; i < ChildCount; i++) {
        Status = FnBusAddChild(Device, i);
        if (!NT_SUCCESS(Status)) {
            TraceError(
                TRACE_CONTROL, "FnBusAddChild failed SerialNumber=%u Status=%!STATUS!", i, Status);
            goto Exit;
        }
    }

    TraceInfo(
        TRACE_CONTROL, "FNBUS FDO created Device=%p ChildCount=%u", Device, ChildCount);

Exit:

    return Status;
}

_Use_decl_annotations_
NTSTATUS
FnBusEvtChildListCreateDevice(
    WDFCHILDLIST ChildList,
    PWDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER IdentificationDescription,
    PWDFDEVICE_INIT ChildInit
    )
{
    return FnBusPdoCreate(ChildList, IdentificationDescription, ChildInit);
}
