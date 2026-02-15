/*++

Module Name:

    Driver.h

Abstract:

    This file contains the driver definitions.

Environment:

    Kernel-mode Driver Framework

--*/

#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <initguid.h>

#include "public.h"
#include "Trace.h"
#include "RingBuffer.h"

//
// Device Context
//
typedef struct _DEVICE_CONTEXT {
    //
    // Event Ring Buffer
    //
    RING_BUFFER EventRing;

    //
    // Manual Queue for pending GET_EVENTS requests
    //
    WDFQUEUE PendingNotificationQueue;

    //
    // Registration State
    //
    WDFWAITLOCK EngineLock;
    BOOLEAN EngineRegistered;
    PFILE_OBJECT EngineFileObject;
    UINT64 CurrentSessionId;

} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

//
// This macro will generate an inline function called GetDeviceContext
// which will be used to get the pointer to the device context memory
// in a type safe manner.
//
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext)

//
// Function Prototypes
//

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD HAStorRepEvtDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP HAStorRepEvtDriverContextCleanup;
EVT_WDF_OBJECT_CONTEXT_CLEANUP HAStorRepEvtDeviceContextCleanup;

EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL HAStorRepEvtIoDeviceControl;
EVT_WDF_DEVICE_FILE_CREATE HAStorRepEvtDeviceFileCreate;
EVT_WDF_FILE_CLOSE HAStorRepEvtFileClose;
EVT_WDF_FILE_CLEANUP HAStorRepEvtFileCleanup;

NTSTATUS
HAStorRepCreateDevice(
    _Inout_ PWDFDEVICE_INIT DeviceInit
    );

VOID
ProcessPendingEvents(
    _In_ PDEVICE_CONTEXT DeviceContext
    );
