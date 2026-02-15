/*++

Module Name:

    HATestClient.c

Abstract:

    Test client for HAStorRep driver (Phase 1A).

Environment:

    User mode (Win32)

--*/

#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

//
// Include Driver Shared Header
// Adjust path as needed based on project structure
//
#include "../../driver/HAStorRep/public.h"

//
// Globals
//
HANDLE g_hDevice = INVALID_HANDLE_VALUE;
BOOL g_Running = TRUE;

//
// Ctrl-C Handler
//
BOOL WINAPI ConsoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT) {
        printf("\nStopping client...\n");
        g_Running = FALSE;
        // Depending on blocking IO, we might need to cancel IO or just close handle?
        // Blocking IOCTL will remain blocked until completed or cancelled.
        // We can CancelIoEx or just start shutdown.
        if (g_hDevice != INVALID_HANDLE_VALUE) {
            CancelIoEx(g_hDevice, NULL);
        }
        return TRUE;
    }
    return FALSE;
}

int main(int argc, char* argv[]) {
    DWORD bytesReturned;
    HA_REGISTER_ENGINE_IN regIn = {0};
    HA_REGISTER_ENGINE_OUT regOut = {0};
    HA_GET_EVENTS_IN getIn = {0};
    // Large buffer for output
    BYTE* eventBuffer = NULL;
    DWORD eventBufferSize = sizeof(HA_GET_EVENTS_OUT) + (sizeof(HA_EVENT) * HA_MAX_EVENTS_PER_BATCH);
    HA_GET_EVENTS_OUT* getOut = NULL;
    BOOL result;

    printf("HATestClient - Phase 1A\n");

    //
    // Set Console Handler
    //
    if (!SetConsoleCtrlHandler(ConsoleHandler, TRUE)) {
        printf("Error: Could not set control handler\n");
        return 1;
    }

    //
    // Open Device
    //
    printf("Opening device %s...\n", "\\\\.\\HAStorRepCtl");
    g_hDevice = CreateFile(
        "\\\\.\\HAStorRepCtl",
        GENERIC_READ | GENERIC_WRITE,
        0, // Exclusive access
        NULL,
        OPEN_EXISTING,
        0, // FILE_ATTRIBUTE_NORMAL (Synchronous IO)
        NULL
        );

    if (g_hDevice == INVALID_HANDLE_VALUE) {
        printf("Error: Failed to open device. Error %d\n", GetLastError());
        return 1;
    }

    //
    // Register Engine
    //
    regIn.magic = HA_MAGIC;
    regIn.pid = GetCurrentProcessId();

    printf("Registering engine (PID: %d)...\n", regIn.pid);
    
    result = DeviceIoControl(
        g_hDevice,
        IOCTL_HA_REGISTER_ENGINE,
        &regIn, sizeof(regIn),
        &regOut, sizeof(regOut),
        &bytesReturned,
        NULL
        );

    if (!result) {
        printf("Error: Register failed. Error %d\n", GetLastError());
        CloseHandle(g_hDevice);
        return 1;
    }

    printf("Registered! SessionID: %llu, Limits: %u\n", regOut.session_id, regOut.limits);

    //
    // Allocate Event Buffer
    //
    eventBuffer = (BYTE*)malloc(eventBufferSize);
    if (!eventBuffer) {
        printf("Error: OOM\n");
        CloseHandle(g_hDevice);
        return 1;
    }

    getOut = (HA_GET_EVENTS_OUT*)eventBuffer;

    //
    // Event Loop
    //
    printf("Starting Event Loop (Ctrl+C to stop)...\n");

    while (g_Running) {
        getIn.session_id = regOut.session_id;
        getIn.max_events = HA_MAX_EVENTS_PER_BATCH;
        getIn.timeout_ms = 5000; // 5s timeout block

        // Clear buffer
        ZeroMemory(eventBuffer, eventBufferSize);

        // Blocking Call
        result = DeviceIoControl(
            g_hDevice,
            IOCTL_HA_GET_EVENTS,
            &getIn, sizeof(getIn),
            eventBuffer, eventBufferSize,
            &bytesReturned,
            NULL
            );

        if (!result) {
            DWORD err = GetLastError();
            if (err == ERROR_OPERATION_ABORTED) {
                printf("IO Cancelled.\n");
                break;
            }
            if (g_Running) {
                printf("Error: GetEvents failed. Error %d\n", err);
                Sleep(1000); // Backoff
            }
            continue;
        }

        if (getOut->count > 0) {
            printf("Received %u events:\n", getOut->count);
            HA_EVENT* events = (HA_EVENT*)(getOut + 1);
            for (DWORD i = 0; i < getOut->count; i++) {
                printf("  [%d] Type: %u, Seq: %llu, PID: %u\n", 
                    i, events[i].event_type, events[i].seq, events[i].pid);
                
                if (events[i].event_type == HA_EVT_TEST) {
                    printf("    -> TEST EVENT RECEIVED!\n");
                }
            }

            // Acknowledge (Mock)
            HA_ACK_EVENTS_IN ackIn;
            ackIn.session_id = regOut.session_id;
            ackIn.ack_seq = 0; 
            ackIn.ack_stage = 0;
            
            DeviceIoControl(
                g_hDevice,
                IOCTL_HA_ACK_EVENTS,
                &ackIn, sizeof(ackIn),
                NULL, 0,
                &bytesReturned,
                NULL
                );
        }
    }

    printf("Exiting cleanup...\n");
    free(eventBuffer);
    CloseHandle(g_hDevice);
    return 0;
}
