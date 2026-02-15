/*++

Module Name:

    Device.c

Abstract:

    This file contains the device creation and IO queue configuration.

Environment:

    Kernel-mode Driver Framework

--*/

#include "Driver.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, HAStorRepCreateDevice)
#pragma alloc_text (PAGE, HAStorRepEvtDeviceAdd)
#pragma alloc_text (PAGE, HAStorRepEvtDeviceFileCreate)
#pragma alloc_text (PAGE, HAStorRepEvtFileClose)
#pragma alloc_text (PAGE, HAStorRepEvtFileCleanup)
#pragma alloc_text (PAGE, HAStorRepEvtDeviceContextCleanup)
#endif

NTSTATUS
HAStorRepEvtDeviceAdd(
    _In_    WDFDRIVER       Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
    )
{
    NTSTATUS status;

    UNREFERENCED_PARAMETER(Driver);

    TraceInfo("HAStorRepEvtDeviceAdd: Adding device");

    status = HAStorRepCreateDevice(DeviceInit);

    return status;
}

NTSTATUS
HAStorRepCreateDevice(
    _Inout_ PWDFDEVICE_INIT DeviceInit
    )
{
    WDF_OBJECT_ATTRIBUTES   deviceAttributes;
    WDFDEVICE               device;
    NTSTATUS                status;
    WDF_IO_QUEUE_CONFIG     queueConfig;
    DECLARE_CONST_UNICODE_STRING(ntDeviceName, L"\\Device\\HAStorRepCtl");
    DECLARE_CONST_UNICODE_STRING(symbolicLinkName, L"\\DosDevices\\HAStorRepCtl");
    PDEVICE_CONTEXT         deviceContext;
    WDF_FILEOBJECT_CONFIG   fileConfig;
    WDF_OBJECT_ATTRIBUTES   fileAttributes;

    PAGED_CODE();

    //
    // Set explicit device name
    //
    status = WdfDeviceInitAssignName(DeviceInit, &ntDeviceName);
    if (!NT_SUCCESS(status)) {
        TraceError("WdfDeviceInitAssignName failed %!STATUS!", status);
        return status;
    }

    //
    // Configure File Object callbacks (optional but good for tracking opens)
    //
    WDF_FILEOBJECT_CONFIG_INIT(
        &fileConfig,
        HAStorRepEvtDeviceFileCreate,
        HAStorRepEvtFileClose,
        HAStorRepEvtFileCleanup
        );

    WDF_OBJECT_ATTRIBUTES_INIT(&fileAttributes);
    WdfDeviceInitSetFileObjectConfig(DeviceInit, &fileConfig, &fileAttributes);

    //
    // Set device context
    //
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, DEVICE_CONTEXT);
    deviceAttributes.EvtCleanupCallback = HAStorRepEvtDeviceContextCleanup;

    //
    // Create the device
    //
    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);
    if (!NT_SUCCESS(status)) {
        TraceError("WdfDeviceCreate failed %!STATUS!", status);
        return status;
    }

    //
    // Create symbolic link
    //
    status = WdfDeviceCreateSymbolicLink(device, &symbolicLinkName);
    if (!NT_SUCCESS(status)) {
        TraceError("WdfDeviceCreateSymbolicLink failed %!STATUS!", status);
        return status;
    }

    //
    // Initialize Context
    //
    deviceContext = GetDeviceContext(device);
    
    status = WdfWaitLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &deviceContext->EngineLock);
    if (!NT_SUCCESS(status)) {
        TraceError("WdfWaitLockCreate failed %!STATUS!", status);
        return status;
    }

    deviceContext->EngineRegistered = FALSE;
    deviceContext->EngineFileObject = NULL;
    deviceContext->CurrentSessionId = 0;

    //
    // Initialize Ring Buffer
    //
    status = RingBufferInitialize(&deviceContext->EventRing, RING_BUFFER_CAPACITY);
    if (!NT_SUCCESS(status)) {
        TraceError("RingBufferInitialize failed %!STATUS!", status);
        return status;
    }

    //
    // Create Default Queue (Serialized) for Register/Ack
    //
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
        &queueConfig,
        WdfIoQueueDispatchSequential
        );

    queueConfig.EvtIoDeviceControl = HAStorRepEvtIoDeviceControl;

    status = WdfIoQueueCreate(
        device,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        WDF_NO_HANDLE
        );

    if (!NT_SUCCESS(status)) {
        TraceError("WdfIoQueueCreate (Default) failed %!STATUS!", status);
        RingBufferDestroy(&deviceContext->EventRing);
        return status;
    }

    //
    // Create Manual Queue for Pending GET_EVENTS
    //
    WDF_IO_QUEUE_CONFIG_INIT(
        &queueConfig,
        WdfIoQueueDispatchManual
        );
    
    // Allow cancellation
    queueConfig.PowerManaged = WdfFalse;

    status = WdfIoQueueCreate(
        device,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &deviceContext->PendingNotificationQueue
        );

    if (!NT_SUCCESS(status)) {
        TraceError("WdfIoQueueCreate (Manual) failed %!STATUS!", status);
        RingBufferDestroy(&deviceContext->EventRing);
        return status;
    }

    TraceInfo("Device created successfully");

    return status;
}

