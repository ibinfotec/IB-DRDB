/*++

Module Name:

    Driver.c

Abstract:

    This file contains the driver entry points and callbacks.

Environment:

    Kernel-mode Driver Framework

--*/

#include "Driver.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text (INIT, DriverEntry)
#pragma alloc_text (PAGE, HAStorRepEvtDriverContextCleanup)
#endif

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    WDF_DRIVER_CONFIG config;
    NTSTATUS status;
    WDF_OBJECT_ATTRIBUTES attributes;

    TraceInfo("DriverEntry: Entering HAStorRep");

    //
    // Register a cleanup callback so that we can call WPP_CLEANUP when
    // the framework driver object is deleted during DriverEntry failure.
    //
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.EvtCleanupCallback = HAStorRepEvtDriverContextCleanup;

    WDF_DRIVER_CONFIG_INIT(&config,
                           HAStorRepEvtDeviceAdd
                           );

    status = WdfDriverCreate(DriverObject,
                             RegistryPath,
                             &attributes,
                             &config,
                             WDF_NO_HANDLE
                             );

    if (!NT_SUCCESS(status)) {
        TraceError("WdfDriverCreate failed %!STATUS!", status);
        return status;
    }

    TraceInfo("DriverEntry: HAStorRep loaded successfully");
    return status;
}

VOID
HAStorRepEvtDriverContextCleanup(
    _In_ WDFOBJECT DriverObject
    )
{
    UNREFERENCED_PARAMETER(DriverObject);
    TraceInfo("HAStorRepEvtDriverContextCleanup: Driver Unload");
}
