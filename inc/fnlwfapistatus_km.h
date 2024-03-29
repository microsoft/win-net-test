//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

#define FNLWFAPI_STATUS NTSTATUS
#define FNLWFAPI_FAILED(X) !NT_SUCCESS(X)
#define FNLWFAPI_SUCCEEDED(X) NT_SUCCESS(X)

#define FNLWFAPI_STATUS_SUCCESS         STATUS_SUCCESS
#define FNLWFAPI_STATUS_NOT_FOUND       STATUS_NOT_FOUND
#define FNLWFAPI_STATUS_MORE_DATA       STATUS_BUFFER_OVERFLOW
#define FNLWFAPI_STATUS_NOT_READY       STATUS_DEVICE_NOT_READY

EXTERN_C_END
