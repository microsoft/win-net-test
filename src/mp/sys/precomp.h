//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

#pragma once

#include <ntddk.h>
#include <ndis.h>
#include <ntintsafe.h>
#include <ndis/ndl/nblqueue.h>
#include <qeo_ndis.h>
#include <fnassert.h>
#include <fnrtl.h>
#include <fnstatusconvert.h>

#include <fnio.h>
#include <fnoid.h>
#include <fnmpioctl.h>
#include <fnmpapiconfig.h>
#include <fntimer.h>

#include "dispatch.h"
#include "miniport.h"

#include "bounce.h"
#include "exclusive.h"
#include "pooltag.h"
#include "oid.h"
#include "port.h"
#include "rss.h"
#include "rx.h"
#include "shared.h"
#include "trace.h"
#include "tx.h"

#define MP_APIVER(_N) (_N)
