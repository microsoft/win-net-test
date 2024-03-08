//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include <ntddk.h>

#include "cxplat.h"
#include "trace.h"

#include "cxplat.tmh"

PAGEDX
_IRQL_requires_max_(PASSIVE_LEVEL)
CXPLAT_STATUS
CxPlatInitialize(
    void
    )
{
    PAGED_CODE();
    TraceError("CxPlatInitialize");
    return CXPLAT_STATUS_SUCCESS;
}

PAGEDX
_IRQL_requires_max_(PASSIVE_LEVEL)
void
CxPlatUninitialize(
    void
    )
{
    PAGED_CODE();
    TraceError("CxPlatUninitialize");
}
