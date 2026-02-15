/*++

Module Name:

    Ioctl.c

Abstract:

    This file contains the IOCTL handling logic.

Environment:

    Kernel-mode Driver Framework

--*/

#include "Driver.h"



BOOLEAN
TryCompleteGetEventsRequest(
    _In_ WDFREQUEST Request,
    _In_ PDEVICE_CONTEXT DeviceContext
    );

VOID
ProcessPendingEvents(
    _In_ PDEVICE_CONTEXT DeviceContext
    )
{
    if (RingBufferIsEmpty(&DeviceContext->EventRing)) {
        return;
    }

    WDFREQUEST request;
    BOOLEAN validEngine = FALSE;
    PFILE_OBJECT registeredFile = NULL;

    //
    // Requirement: Use only RetrieveNextRequest loop. 
    //
    while (NT_SUCCESS(WdfIoQueueRetrieveNextRequest(DeviceContext->PendingNotificationQueue, &request))) {
        
        WdfWaitLockAcquire(DeviceContext->EngineLock, NULL);
        validEngine = DeviceContext->EngineRegistered;
        registeredFile = DeviceContext->EngineFileObject;
        WdfWaitLockRelease(DeviceContext->EngineLock);

        if (!validEngine) {
            WdfRequestComplete(request, STATUS_INVALID_HANDLE);
            continue; 
        }

        //
        // Requirement: Validate FileObject identity
        //
        WDFFILEOBJECT wdfFileObject = WdfRequestGetFileObject(request);
        PFILE_OBJECT wdmFileObject = (wdfFileObject != NULL) ? WdfFileObjectWdmGetFileObject(wdfFileObject) : NULL;
        
        if (wdmFileObject != registeredFile) {
            WdfRequestComplete(request, STATUS_ACCESS_DENIED);
            continue;
        }

        //
        // Attempt completion.
        // Returns FALSE if ring buffer was empty.
        //
        if (!TryCompleteGetEventsRequest(request, DeviceContext)) {

            NTSTATUS st = WdfRequestForwardToIoQueue(
                request,
                DeviceContext->PendingNotificationQueue
            );

            if (!NT_SUCCESS(st)) {
                WdfRequestComplete(request, st);
            }

            return; // break -> return
        }
    }
}

BOOLEAN

// MUST be invoked at PASSIVE_LEVEL.
// This function completes WDF requests and must never be called
// from DPC/ISR context.

TryCompleteGetEventsRequest(
    _In_ WDFREQUEST Request,
    _In_ PDEVICE_CONTEXT DeviceContext
    )
{
    NTSTATUS status;
    PHA_GET_EVENTS_IN inputBuffer = NULL;
    PHA_GET_EVENTS_OUT outputBuffer = NULL;
    size_t outputLength = 0;
    ULONG popped = 0;
    ULONG maxEvents = 0;
    
    //
    // Pop attempt first to fix race
    // We don't know maxEvents yet, but we can't pop without knowing it.
    // Actually, we can retrieve input buffer first.
    //
    status = WdfRequestRetrieveInputBuffer(Request, sizeof(HA_GET_EVENTS_IN), (PVOID*)&inputBuffer, NULL);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return TRUE; // Request gone
    }
    
    maxEvents = inputBuffer->max_events;
    if (maxEvents == 0 || maxEvents > HA_MAX_EVENTS_PER_BATCH) {
        maxEvents = HA_MAX_EVENTS_PER_BATCH;
    }

    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(HA_GET_EVENTS_OUT), (PVOID*)&outputBuffer, &outputLength);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return TRUE;
    }

    if (outputLength < sizeof(HA_GET_EVENTS_OUT)) {
        WdfRequestComplete(Request, STATUS_BUFFER_TOO_SMALL);
        return TRUE;
    }

    //
    // Calculate max events based on buffer size
    //
    size_t availableSpace = outputLength - sizeof(HA_GET_EVENTS_OUT); 
    ULONG maxEventsByBuffer = (ULONG)(availableSpace / sizeof(HA_EVENT));

    if (maxEvents > maxEventsByBuffer) {
        maxEvents = maxEventsByBuffer;
    }

    if (maxEvents == 0) {
        RtlZeroMemory(outputBuffer, sizeof(HA_GET_EVENTS_OUT));
        WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, sizeof(HA_GET_EVENTS_OUT));
        return TRUE;
    }

    //
    // Requirement 2: ZeroMemory only header BEFORE pop.
    //
    RtlZeroMemory(outputBuffer, sizeof(HA_GET_EVENTS_OUT));

    //
    // ATOMIC POP ATTEMPT
    //
    popped = RingBufferPop(
        &DeviceContext->EventRing,
        (PHA_EVENT)(outputBuffer + 1), 
        maxEvents
        );

    if (popped == 0) {
        // No events were available. 
        return FALSE; 
    }

    //
    // Initialize header now that we have data
    //
    outputBuffer->count = popped;

    size_t bytesReturned = sizeof(HA_GET_EVENTS_OUT) + (popped * sizeof(HA_EVENT));
    
    TraceInfo("Completing GET_EVENTS with %u events", popped);
    WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, bytesReturned);
    return TRUE;
}

