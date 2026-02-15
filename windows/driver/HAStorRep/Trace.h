/*++

Module Name:

    Trace.h

Abstract:

    Header file for debug tracing.

Environment:

    Kernel mode

--*/

#pragma once
#pragma warning(disable:4127)

#define TRACE_LEVEL_ERROR   1
#define TRACE_LEVEL_WARNING 2
#define TRACE_LEVEL_INFO    3
#define TRACE_LEVEL_VERBOSE 4

#ifndef TRACE_LEVEL
#define TRACE_LEVEL TRACE_LEVEL_INFO
#endif

//
// Simple DbgPrint wrapper
//
#define TraceEvents(Level, Format, ...) \
    if (Level <= TRACE_LEVEL) { \
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "HAStorRep: " Format "\n", __VA_ARGS__); \
    }

#define TraceError(Format, ...) \
    TraceEvents(TRACE_LEVEL_ERROR, "ERROR: " Format, __VA_ARGS__)

#define TraceInfo(Format, ...) \
    TraceEvents(TRACE_LEVEL_INFO, Format, __VA_ARGS__)
