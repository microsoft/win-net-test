//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

#define FNMPAPI_STATUS NTSTATUS
#define FNMPAPI_FAILED(X) !NT_SUCCESS(X)
#define FNMPAPI_SUCCEEDED(X) NT_SUCCESS(X)

#define FNMPAPI_STATUS_SUCCESS         STATUS_SUCCESS
#define FNMPAPI_STATUS_NOT_FOUND       STATUS_NOT_FOUND
#define FNMPAPI_STATUS_MORE_DATA       STATUS_BUFFER_OVERFLOW

EXTERN_C_END
