/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Licensed under the GNU General Public License, version 2 only.
 * Provided without warranty. See LICENSE for the license text.
 */

#pragma once

#define TRACE_DRIVER    0x0001
#define TRACE_PNP       0x0002
#define TRACE_POWER     0x0004
#define TRACE_SPB       0x0008
#define TRACE_CODEC     0x0010
#define TRACE_JACK      0x0020
#define TRACE_REGISTRY  0x0040

#ifndef TRACE_LEVEL_ERROR
#define TRACE_LEVEL_ERROR       2
#define TRACE_LEVEL_WARNING     3
#define TRACE_LEVEL_INFORMATION 4
#define TRACE_LEVEL_VERBOSE     5
#endif

extern ULONG g_Es8316TraceLevel;

#define Trace(LEVEL, FLAGS, MSG, ...)                                       \
    do {                                                                    \
        if ((ULONG)(LEVEL) <= g_Es8316TraceLevel) {                         \
            DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,             \
                       "ES8316: " MSG "\n", __VA_ARGS__);                   \
        }                                                                   \
    } while (0)
