//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#pragma warning(disable:4201)  // nonstandard extension used: nameless struct/union

#include <ntddk.h>
#include <ntintsafe.h>
#include <ndis.h>
#include <ndis/ndl/nblqueue.h>
#include <ntintsafe.h>

#include <fnassert.h>
#include <fnrtl.h>

#include <bounce.h>
#include <fnio.h>
#include <pooltag.h>

#include "ioctlbounce.h"