VOID
HAStorRepEvtDeviceFileCreate(
    _In_ WDFDEVICE     Device,
    _In_ WDFREQUEST    Request,
    _In_ WDFFILEOBJECT FileObject
    )
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(FileObject);
    TraceInfo("HAStorRepEvtDeviceFileCreate");
    WdfRequestComplete(Request, STATUS_SUCCESS);
}

VOID
HAStorRepEvtFileClose(
    _In_ WDFFILEOBJECT FileObject
    )
{
    UNREFERENCED_PARAMETER(FileObject);
    TraceInfo("HAStorRepEvtFileClose");
}

VOID
HAStorRepEvtFileCleanup(
    _In_ WDFFILEOBJECT FileObject
    )
{
    PDEVICE_CONTEXT deviceContext;
    WDFDEVICE device;
    PFILE_OBJECT wdmFileObject;
    BOOLEAN shouldPurge = FALSE;

    TraceInfo("HAStorRepEvtFileCleanup");
    
    device = WdfFileObjectGetDevice(FileObject);
    deviceContext = GetDeviceContext(device);
    wdmFileObject = WdfFileObjectWdmGetFileObject(FileObject);

    //
    // Synchronize access to engine state
    //
    WdfWaitLockAcquire(deviceContext->EngineLock, NULL);

    if (deviceContext->EngineRegistered && deviceContext->EngineFileObject == wdmFileObject) {
        shouldPurge = TRUE;
    }

    WdfWaitLockRelease(deviceContext->EngineLock);

    if (shouldPurge) {
        //
        // Purge queue synchronously outside of EngineLock
        //
        WdfIoQueuePurgeSynchronously(deviceContext->PendingNotificationQueue);
        WdfIoQueueStart(deviceContext->PendingNotificationQueue);

        WdfWaitLockAcquire(deviceContext->EngineLock, NULL);
        
        //
        // Final State Reset
        //
        if (deviceContext->EngineFileObject != NULL) {
            ObDereferenceObject(deviceContext->EngineFileObject);
            deviceContext->EngineFileObject = NULL;
        }
        deviceContext->EngineRegistered = FALSE;
        deviceContext->CurrentSessionId = 0;
        
        WdfWaitLockRelease(deviceContext->EngineLock);

        RingBufferResetOverflow(&deviceContext->EventRing);
        TraceInfo("Engine Cleanup: Purged and state reset completed.");
    }
}

VOID
HAStorRepEvtDeviceContextCleanup(
    _In_ WDFOBJECT Device
    )
{
    PDEVICE_CONTEXT deviceContext;
    PFILE_OBJECT engineFileToRelease = NULL;
    
    PAGED_CODE();

    TraceInfo("HAStorRepEvtDeviceContextCleanup: Cleaning up device context");

    deviceContext = GetDeviceContext(Device);

    if (deviceContext != NULL) {
        
        WdfWaitLockAcquire(deviceContext->EngineLock, NULL);

        //
        // Capture reference and reset state
        //
        engineFileToRelease = deviceContext->EngineFileObject;
        deviceContext->EngineFileObject = NULL;
        deviceContext->EngineRegistered = FALSE;
        deviceContext->CurrentSessionId = 0;
        RingBufferResetOverflow(&deviceContext->EventRing);

        WdfWaitLockRelease(deviceContext->EngineLock);

        //
        // Final Purge outside of lock
        //
        if (deviceContext->PendingNotificationQueue != NULL) {
            WdfIoQueuePurgeSynchronously(deviceContext->PendingNotificationQueue);
        }

        //
        // Release reference 
        //
        if (engineFileToRelease != NULL) {
            ObDereferenceObject(engineFileToRelease);
        }

        RingBufferDestroy(&deviceContext->EventRing);
    }
}
