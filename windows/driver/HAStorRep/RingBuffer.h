/*++

Module Name:

    RingBuffer.h

Abstract:

    Header file for the event ring buffer.

Environment:

    Kernel mode

--*/

#pragma once

#include <ntddk.h>
#include <wdf.h>
#include "public.h"

// Max capacity constraints
#define RING_BUFFER_CAPACITY 1024

typedef struct _RING_BUFFER {
    HA_EVENT* Events;
    ULONG Capacity; // Capacity in number of events
    ULONG Head;     // Write index
    ULONG Tail;     // Read index
    ULONG Count;    // Current number of events
    WDFSPINLOCK Lock;
    BOOLEAN Initialized;
    BOOLEAN Overflowed;
} RING_BUFFER, *PRING_BUFFER;

NTSTATUS
RingBufferInitialize(
    _Inout_ PRING_BUFFER RingBuffer,
    _In_ ULONG Capacity
    );

VOID
RingBufferDestroy(
    _Inout_ PRING_BUFFER RingBuffer
    );

VOID
RingBufferResetOverflow(
    _Inout_ PRING_BUFFER RingBuffer
    );

//
// Push an event into the ring.
// Returns STATUS_SUCCESS or STATUS_BUFFER_OVERFLOW.
// If overflow occurs, the event is NOT added, and an overflow event might be recorded instead (if logic permits).
//
NTSTATUS
RingBufferPush(
    _Inout_ PRING_BUFFER RingBuffer,
    _In_ PHA_EVENT Event
    );

//
// Pop events from the ring.
// Pop up to MaxEvents.
// Returns number of events popped.
//
ULONG
RingBufferPop(
    _Inout_ PRING_BUFFER RingBuffer,
    _Out_ PHA_EVENT Destination,
    _In_ ULONG MaxCount
    );

BOOLEAN
RingBufferIsEmpty(
    _In_ PRING_BUFFER RingBuffer
    );
