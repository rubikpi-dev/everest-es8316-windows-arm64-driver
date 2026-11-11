/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Licensed under the GNU General Public License, version 2 only.
 * Provided without warranty. See LICENSE for the license text.
 */

/* Headphone output-transition timing controls. Bounds are tuning limits, not a claim
 * that every permitted value has been validated on hardware. Out-of-range
 * registry values fall back to the proven default, even without the UI. */
#pragma once

#define ES8316_TUNING_FIELDS(X) \
    X(HpEnableDelayMs,       10,   0,  1000) \
    X(HpMuteDelayMs,         10,   0,  1000) \
    X(DacRampRate,            4,   0,     4)

static __inline ULONG
Es8316BoundTuningValue(ULONG value, ULONG defaultValue, ULONG minimum, ULONG maximum)
{
    return (value < minimum || value > maximum) ? defaultValue : value;
}
