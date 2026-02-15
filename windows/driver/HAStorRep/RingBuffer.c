/*++

Module Name:

    RingBuffer.c

Abstract:

    Implementation of the event ring buffer.

Environment:

    Kernel mode

--*/

#include "Driver.h"

NTSTATUS
RingBufferInitialize(
    _Inout_ PRING_BUFFER RingBuffer,
    _In_ ULONG Capacity
    )
{
    NTSTATUS status;

    if (Capacity == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    RingBuffer->Capacity = Capacity;
    RingBuffer->Head = 0;
    RingBuffer->Tail = 0;
    RingBuffer->Count = 0;
    RingBuffer->Initialized = FALSE;
    RingBuffer->Overflowed = FALSE;

    //
    // Allocate buffer from NonPagedPoolNx
    //
    RingBuffer->Events = (HA_EVENT*)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        sizeof(HA_EVENT) * Capacity,
        'bRaH' // HaRb
        );

    if (RingBuffer->Events == NULL) {
        TraceError("Failed to allocate ring buffer events");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    status = WdfSpinLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &RingBuffer->Lock);
    if (!NT_SUCCESS(status)) {
        TraceError("Failed to create spinlock: %!STATUS!", status);
        ExFreePool(RingBuffer->Events);
        RingBuffer->Events = NULL;
        return status;
    }

    RingBuffer->Initialized = TRUE;
    TraceInfo("RingBuffer initialized with capacity %u", Capacity);
    return STATUS_SUCCESS;
}

VOID
RingBufferDestroy(
    _Inout_ PRING_BUFFER RingBuffer
    )
{
    if (RingBuffer->Lock != NULL) {
        WdfObjectDelete(RingBuffer->Lock);
        RingBuffer->Lock = NULL;
    }

    if (RingBuffer->Events != NULL) {
        ExFreePool(RingBuffer->Events);
        RingBuffer->Events = NULL;
    }

    RingBuffer->Initialized = FALSE;
}

VOID
RingBufferResetOverflow(
    _Inout_ PRING_BUFFER RingBuffer
    )
{
    if (RingBuffer->Initialized) {
        WdfSpinLockAcquire(RingBuffer->Lock);
        RingBuffer->Overflowed = FALSE;
        WdfSpinLockRelease(RingBuffer->Lock);
    }
}

NTSTATUS
RingBufferPush(
    _Inout_ PRING_BUFFER RingBuffer,
    _In_ PHA_EVENT Event
    )
{
    NTSTATUS status = STATUS_SUCCESS;

    if (!RingBuffer->Initialized) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    WdfSpinLockAcquire(RingBuffer->Lock);

    if (RingBuffer->Count >= RingBuffer->Capacity) {
        //
        // Buffer is full. Handle Overflow Policy.
        //
        if (!RingBuffer->Overflowed) {
            HA_EVENT overflowEvent = {0};
            LARGE_INTEGER tick;

            RingBuffer->Overflowed = TRUE;

            //
            // Construct Overflow Event
            //
            overflowEvent.event_type = HA_EVT_OVERFLOW;
            overflowEvent.pid = 0; // System event
            KeQuerySystemTime(&tick);
            overflowEvent.timestamp_ns = tick.QuadPart;
            
            //
            // Overwrite oldest entry (Tail) 
            //
            RingBuffer->Tail = (RingBuffer->Tail + 1) % RingBuffer->Capacity;
            RingBuffer->Count--;

            //
            // Push Overflow Event
            //
            RtlCopyMemory(&RingBuffer->Events[RingBuffer->Head], &overflowEvent, sizeof(HA_EVENT));
            RingBuffer->Head = (RingBuffer->Head + 1) % RingBuffer->Capacity;
            RingBuffer->Count++;
            
            status = STATUS_BUFFER_OVERFLOW;

        } else {
            status = STATUS_BUFFER_OVERFLOW;
        }
    } else {
        //
        // Normal Push
        //
        RtlCopyMemory(&RingBuffer->Events[RingBuffer->Head], Event, sizeof(HA_EVENT));
        
        // Update Head
        RingBuffer->Head = (RingBuffer->Head + 1) % RingBuffer->Capacity;
        RingBuffer->Count++;
    }

    WdfSpinLockRelease(RingBuffer->Lock);
    return status;
}

ULONG
RingBufferPop(
    _Inout_ PRING_BUFFER RingBuffer,
    _Out_ PHA_EVENT Destination,
    _In_ ULONG MaxCount
    )
{
    ULONG popped = 0;

    if (!RingBuffer->Initialized || MaxCount == 0) {
        return 0;
    }

    WdfSpinLockAcquire(RingBuffer->Lock);

    while (RingBuffer->Count > 0 && popped < MaxCount) {
        // Copy event to destination
        RtlCopyMemory(&Destination[popped], &RingBuffer->Events[RingBuffer->Tail], sizeof(HA_EVENT));

        // Update Tail
        RingBuffer->Tail = (RingBuffer->Tail + 1) % RingBuffer->Capacity;
        RingBuffer->Count--;
        popped++;
    }

    WdfSpinLockRelease(RingBuffer->Lock);

    return popped;
}

BOOLEAN
RingBufferIsEmpty(
    _In_ PRING_BUFFER RingBuffer
    )
{
    BOOLEAN isEmpty;
    WdfSpinLockAcquire(RingBuffer->Lock);
    isEmpty = (RingBuffer->Count == 0);
    WdfSpinLockRelease(RingBuffer->Lock);
    return isEmpty;
}
