//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

//
// Tracing Definitions:
//
// Control GUID:
// {9B4E1D7C-3F2A-4D18-B0C6-2A7E5D9F4A31}
//
#define WPP_CONTROL_GUIDS                           \
    WPP_DEFINE_CONTROL_GUID(                        \
        FnBusTraceGuid,                             \
        (9B4E1D7C,3F2A,4D18,B0C6,2A7E5D9F4A31),     \
        WPP_DEFINE_BIT(TRACE_CONTROL)               \
        WPP_DEFINE_BIT(TRACE_POWER)                 \
        )

#define WPP_LEVEL_FLAGS_LOGGER(level, flags) WPP_LEVEL_LOGGER(flags)
#define WPP_LEVEL_FLAGS_ENABLED(level, flags) \
    (WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level >= level)

#define WPP_RECORDER_LEVEL_FLAGS_ARGS(level, flags) \
    WPP_CONTROL(WPP_BIT_ ## flags).AutoLogContext, level, WPP_BIT_ ## flags
#define WPP_RECORDER_LEVEL_FLAGS_FILTER(level, flags) \
    (level < TRACE_LEVEL_VERBOSE || WPP_CONTROL(WPP_BIT_ ## flags).Level >= level)

// begin_wpp config
//
// FUNC TraceError{LEVEL=TRACE_LEVEL_ERROR}(FLAGS, MSG, ...);
// FUNC TraceWarn{LEVEL=TRACE_LEVEL_WARNING}(FLAGS, MSG, ...);
// FUNC TraceInfo{LEVEL=TRACE_LEVEL_INFORMATION}(FLAGS, MSG, ...);
// FUNC TraceVerbose{LEVEL=TRACE_LEVEL_VERBOSE}(FLAGS, MSG, ...);
//
// FUNC TraceEnter{LEVEL=TRACE_LEVEL_VERBOSE, FLAGS=TRACE_CONTROL}(ENTEREXIT_MSG, ...);
// USESUFFIX(TraceEnter, "%!FUNC! ---> " ENTEREXIT_MSG);
//
// FUNC TraceExitStatus{LEVEL=TRACE_LEVEL_VERBOSE, FLAGS=TRACE_CONTROL}(EXITSTATUS_ARG);
// USESUFFIX(TraceExitStatus, "%!FUNC! <--- Status=%!STATUS!", EXITSTATUS_ARG);
//
// FUNC TraceExitSuccess{LEVEL=TRACE_LEVEL_VERBOSE, FLAGS=TRACE_CONTROL}(...);
// USESUFFIX(TraceExitSuccess, "%!FUNC! <--- Success");
//
// end_wpp
