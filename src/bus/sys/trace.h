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

//
// Opt-in to a WPP recorder feature that enables independent evaluation of
// conditions to decide if a message needs to be sent to the recorder, an
// enabled session, or both.
//
#define ENABLE_WPP_TRACE_FILTERING_WITH_WPP_RECORDER 1

#define WPP_IFRLOG_LEVEL_FLAGS_ENABLED(IFRLOG, LEVEL, FLAGS) WPP_LEVEL_FLAGS_ENABLED(LEVEL, FLAGS)
#define WPP_IFRLOG_LEVEL_FLAGS_LOGGER(IFRLOG, LEVEL, FLAGS)  WPP_LEVEL_FLAGS_LOGGER(LEVEL, FLAGS)

// begin_wpp config
//
// USEPREFIX(TraceError,"%!STDPREFIX! %!FUNC!:%!LINE!%!SPACE!");
// FUNC TraceError{LEVEL=TRACE_LEVEL_ERROR}(FLAGS,MSG,...);
//
// USEPREFIX(TraceWarn,"%!STDPREFIX! %!FUNC!:%!LINE!%!SPACE!");
// FUNC TraceWarn{LEVEL=TRACE_LEVEL_WARNING}(FLAGS,MSG,...);
//
// USEPREFIX(TraceInfo,"%!STDPREFIX! %!FUNC!:%!LINE!%!SPACE!");
// FUNC TraceInfo{LEVEL=TRACE_LEVEL_INFORMATION}(FLAGS,MSG,...);
//
// USEPREFIX(TraceVerbose,"%!STDPREFIX! %!FUNC!:%!LINE!%!SPACE!");
// FUNC TraceVerbose{LEVEL=TRACE_LEVEL_VERBOSE}(FLAGS,MSG,...);
//
// USEPREFIX(TraceEnter,"%!STDPREFIX! %!FUNC!:%!LINE! --->%!SPACE!");
// FUNC TraceEnter{LEVEL=TRACE_LEVEL_VERBOSE}(FLAGS,MSG,...);
//
// USEPREFIX(TraceExitSuccess,"%!STDPREFIX! %!FUNC!:%!LINE! <---%!SPACE! ");
// FUNC TraceExitSuccess{LEVEL=TRACE_LEVEL_VERBOSE}(FLAGS,...);
// USESUFFIX (TraceExitSuccess, "STATUS_SUCCESS");
//
// USEPREFIX(TraceExitStatus,"%!STDPREFIX! %!FUNC!:%!LINE! <---%!SPACE!");
// FUNC TraceExitStatus{LEVEL=TRACE_LEVEL_VERBOSE}(FLAGS);
// USESUFFIX (TraceExitStatus, "%!STATUS!", Status);
//
// end_wpp
