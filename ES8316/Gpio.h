/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Licensed under the GNU General Public License, version 2 only.
 * Provided without warranty. See LICENSE for the license text.
 */

#pragma once

#include <wdm.h>
#include <wdf.h>

NTSTATUS
GpioOpenPin(
    _In_ WDFDEVICE FxDevice,
    _In_ LARGE_INTEGER ConnectionId,
    _In_ ACCESS_MASK DesiredAccess,
    _Out_ WDFIOTARGET *Target
    );

NTSTATUS
GpioWritePin(
    _In_ WDFIOTARGET Target,
    _In_ BOOLEAN High
    );

NTSTATUS
GpioReadPin(
    _In_ WDFIOTARGET Target,
    _Out_ BOOLEAN *High
    );

VOID
GpioClosePin(
    _Inout_ WDFIOTARGET *Target
    );
