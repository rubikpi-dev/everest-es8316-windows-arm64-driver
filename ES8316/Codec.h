/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Licensed under the GNU General Public License, version 2 only.
 * Provided without warranty. See LICENSE for the license text.
 */

#pragma once

EXTERN_C_START

NTSTATUS
Es8316RegRead(
    _In_ PDEVICE_CONTEXT Ctx,
    _In_ UCHAR Reg,
    _Out_ PUCHAR Value
    );

NTSTATUS
Es8316RegWrite(
    _In_ PDEVICE_CONTEXT Ctx,
    _In_ UCHAR Reg,
    _In_ UCHAR Value
    );

NTSTATUS
Es8316RegUpdate(
    _In_ PDEVICE_CONTEXT Ctx,
    _In_ UCHAR Reg,
    _In_ UCHAR Mask,
    _In_ UCHAR Value
    );

NTSTATUS
Es8316CodecInitialize(
    _In_ PDEVICE_CONTEXT Ctx
    );

VOID
Es8316CodecShutdown(
    _In_ PDEVICE_CONTEXT Ctx
    );

NTSTATUS
Es8316CodecSetOutputEnabled(
    _In_ PDEVICE_CONTEXT Ctx,
    _In_ BOOLEAN Enable
    );

NTSTATUS
Es8316CodecSetMicEnabled(
    _In_ PDEVICE_CONTEXT Ctx,
    _In_ BOOLEAN Enable
    );

NTSTATUS
Es8316CodecSetVolumes(
    _In_ PDEVICE_CONTEXT Ctx,
    _In_ ULONG HpVolume,
    _In_ ULONG DacVolume
    );

NTSTATUS
Es8316JackEvaluate(
    _In_ PDEVICE_CONTEXT Ctx,
    _In_ BOOLEAN FromInterrupt
    );

NTSTATUS
Es8316JackSetInterruptEnabled(
    _In_ PDEVICE_CONTEXT Ctx,
    _In_ BOOLEAN Enable
    );

EXTERN_C_END
