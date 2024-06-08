//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

EXTERN_C_START

bool
TestSetup();

bool
TestCleanup();

VOID
MpBasicRx();

VOID
MpBasicTx();

VOID
MpBasicRxOffload();

VOID
MpBasicTxOffload();

VOID
LwfBasicRx();

VOID
LwfBasicTx();

VOID
LwfBasicOid();

VOID
SockBasicTcp(USHORT AddressFamily);

VOID
SockBasicRaw(USHORT AddressFamily);

EXTERN_C_END
