//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include <ntddk.h>
#include <ntintsafe.h>

#include "invokesystemrelayioctl.h"

#include "dispatch.h"
#include "client.h"
#include "service.h"
#include "trace.h"

#define POOLTAG_ISR_CLIENT 'CrsI' // IsrC
#define POOLTAG_ISR_SERVICE 'SrsI' // IsrS
