//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

//
// This header contains private OIDs shared between tests and FNMP drivers.
// NDIS requires private OIDs set the highest 8 bits.
//

EXTERN_C_START

//
// Set hardware offload capabilities.
//
#define OID_TCP_OFFLOAD_HW_PARAMETERS   0xff000000

//
// Set no-op.
//
#define OID_FNMP_SET_NOP                0xff000001

EXTERN_C_END
