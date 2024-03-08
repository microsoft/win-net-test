//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include <windows.h>

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
    int WsaError;
    CXPLAT_STATUS Status;
    WSADATA WsaData;
    BOOLEAN WsaInitialized = FALSE;

    TraceError("CxPlatInitialize");

    if ((WsaError = WSAStartup(MAKEWORD(2, 2), &WsaData)) != 0) {
        TraceError(
            "[ lib] ERROR, %u, %s.",
            WsaError,
            "WSAStartup");
        Status = HRESULT_FROM_WIN32(WsaError);
        goto Exit;
    }
    WsaInitialized = TRUE;

    Status = CXPLAT_STATUS_SUCCESS;

Exit:

    if (CXPLAT_FAILED(Status)) {
        if (WsaInitialized) {
            (void)WSACleanup();
        }
    }

    return Status;
}

PAGEDX
_IRQL_requires_max_(PASSIVE_LEVEL)
void
CxPlatUninitialize(
    void
    )
{
    WSACleanup();
}
