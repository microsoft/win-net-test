//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#if defined(KERNEL_MODE)
#define CXPLAT_STATUS NTSTATUS
#define CXPLAT_FAILED(X) !NT_SUCCESS(X)
#define CXPLAT_SUCCEEDED(X) NT_SUCCESS(X)
#define CXPLAT_STATUS_SUCCESS STATUS_SUCCESS
#ifndef KRTL_INIT_SEGMENT
#define KRTL_INIT_SEGMENT "INIT"
#endif
#ifndef KRTL_PAGE_SEGMENT
#define KRTL_PAGE_SEGMENT "PAGE"
#endif
#ifndef KRTL_NONPAGED_SEGMENT
#define KRTL_NONPAGED_SEGMENT ".text"
#endif
// Use on code in the INIT segment. (Code is discarded after DriverEntry returns.)
#define INITCODE __declspec(code_seg(KRTL_INIT_SEGMENT))
// Use on pageable functions.
#define PAGEDX __declspec(code_seg(KRTL_PAGE_SEGMENT))
#else
#define CXPLAT_STATUS HRESULT
#define CXPLAT_FAILED(X) FAILED(X)
#define CXPLAT_SUCCEEDED(X) SUCCEEDED(X)
#define CXPLAT_STATUS_SUCCESS S_OK
#define PAGEDX
#endif

EXTERN_C_START

PAGEDX
_IRQL_requires_max_(PASSIVE_LEVEL)
CXPLAT_STATUS
CxPlatInitialize(
    void
    );

PAGEDX
_IRQL_requires_max_(PASSIVE_LEVEL)
void
CxPlatUninitialize(
    void
    );

EXTERN_C_END
