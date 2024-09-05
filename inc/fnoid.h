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

//
// Method direct no-op.
//
// N.B. This MUST be a pre-defined NDIS direct OID, else it will be treated as a
//      regular OID.
//
#define OID_FNMP_METHOD_DIRECT_NOP      OID_TCP_TASK_IPSEC_OFFLOAD_V2_ADD_SA

EXTERN_C_END
