//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

//
// Tracing Definitions:
//
// Control GUID:
// {9A90334B-68EE-43E5-8FFA-AEF115CE44A1}
//
#define WPP_CONTROL_GUIDS                           \
    WPP_DEFINE_CONTROL_GUID(                        \
        FnSockTraceGuid,                            \
        (9A90334B,68EE,43E5,8FFA,AEF115CE44A1),     \
        WPP_DEFINE_BIT(TRACE_FNSOCK)                \
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
// FUNC TraceFatal{LEVEL=TRACE_LEVEL_FATAL,FLAGS=TRACE_FNSOCK}(MSG,...);
//
// USEPREFIX(TraceError,"%!STDPREFIX! %!FUNC!:%!LINE!%!SPACE!");
// FUNC TraceError{LEVEL=TRACE_LEVEL_ERROR,FLAGS=TRACE_FNSOCK}(MSG,...);
//
// USEPREFIX(TraceWarn,"%!STDPREFIX! %!FUNC!:%!LINE!%!SPACE!");
// FUNC TraceWarn{LEVEL=TRACE_LEVEL_WARNING,FLAGS=TRACE_FNSOCK}(MSG,...);
//
// USEPREFIX(TraceInfo,"%!STDPREFIX! %!FUNC!:%!LINE!%!SPACE!");
// FUNC TraceInfo{LEVEL=TRACE_LEVEL_INFORMATION,FLAGS=TRACE_FNSOCK}(MSG,...);
//
// USEPREFIX(TraceVerbose,"%!STDPREFIX! %!FUNC!:%!LINE!%!SPACE!");
// FUNC TraceVerbose{LEVEL=TRACE_LEVEL_VERBOSE,FLAGS=TRACE_FNSOCK}(MSG,...);
//
// USEPREFIX(TraceEnter,"%!STDPREFIX! %!FUNC!:%!LINE! --->%!SPACE!");
// FUNC TraceEnter{LEVEL=TRACE_LEVEL_VERBOSE,FLAGS=TRACE_FNSOCK}(MSG,...);
//
// USEPREFIX(TraceExit,"%!STDPREFIX! %!FUNC!:%!LINE! <---%!SPACE! ");
// FUNC TraceExit{LEVEL=TRACE_LEVEL_VERBOSE,FLAGS=TRACE_FNSOCK}(MSG,...);
//
// end_wpp
//

#define WPP_LEVEL_FLAGS_ENABLED(LEVEL, FLAGS) \
    (WPP_LEVEL_ENABLED(FLAGS) && (WPP_CONTROL(WPP_BIT_ ## FLAGS).Level >= LEVEL))
#define WPP_LEVEL_FLAGS_LOGGER(LEVEL, FLAGS) WPP_LEVEL_LOGGER(FLAGS)
