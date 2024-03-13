//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

//
// Test framework abstraction layer. This interface exists so that tests may
// use different test frameworks without changing test logic.
//

#if !defined(KERNEL_MODE)
#include <windows.h>
#endif

EXTERN_C_START

VOID
LogTestFailure(
    _In_z_ PCWSTR File,
    _In_z_ PCWSTR Function,
    INT Line,
    _Printf_format_string_ PCWSTR Format,
    ...
    );

VOID
LogTestWarning(
    _In_z_ PCWSTR File,
    _In_z_ PCWSTR Function,
    INT Line,
    _Printf_format_string_ PCWSTR Format,
    ...
    );

EXTERN_C_END
