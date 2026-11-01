/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Licensed under the GNU General Public License, version 2 only.
 * Provided without warranty. See LICENSE for the license text.
 */

#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <initguid.h>
#define RESHUB_USE_HELPER_ROUTINES
#include <reshub.h>

#include "Trace.h"
#include "Spb.h"
#include "Gpio.h"
#include "Es8316Regs.h"
#include "Es8316Ioctl.h"
#include "Mclk.h"
#include "Device.h"
#include "Codec.h"

#define ES8316_POOL_TAG '618E'

EXTERN_C_START

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD Es8316EvtDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP Es8316EvtDriverContextCleanup;

EXTERN_C_END
