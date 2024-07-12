//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

#define FNMP_DEFAULT_RSS_QUEUES 4
#define FNMP_MAX_RSS_INDIR_COUNT 128
#define FNMP_MIN_MTU 1514
#define FNMP_MAX_MTU (16 * 1024 * 1024)
#define FNMP_DEFAULT_MTU FNMP_MAX_MTU
#define FNMP_DEFAULT_MAX_GSO_SIZE 0x20000
#define FNMP_DEFAULT_MIN_GSO_SEG_COUNT 2

EXTERN_C_END
