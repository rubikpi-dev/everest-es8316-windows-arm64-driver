/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Licensed under the GNU General Public License, version 2 only.
 * Provided without warranty. See LICENSE for the license text.
 */

/* Actual Codec.c with mocked register I/O and delays; no hardware access. */
#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
typedef LONG NTSTATUS;
#include "../../ES8316/Tuning.h"
#include "../../ES8316/Es8316Regs.h"
#include "../../ES8316/Es8316Ioctl.h"
#include "x64/Config.inc"

#ifndef NT_SUCCESS
#define NT_SUCCESS(status) (((NTSTATUS)(status)) >= 0)
#endif
#define EXTERN_C_START
#define EXTERN_C_END
#define KernelMode 0
#define TRACE_LEVEL_ERROR 2
#define TRACE_LEVEL_WARNING 3
#define TRACE_LEVEL_INFORMATION 4
#define TRACE_LEVEL_VERBOSE 5
#define TRACE_CODEC 1
#define TRACE_JACK 2
#define ASSERT_TEST(expr) do { if (!(expr)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #expr); exit(1); } } while (0)

typedef struct _DEVICE_CONTEXT {
    ES8316_CONFIG Config;
    int I2c;
    BOOLEAN ButtonPressed, CodecInitialized, MicEnabled, OutputEnabled;
    ULONG JackEvents;
    ES8316_JACK_STATE JackState;
    UCHAR LastGpioFlag;
    NTSTATUS LastI2cStatus;
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;
#include "../../ES8316/Codec.h"

static UCHAR Registers[256];
static ULONG Delays[64], DelayCount;
static NTSTATUS IoStatus;
static void Trace(int level, int flags, const char *format, ...)
{
    UNREFERENCED_PARAMETER(level); UNREFERENCED_PARAMETER(flags); UNREFERENCED_PARAMETER(format);
}
static NTSTATUS KeDelayExecutionThread(int mode, BOOLEAN alertable, LARGE_INTEGER *delay)
{
    UNREFERENCED_PARAMETER(mode); UNREFERENCED_PARAMETER(alertable);
    ASSERT_TEST(delay->QuadPart < 0 && DelayCount < ARRAYSIZE(Delays));
    Delays[DelayCount++] = (ULONG)(-delay->QuadPart / 10000);
    return STATUS_SUCCESS;
}
static NTSTATUS SpbReadDataSynchronously(int *bus, UCHAR reg, PVOID data, ULONG length)
{
    UNREFERENCED_PARAMETER(bus); ASSERT_TEST(length == 1);
    if (!NT_SUCCESS(IoStatus)) return IoStatus;
    *(UCHAR *)data = Registers[reg]; return STATUS_SUCCESS;
}
static NTSTATUS SpbWriteDataSynchronously(int *bus, UCHAR reg, PVOID data, ULONG length)
{
    UNREFERENCED_PARAMETER(bus); ASSERT_TEST(length == 1);
    if (!NT_SUCCESS(IoStatus)) return IoStatus;
    Registers[reg] = *(UCHAR *)data; return STATUS_SUCCESS;
}
#include "x64/Codec.inc"

static void ResetCodec(PDEVICE_CONTEXT ctx)
{
    ZeroMemory(ctx, sizeof(*ctx)); ZeroMemory(Registers, sizeof(Registers));
    DelayCount = 0; IoStatus = STATUS_SUCCESS;
    ctx->Config.MclkHz = 12288000; ctx->Config.SampleRate = 48000;
    ctx->Config.WordLength = 16; ctx->Config.I2sMaster = 1;
    ctx->Config.MicInput = 2; ctx->Config.MicPgaGain = 6; ctx->Config.MicAlc = 1;
    ctx->Config.HpVolume = 3; ctx->Config.HpMixerGain = 11;
#define DEFAULT_TUNING(name, def, minimum, maximum) ctx->Config.name = def;
    ES8316_TUNING_FIELDS(DEFAULT_TUNING)
#undef DEFAULT_TUNING
    Registers[ES8316_DAC_SET1] = 0x0C;
}

int main(void)
{
    DEVICE_CONTEXT ctx;
    ResetCodec(&ctx);
    ASSERT_TEST(NT_SUCCESS(Es8316CodecInitialize(&ctx)));
    ASSERT_TEST(DelayCount == 3 && Delays[0] == 5 && Delays[1] == 30 && Delays[2] == 10);
    ASSERT_TEST(Registers[ES8316_ADC_ALC3] == 0xA0 && Registers[ES8316_ADC_ALC4] == 0x05);
    ASSERT_TEST((Registers[ES8316_DAC_SET1] & 0x0C) == 0x0C);
    ASSERT_TEST(NT_SUCCESS(Es8316JackSetInterruptEnabled(&ctx, TRUE)));
    ASSERT_TEST(Registers[ES8316_GPIO_DEBOUNCE] == 0xF2);
    ASSERT_TEST(NT_SUCCESS(Es8316CodecSetOutputEnabled(&ctx, FALSE)) && Delays[3] == 10);
    Es8316CodecShutdown(&ctx); ASSERT_TEST(Delays[4] == 10);

    ResetCodec(&ctx);
    ctx.Config.HpEnableDelayMs = 17; ctx.Config.HpMuteDelayMs = 19;
    ctx.Config.DacRampRate = 1;
    ASSERT_TEST(NT_SUCCESS(Es8316CodecInitialize(&ctx)));
    ASSERT_TEST(DelayCount == 3 && Delays[0] == 5 && Delays[1] == 30 && Delays[2] == 17);
    ASSERT_TEST(Registers[ES8316_ADC_ALC3] == 0xA0 && Registers[ES8316_ADC_ALC4] == 0x05);
    ASSERT_TEST((Registers[ES8316_DAC_SET1] & 0x0C) == 0x04);
    ASSERT_TEST(NT_SUCCESS(Es8316JackSetInterruptEnabled(&ctx, FALSE)));
    ASSERT_TEST(Registers[ES8316_GPIO_DEBOUNCE] == 0xF0);
    ASSERT_TEST(NT_SUCCESS(Es8316JackSetInterruptEnabled(&ctx, TRUE)));
    ASSERT_TEST(Registers[ES8316_GPIO_DEBOUNCE] == 0xF2);
    Registers[ES8316_GPIO_FLAG] = ES8316_GPIO_FLAG_GM_NOT_SHORTED;
    ASSERT_TEST(NT_SUCCESS(Es8316JackEvaluate(&ctx, FALSE)));
    ASSERT_TEST(Delays[3] == 20 && ctx.JackState == Es8316JackHeadset);
    ASSERT_TEST(DelayCount == 5 && Delays[4] == 17);
    ASSERT_TEST(NT_SUCCESS(Es8316CodecSetOutputEnabled(&ctx, FALSE)) && Delays[5] == 19);
    Es8316CodecShutdown(&ctx); ASSERT_TEST(Delays[6] == 19);

    ResetCodec(&ctx); ctx.Config.MicAlc = 0;
    ctx.Config.HpEnableDelayMs = ctx.Config.HpMuteDelayMs = 0;
    ASSERT_TEST(NT_SUCCESS(Es8316CodecInitialize(&ctx)));
    ASSERT_TEST(DelayCount == 2 && Delays[0] == 5 && Delays[1] == 30);
    ASSERT_TEST(Registers[ES8316_ADC_ALC3] == 0 && Registers[ES8316_ADC_ALC4] == 0);
    ASSERT_TEST(NT_SUCCESS(Es8316CodecSetOutputEnabled(&ctx, FALSE)) && DelayCount == 2);
    IoStatus = STATUS_IO_TIMEOUT;
    ASSERT_TEST(Es8316CodecSetOutputEnabled(&ctx, FALSE) == STATUS_IO_TIMEOUT);
    ASSERT_TEST(ctx.LastI2cStatus == STATUS_IO_TIMEOUT && DelayCount == 2);
    IoStatus = STATUS_SUCCESS;
    Es8316CodecShutdown(&ctx); ASSERT_TEST(DelayCount == 2);
    puts("PASS: codec defaults, output enable/mute delays, zero output waits, ramp field, fixed reset/ALC/debounce and I/O failure propagation");
    return 0;
}
