//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include <fnassert.h>

typedef INT64 FN_REFERENCE_COUNT;

inline
VOID
FnInitializeReferenceCount(
    _Out_ FN_REFERENCE_COUNT *RefCount
    )
{
    *RefCount = 1;
}

inline
VOID
FnInitializeReferenceCountEx(
    _Out_ FN_REFERENCE_COUNT *RefCount,
    _In_ SSIZE_T Bias
    )
{
    FRE_ASSERT(Bias > 0);
    *RefCount = Bias;
}

inline
VOID
FnIncrementReferenceCount(
    _Inout_ FN_REFERENCE_COUNT *RefCount
    )
{
    FRE_ASSERT(InterlockedIncrement64(RefCount) > 1);
}

inline
BOOLEAN
FnDecrementReferenceCount(
    _Inout_ FN_REFERENCE_COUNT *RefCount
    )
{
    INT64 NewValue = InterlockedDecrement64(RefCount);
    FRE_ASSERT(NewValue >= 0);

    return NewValue == 0;
}
