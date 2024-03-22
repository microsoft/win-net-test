//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

//
// Tracing Definitions:
//
// Control GUID:
// {A9838682-ABBC-4CE9-B63E-E4E26C6F9979}
//
#define WPP_CONTROL_GUIDS                           \
    WPP_DEFINE_CONTROL_GUID(                        \
        CxPlatTraceGuid,                            \
        (A9838682,ABBC,4CE9,B63E,E4E26C6F9979),     \
        WPP_DEFINE_BIT(TRACE_CXPLAT)                \
        )

//
// The following system defined definitions may be used:
//
// TRACE_LEVEL_FATAL = 1        // Abnormal exit or termination.
// TRACE_LEVEL_ERROR = 2        // Severe errors that need logging.
// TRACE_LEVEL_WARNING = 3      // Warnings such as allocation failures.
// TRACE_LEVEL_INFORMATION = 4  // Including non-error cases.
// TRACE_LEVEL_VERBOSE = 5      // Detailed traces from intermediate steps.
//
// begin_wpp config
//
// USEPREFIX(TraceFatal,"%!STDPREFIX! %!FUNC!:%!LINE!%!SPACE!");
// FUNC TraceFatal{LEVEL=TRACE_LEVEL_FATAL,FLAGS=TRACE_CXPLAT}(MSG,...);
//
// USEPREFIX(TraceError,"%!STDPREFIX! %!FUNC!:%!LINE!%!SPACE!");
// FUNC TraceError{LEVEL=TRACE_LEVEL_ERROR,FLAGS=TRACE_CXPLAT}(MSG,...);
//
// USEPREFIX(TraceWarn,"%!STDPREFIX! %!FUNC!:%!LINE!%!SPACE!");
// FUNC TraceWarn{LEVEL=TRACE_LEVEL_WARNING,FLAGS=TRACE_CXPLAT}(MSG,...);
//
// USEPREFIX(TraceInfo,"%!STDPREFIX! %!FUNC!:%!LINE!%!SPACE!");
// FUNC TraceInfo{LEVEL=TRACE_LEVEL_INFORMATION,FLAGS=TRACE_CXPLAT}(MSG,...);
//
// USEPREFIX(TraceVerbose,"%!STDPREFIX! %!FUNC!:%!LINE!%!SPACE!");
// FUNC TraceVerbose{LEVEL=TRACE_LEVEL_VERBOSE,FLAGS=TRACE_CXPLAT}(MSG,...);
//
// USEPREFIX(TraceEnter,"%!STDPREFIX! %!FUNC!:%!LINE! --->%!SPACE!");
// FUNC TraceEnter{LEVEL=TRACE_LEVEL_VERBOSE,FLAGS=TRACE_CXPLAT}(MSG,...);
//
// USEPREFIX(TraceExit,"%!STDPREFIX! %!FUNC!:%!LINE! <---%!SPACE! ");
// FUNC TraceExit{LEVEL=TRACE_LEVEL_VERBOSE,FLAGS=TRACE_CXPLAT}(MSG,...);
//
// end_wpp
//

#define WPP_LEVEL_FLAGS_ENABLED(LEVEL, FLAGS) \
    (WPP_LEVEL_ENABLED(FLAGS) && (WPP_CONTROL(WPP_BIT_ ## FLAGS).Level >= LEVEL))
#define WPP_LEVEL_FLAGS_LOGGER(LEVEL, FLAGS) WPP_LEVEL_LOGGER(FLAGS)

//
// Opt-in to a WPP recorder feature that enables independent evaluation of
// conditions to decide if a message needs to be sent to the recorder, an
// enabled session, or both.
//
#define ENABLE_WPP_TRACE_FILTERING_WITH_WPP_RECORDER 1

//
// Logger and Enabled macros that support custom recorders. They simply delegate
// to the default.
//
#define WPP_IFRLOG_LEVEL_FLAGS_ENABLED(IFRLOG, LEVEL, FLAGS) WPP_LEVEL_FLAGS_ENABLED(LEVEL, FLAGS)
#define WPP_IFRLOG_LEVEL_FLAGS_LOGGER(IFRLOG, LEVEL, FLAGS)  WPP_LEVEL_FLAGS_LOGGER(LEVEL, FLAGS)
