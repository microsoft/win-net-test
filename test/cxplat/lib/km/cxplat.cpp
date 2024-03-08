//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#include <ntddk.h>
#include <wsk.h>

#include "cxplat.h"
#include "trace.h"

#include "cxplat.tmh"

static WSK_REGISTRATION WskRegistration;
static WSK_PROVIDER_NPI WskProviderNpi;
static WSK_CLIENT_DATAGRAM_DISPATCH WskDispatch;

//
// WSK Client version
//
static const WSK_CLIENT_DISPATCH WskAppDispatch = {
    MAKE_WSK_VERSION(1,0), // Use WSK version 1.0
    0,    // Reserved
    NULL  // WskClientEvent callback not required for WSK version 1.0
};

PAGEDX
_IRQL_requires_max_(PASSIVE_LEVEL)
CXPLAT_STATUS
CxPlatInitialize(
    void
    )
{
    CXPLAT_STATUS Status;
    WSK_CLIENT_NPI WskClientNpi = { NULL, &WskAppDispatch };
    BOOLEAN WskRegistered = FALSE;

    PAGED_CODE();

    TraceError("CxPlatInitialize");

    Status = WskRegister(&WskClientNpi, &WskRegistration);
    if (CXPLAT_FAILED(Status)) {
        TraceError(
            "[ lib] ERROR, %u, %s.",
            Status,
            "WskRegister");
        goto Exit;
    }
    WskRegistered = TRUE;

    //
    // Capture the WSK Provider NPI. If WSK subsystem is not ready yet,
    // wait until it becomes ready.
    //
    Status =
        WskCaptureProviderNPI(
            &WskRegistration,
            WSK_INFINITE_WAIT,
            &WskProviderNpi);
    if (CXPLAT_FAILED(Status)) {
        TraceError(
            "[ lib] ERROR, %u, %s.",
            Status,
            "WskCaptureProviderNPI");
        goto Exit;
    }

Exit:

    if (CXPLAT_FAILED(Status)) {
        if (WskRegistered) {
            WskDeregister(&WskRegistration);
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
    PAGED_CODE();

    WskReleaseProviderNPI(&WskRegistration);
    WskDeregister(&WskRegistration);
}