VOID
HAStorRepEvtIoDeviceControl(
    _In_ WDFQUEUE   Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t     OutputBufferLength,
    _In_ size_t     InputBufferLength,
    _In_ ULONG      IoControlCode
    )
{
    NTSTATUS status = STATUS_SUCCESS;
    WDFDEVICE device = WdfIoQueueGetDevice(Queue);
    PDEVICE_CONTEXT deviceContext = GetDeviceContext(device);
    size_t bytesReturned = 0;
    BOOLEAN locked = FALSE;

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    switch (IoControlCode) {

    case IOCTL_HA_REGISTER_ENGINE: {
        PHA_REGISTER_ENGINE_IN input = NULL;
        PHA_REGISTER_ENGINE_OUT output = NULL;
        WDFFILEOBJECT wdfFileObject;
        PFILE_OBJECT fileObject;
        LARGE_INTEGER tick;
        HA_EVENT testEvent = {0};
        BOOLEAN doGenericPush = FALSE;

        TraceInfo("IOCTL_HA_REGISTER_ENGINE");

        status = WdfRequestRetrieveInputBuffer(Request, sizeof(HA_REGISTER_ENGINE_IN), (PVOID*)&input, NULL);
        if (!NT_SUCCESS(status)) {
            TraceError("REGISTER: Invalid input buffer");
            break;
        }

        if (input->magic != HA_MAGIC) {
            TraceError("REGISTER: Invalid magic 0x%x", input->magic);
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        status = WdfRequestRetrieveOutputBuffer(Request, sizeof(HA_REGISTER_ENGINE_OUT), (PVOID*)&output, NULL);
        if (!NT_SUCCESS(status)) {
            TraceError("REGISTER: Invalid output buffer");
            break;
        }

        WdfWaitLockAcquire(deviceContext->EngineLock, NULL);
        locked = TRUE;

        if (deviceContext->EngineRegistered) {
             TraceError("REGISTER: Engine already registered");
             status = STATUS_DEVICE_ALREADY_ATTACHED; 
             goto Cleanup;
        }

        //
        // Update functionality: FileObject Ownership
        //
        wdfFileObject = WdfRequestGetFileObject(Request);
        if (wdfFileObject == NULL) {
            TraceError("REGISTER: Request has no file object");
            status = STATUS_INVALID_HANDLE;
            goto Cleanup;
        }

        fileObject = WdfFileObjectWdmGetFileObject(wdfFileObject);
        if (fileObject == NULL) {
            TraceError("REGISTER: WDM FileObject is NULL");
            status = STATUS_INVALID_HANDLE;
            goto Cleanup;
        }
        
        ObReferenceObject(fileObject);
        deviceContext->EngineFileObject = fileObject;
        deviceContext->EngineRegistered = TRUE;
        RingBufferResetOverflow(&deviceContext->EventRing); 
        
        KeQueryTickCount(&tick);
        deviceContext->CurrentSessionId = tick.QuadPart;

        output->driver_version = 0x00010000;
        output->limits = HA_MAX_EVENTS_PER_BATCH;
        output->session_id = deviceContext->CurrentSessionId;

        bytesReturned = sizeof(HA_REGISTER_ENGINE_OUT);

        TraceInfo("REGISTER: SessionID %llu registered (FileObject 0x%p)", output->session_id, fileObject);

        //
        // Prepare Test Event
        //
        testEvent.event_type = HA_EVT_TEST;
        testEvent.pid = input->pid;
        testEvent.volume_id = 0;
        KeQuerySystemTime(&tick);
        testEvent.timestamp_ns = tick.QuadPart;
        doGenericPush = TRUE;

        WdfWaitLockRelease(deviceContext->EngineLock);
        locked = FALSE;

        //
        // Push Test Event outside of EngineLock to prevent Nested Lock (SpinLock inside WaitLock)
        //
        if (doGenericPush) {
            RingBufferPush(&deviceContext->EventRing, &testEvent);
ProcessPendingEvents(deviceContext);
        }

        break;
    }

    case IOCTL_HA_GET_EVENTS: {
        PHA_GET_EVENTS_IN input = NULL;

        status = WdfRequestRetrieveInputBuffer(Request, sizeof(HA_GET_EVENTS_IN), (PVOID*)&input, NULL);
        if (!NT_SUCCESS(status)) {
            TraceError("GET_EVENTS: Invalid input");
            break;
        }

        WdfWaitLockAcquire(deviceContext->EngineLock, NULL);
        locked = TRUE;

        //
        // Requirement: Validate that the caller is the registered engine
        //
        WDFFILEOBJECT wdfFileObject = WdfRequestGetFileObject(Request);
        PFILE_OBJECT wdmFileObject = (wdfFileObject != NULL) ? WdfFileObjectWdmGetFileObject(wdfFileObject) : NULL;

        if (!deviceContext->EngineRegistered || 
            deviceContext->EngineFileObject != wdmFileObject ||
            input->session_id != deviceContext->CurrentSessionId) {
            
            TraceError("GET_EVENTS: Access Denied or Session Invalid");
            status = STATUS_INVALID_HANDLE;
            goto Cleanup;
        }

        WdfWaitLockRelease(deviceContext->EngineLock);
        locked = FALSE;

        //
        // Try pop and complete. This is atomic regarding the ring buffer.
        //
        if (TryCompleteGetEventsRequest(Request, deviceContext)) {
             return; // Completed
        }

        //
        // No events. Queue it. Pure Blocking.
        // Forward to queue outside of EngineLock.
        //
        status = WdfRequestForwardToIoQueue(Request, deviceContext->PendingNotificationQueue);
        if (!NT_SUCCESS(status)) {
            TraceError("GET_EVENTS: Failed to forward to queue %!STATUS!", status);
            break;
        }
        
        //
        // Requirement 2: Close race window.
        //
        ProcessPendingEvents(deviceContext);

        return; 
    }

    case IOCTL_HA_ACK_EVENTS: {
        PHA_ACK_EVENTS_IN input = NULL;

        status = WdfRequestRetrieveInputBuffer(Request, sizeof(HA_ACK_EVENTS_IN), (PVOID*)&input, NULL);
        if (!NT_SUCCESS(status)) {
             break;
        }

        WdfWaitLockAcquire(deviceContext->EngineLock, NULL);
        locked = TRUE;

        // Validate session
        if (!deviceContext->EngineRegistered || input->session_id != deviceContext->CurrentSessionId) {
            status = STATUS_INVALID_HANDLE;
        } else {
            // Phase 1A: Do nothing, just succeed
            bytesReturned = 0;
            status = STATUS_SUCCESS;
        }

        WdfWaitLockRelease(deviceContext->EngineLock);
        locked = FALSE;
        break;
    }

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

Cleanup:
    if (locked) {
        WdfWaitLockRelease(deviceContext->EngineLock);
    }
    WdfRequestCompleteWithInformation(Request, status, bytesReturned);
}
