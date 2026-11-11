/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Licensed under the GNU General Public License, version 2 only.
 * Provided without warranty. See LICENSE for the license text.
 */

#include "Driver.h"

static
VOID
Es8316Sleep(
    _In_ ULONG Milliseconds
    )
{
    LARGE_INTEGER interval;

    if (Milliseconds == 0) return;
    interval.QuadPart = -10 * 1000 * (LONGLONG)Milliseconds;
    KeDelayExecutionThread(KernelMode, FALSE, &interval);
}

NTSTATUS
Es8316RegRead(
    _In_ PDEVICE_CONTEXT Ctx,
    _In_ UCHAR Reg,
    _Out_ PUCHAR Value
    )
{
    NTSTATUS status;

    *Value = 0;
    status = SpbReadDataSynchronously(&Ctx->I2c, Reg, Value, 1);
    if (!NT_SUCCESS(status))
    {
        Ctx->LastI2cStatus = status;
    }
    return status;
}

NTSTATUS
Es8316RegWrite(
    _In_ PDEVICE_CONTEXT Ctx,
    _In_ UCHAR Reg,
    _In_ UCHAR Value
    )
{
    NTSTATUS status;
    UCHAR data;

    data = Value;
    status = SpbWriteDataSynchronously(&Ctx->I2c, Reg, &data, 1);
    if (!NT_SUCCESS(status))
    {
        Ctx->LastI2cStatus = status;
    }
    else
    {
        Trace(TRACE_LEVEL_VERBOSE, TRACE_CODEC, "w 0x%02X = 0x%02X", Reg, Value);
    }
    return status;
}

NTSTATUS
Es8316RegUpdate(
    _In_ PDEVICE_CONTEXT Ctx,
    _In_ UCHAR Reg,
    _In_ UCHAR Mask,
    _In_ UCHAR Value
    )
{
    NTSTATUS status;
    UCHAR current;
    UCHAR updated;

    status = Es8316RegRead(Ctx, Reg, &current);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    updated = (UCHAR)((current & ~Mask) | (Value & Mask));
    if (updated == current)
    {
        return STATUS_SUCCESS;
    }

    return Es8316RegWrite(Ctx, Reg, updated);
}

#define CHECK(expr)                         \
    do {                                    \
        status = (expr);                    \
        if (!NT_SUCCESS(status)) {          \
            Trace(TRACE_LEVEL_ERROR, TRACE_CODEC, "%s failed 0x%08lX", #expr, status); \
            return status;                  \
        }                                   \
    } while (0)

static
UCHAR
Es8316WordLengthBits(
    _In_ ULONG WordLength
    )
{
    switch (WordLength)
    {
    case 18: return ES8316_SERDATA2_LEN_18;
    case 20: return ES8316_SERDATA2_LEN_20;
    case 24: return ES8316_SERDATA2_LEN_24;
    case 32: return ES8316_SERDATA2_LEN_32;
    case 16:
    default: return ES8316_SERDATA2_LEN_16;
    }
}

static const ULONG Es8316MclkRatios[] = { 256, 384, 400, 500, 512, 768, 1024 };

static
BOOLEAN
Es8316RatioSupported(
    _In_ ULONG Clk,
    _In_ ULONG Rate
    )
{
    ULONG i;

    for (i = 0; i < ARRAYSIZE(Es8316MclkRatios); i++)
    {
        if ((Clk % Es8316MclkRatios[i]) == 0 && (Clk / Es8316MclkRatios[i]) == Rate)
        {
            return TRUE;
        }
    }
    return FALSE;
}

static
NTSTATUS
Es8316ProgramSerialPort(
    _In_ PDEVICE_CONTEXT Ctx
    )

{
    NTSTATUS status;
    ULONG clk;
    ULONG lrckDivider;
    ULONG bclkDivider;
    UCHAR clksw;
    UCHAR lenBits;

    clk = Ctx->Config.MclkHz;
    clksw = ES8316_CLKMGR_CLKSW_ALL_ON;

    if (!Es8316RatioSupported(clk, Ctx->Config.SampleRate))
    {
        if (Es8316RatioSupported(clk / 2, Ctx->Config.SampleRate))
        {
            clksw |= ES8316_CLKMGR_CLKSW_MCLK_DIV;
            clk /= 2;
        }
        else
        {
            Trace(TRACE_LEVEL_WARNING, TRACE_CODEC,
                  "MCLK %lu Hz is not a supported multiple of %lu Hz - relying on auto-detect",
                  clk, Ctx->Config.SampleRate);
        }
    }

    lrckDivider = (Ctx->Config.SampleRate != 0) ? (clk / Ctx->Config.SampleRate) : 256;

    {
        ULONG frameBits = 2 * Ctx->Config.WordLength;
        if (frameBits != 0 && (lrckDivider % frameBits) == 0)
        {
            bclkDivider = lrckDivider / frameBits;
        }
        else
        {
            bclkDivider = lrckDivider / 4;
            switch (Ctx->Config.WordLength)
            {
            case 18: bclkDivider /= 18; break;
            case 20: bclkDivider /= 20; break;
            case 24: bclkDivider /= 24; break;
            case 32: bclkDivider /= 32; break;
            default: bclkDivider /= 16; break;
            }
        }
    }
    if (bclkDivider == 0)
    {
        bclkDivider = 1;
    }
    if (Ctx->Config.BclkDiv != 0)
    {
        bclkDivider = Ctx->Config.BclkDiv;
    }

    lenBits = Es8316WordLengthBits(Ctx->Config.WordLength);

    CHECK(Es8316RegUpdate(Ctx, ES8316_SERDATA1,
                          ES8316_SERDATA1_MASTER | ES8316_SERDATA1_BCLK_INV | ES8316_SERDATA1_BCLKDIV_MASK,
                          (UCHAR)((Ctx->Config.I2sMaster ? ES8316_SERDATA1_MASTER : 0) |
                                  (bclkDivider & ES8316_SERDATA1_BCLKDIV_MASK))));

    CHECK(Es8316RegUpdate(Ctx, ES8316_SERDATA_ADC,
                          ES8316_SERDATA2_FMT_MASK | ES8316_SERDATA2_ADCLRP | ES8316_SERDATA2_LEN_MASK | ES8316_SERDATA_ADC_MUTE,
                          ES8316_SERDATA2_FMT_I2S | lenBits));
    CHECK(Es8316RegUpdate(Ctx, ES8316_SERDATA_DAC,
                          ES8316_SERDATA2_FMT_MASK | ES8316_SERDATA2_ADCLRP | ES8316_SERDATA2_LEN_MASK,
                          ES8316_SERDATA2_FMT_I2S | lenBits));

    CHECK(Es8316RegUpdate(Ctx, ES8316_CLKMGR_ADCDIV1, 0x0F, (UCHAR)(lrckDivider >> 8)));
    CHECK(Es8316RegWrite(Ctx, ES8316_CLKMGR_ADCDIV2, (UCHAR)(lrckDivider & 0xFF)));
    CHECK(Es8316RegUpdate(Ctx, ES8316_CLKMGR_DACDIV1, 0x0F, (UCHAR)(lrckDivider >> 8)));
    CHECK(Es8316RegWrite(Ctx, ES8316_CLKMGR_DACDIV2, (UCHAR)(lrckDivider & 0xFF)));

    CHECK(Es8316RegWrite(Ctx, ES8316_CLKMGR_CLKSW, clksw));

    Trace(TRACE_LEVEL_INFORMATION, TRACE_CODEC,
          "serial port: %s I2S %lu-bit, clk=%lu lrck_div=%lu bclk_div=%lu (%lu fs) clksw=0x%02X",
          Ctx->Config.I2sMaster ? "MASTER" : "slave",
          Ctx->Config.WordLength, clk, lrckDivider, bclkDivider,
          (bclkDivider != 0 && Ctx->Config.SampleRate != 0) ? (clk / bclkDivider) / Ctx->Config.SampleRate : 0,
          clksw);

    return STATUS_SUCCESS;
}

NTSTATUS
Es8316CodecSetVolumes(
    _In_ PDEVICE_CONTEXT Ctx,
    _In_ ULONG HpVolume,
    _In_ ULONG DacVolume
    )
{
    NTSTATUS status;

    if (HpVolume != 0xFFFFFFFF)
    {
        if (HpVolume > 3)
        {
            HpVolume = 3;
        }
        Ctx->Config.HpVolume = HpVolume;

        CHECK(Es8316RegUpdate(Ctx, ES8316_CPHP_ICAL_VOL,
                              ES8316_CPHP_ICAL_VOL_LVOL_MASK | ES8316_CPHP_ICAL_VOL_RVOL_MASK,
                              (UCHAR)(((3 - HpVolume) << 4) | (3 - HpVolume))));
    }

    if (DacVolume != 0xFFFFFFFF)
    {
        if (DacVolume > 192)
        {
            DacVolume = 192;
        }
        Ctx->Config.DacVolume = DacVolume;
        CHECK(Es8316RegWrite(Ctx, ES8316_DAC_VOLL, (UCHAR)DacVolume));
        CHECK(Es8316RegWrite(Ctx, ES8316_DAC_VOLR, (UCHAR)DacVolume));
    }

    return STATUS_SUCCESS;
}

NTSTATUS
Es8316CodecSetOutputEnabled(
    _In_ PDEVICE_CONTEXT Ctx,
    _In_ BOOLEAN Enable
    )

{
    NTSTATUS status;

    if (!Ctx->CodecInitialized)
    {
        return STATUS_DEVICE_NOT_READY;
    }

    if (Enable)
    {
        CHECK(Es8316RegUpdate(Ctx, ES8316_CPHP_OUTEN, ES8316_CPHP_OUTEN_ALL, ES8316_CPHP_OUTEN_ALL));
        Es8316Sleep(Ctx->Config.HpEnableDelayMs);
        CHECK(Es8316RegUpdate(Ctx, ES8316_DAC_SET1, ES8316_DAC_SET1_MUTE, 0));
    }
    else
    {
        CHECK(Es8316RegUpdate(Ctx, ES8316_DAC_SET1, ES8316_DAC_SET1_MUTE, ES8316_DAC_SET1_MUTE));
        Es8316Sleep(Ctx->Config.HpMuteDelayMs);
        CHECK(Es8316RegUpdate(Ctx, ES8316_CPHP_OUTEN, ES8316_CPHP_OUTEN_ALL, 0));
    }

    if (Ctx->OutputEnabled != Enable)
    {
        Trace(TRACE_LEVEL_INFORMATION, TRACE_CODEC, "headphone output %s", Enable ? "enabled" : "disabled");
    }
    Ctx->OutputEnabled = Enable;

    return STATUS_SUCCESS;
}

NTSTATUS
Es8316CodecSetMicEnabled(
    _In_ PDEVICE_CONTEXT Ctx,
    _In_ BOOLEAN Enable
    )

{
    NTSTATUS status;
    UCHAR linsel;

    if (!Ctx->CodecInitialized)
    {
        return STATUS_DEVICE_NOT_READY;
    }

    if (Ctx->Config.MicInput == 0)
    {
        Enable = FALSE;
    }

    if (Enable)
    {

        linsel = (Ctx->Config.MicInput == 2) ? ES8316_ADC_PDN_LINSEL_LIN2 : ES8316_ADC_PDN_LINSEL_LIN1;

        CHECK(Es8316RegUpdate(Ctx, ES8316_SYS_PDN,
                              ES8316_SYS_PDN_MICBIAS | ES8316_SYS_PDN_ADC_VREF | ES8316_SYS_PDN_ADC_BIAS,
                              0));
        CHECK(Es8316RegUpdate(Ctx, ES8316_ADC_PDN_LINSEL,
                              ES8316_ADC_PDN_LINSEL_PGA_PDN | ES8316_ADC_PDN_LINSEL_ADC_PDN | ES8316_ADC_PDN_LINSEL_SEL_MASK,
                              linsel));
        CHECK(Es8316RegUpdate(Ctx, ES8316_ADC_D2SEPGA, ES8316_ADC_D2SEPGA_MIC_BOOST,
                              Ctx->Config.MicBoost ? ES8316_ADC_D2SEPGA_MIC_BOOST : 0));
        CHECK(Es8316RegUpdate(Ctx, ES8316_ADC_MUTE, ES8316_ADC_MUTE_MUTE, 0));
        CHECK(Es8316RegUpdate(Ctx, ES8316_SERDATA_ADC, ES8316_SERDATA_ADC_MUTE, 0));
    }
    else
    {
        CHECK(Es8316RegUpdate(Ctx, ES8316_SERDATA_ADC, ES8316_SERDATA_ADC_MUTE, ES8316_SERDATA_ADC_MUTE));
        CHECK(Es8316RegUpdate(Ctx, ES8316_ADC_MUTE, ES8316_ADC_MUTE_MUTE, ES8316_ADC_MUTE_MUTE));
        CHECK(Es8316RegUpdate(Ctx, ES8316_ADC_PDN_LINSEL,
                              ES8316_ADC_PDN_LINSEL_PGA_PDN | ES8316_ADC_PDN_LINSEL_ADC_PDN,
                              ES8316_ADC_PDN_LINSEL_PGA_PDN | ES8316_ADC_PDN_LINSEL_ADC_PDN));
        CHECK(Es8316RegUpdate(Ctx, ES8316_SYS_PDN,
                              ES8316_SYS_PDN_MICBIAS | ES8316_SYS_PDN_ADC_VREF | ES8316_SYS_PDN_ADC_BIAS,
                              ES8316_SYS_PDN_MICBIAS | ES8316_SYS_PDN_ADC_VREF | ES8316_SYS_PDN_ADC_BIAS));
    }

    if (Ctx->MicEnabled != Enable)
    {
        Trace(TRACE_LEVEL_INFORMATION, TRACE_CODEC, "microphone path %s", Enable ? "enabled" : "disabled");
    }
    Ctx->MicEnabled = Enable;

    return STATUS_SUCCESS;
}

NTSTATUS
Es8316CodecInitialize(
    _In_ PDEVICE_CONTEXT Ctx
    )
{
    NTSTATUS status;
    UCHAR value;

    Ctx->CodecInitialized = FALSE;
    Ctx->OutputEnabled = FALSE;
    Ctx->MicEnabled = FALSE;
    Ctx->LastI2cStatus = STATUS_SUCCESS;

    status = Es8316RegRead(Ctx, ES8316_CLKMGR_CLKSW, &value);
    if (!NT_SUCCESS(status))
    {
        Trace(TRACE_LEVEL_ERROR, TRACE_CODEC, "codec does not answer on I2C (0x%08lX)", status);
        return status;
    }

    CHECK(Es8316RegWrite(Ctx, ES8316_RESET, ES8316_RESET_ALL));
    Es8316Sleep(5);
    CHECK(Es8316RegWrite(Ctx, ES8316_RESET, ES8316_RESET_CSM_ON));
    Es8316Sleep(30);
    CHECK(Es8316RegWrite(Ctx, ES8316_SYS_VMIDSEL, 0xFF));
    CHECK(Es8316RegWrite(Ctx, ES8316_CLKMGR_ADCOSR, 0x32));
    CHECK(Es8316RegWrite(Ctx, ES8316_CLKMGR_CLKSEL, 0x09));

    CHECK(Es8316RegWrite(Ctx, ES8316_GPIO_SEL, 0x00));

    CHECK(Es8316ProgramSerialPort(Ctx));

    CHECK(Es8316RegUpdate(Ctx, ES8316_SYS_PDN,
                          ES8316_SYS_PDN_DAC_VREF | ES8316_SYS_PDN_BIAS | ES8316_SYS_PDN_ANALOG,
                          0));

    CHECK(Es8316RegUpdate(Ctx, ES8316_DAC_SET1, ES8316_DAC_SET1_SRC_MASK | ES8316_DAC_SET1_MUTE, ES8316_DAC_SET1_MUTE));

    if (Ctx->Config.DacRampRate < 4)
    {
        CHECK(Es8316RegUpdate(Ctx, ES8316_DAC_SET1, 0x0C,
                              (UCHAR)(Ctx->Config.DacRampRate << 2)));
    }
    CHECK(Es8316RegWrite(Ctx, ES8316_DAC_VOLL, (UCHAR)Ctx->Config.DacVolume));
    CHECK(Es8316RegWrite(Ctx, ES8316_DAC_VOLR, (UCHAR)Ctx->Config.DacVolume));
    CHECK(Es8316RegUpdate(Ctx, ES8316_DAC_PDN, ES8316_DAC_PDN_ALL, 0));

    CHECK(Es8316RegWrite(Ctx, ES8316_HPMIX_SEL, 0x00));
    CHECK(Es8316RegUpdate(Ctx, ES8316_HPMIX_SWITCH,
                          ES8316_HPMIX_SWITCH_LDAC | ES8316_HPMIX_SWITCH_LLIN | ES8316_HPMIX_SWITCH_RDAC | ES8316_HPMIX_SWITCH_RLIN,
                          ES8316_HPMIX_SWITCH_LDAC | ES8316_HPMIX_SWITCH_RDAC));
    CHECK(Es8316RegWrite(Ctx, ES8316_HPMIX_VOL,
                         (UCHAR)((Ctx->Config.HpMixerGain << 4) | Ctx->Config.HpMixerGain)));
    CHECK(Es8316RegUpdate(Ctx, ES8316_HPMIX_PDN, ES8316_HPMIX_PDN_ALL, 0));

    CHECK(Es8316RegWrite(Ctx, ES8316_CPHP_LDOCTL, 0x30));
    CHECK(Es8316RegWrite(Ctx, ES8316_CPHP_PDN2, 0x10));
    CHECK(Es8316RegWrite(Ctx, ES8316_CPHP_PDN1, 0x02));
    CHECK(Es8316RegUpdate(Ctx, ES8316_CPHP_ICAL_VOL,
                          ES8316_CPHP_ICAL_VOL_LICAL_PDN | ES8316_CPHP_ICAL_VOL_RICAL_PDN |
                          ES8316_CPHP_ICAL_VOL_LVOL_MASK | ES8316_CPHP_ICAL_VOL_RVOL_MASK,
                          (UCHAR)(((3 - Ctx->Config.HpVolume) << 4) | (3 - Ctx->Config.HpVolume))));

    CHECK(Es8316RegWrite(Ctx, ES8316_ADC_DMIC, 0x08));
    CHECK(Es8316RegUpdate(Ctx, ES8316_ADC_PGAGAIN, 0xF0, (UCHAR)(Ctx->Config.MicPgaGain << 4)));
    CHECK(Es8316RegUpdate(Ctx, ES8316_ADC_D2SEPGA, ES8316_ADC_D2SEPGA_MIC_BOOST, 0));
    CHECK(Es8316RegWrite(Ctx, ES8316_ADC_VOLUME, (UCHAR)Ctx->Config.AdcVolume));
    if (Ctx->Config.MicAlc)
    {

        CHECK(Es8316RegWrite(Ctx, ES8316_ADC_ALC1, 0xCD));
        CHECK(Es8316RegWrite(Ctx, ES8316_ADC_ALC2, 0x08));
        CHECK(Es8316RegWrite(Ctx, ES8316_ADC_ALC3, 0xA0));
        CHECK(Es8316RegWrite(Ctx, ES8316_ADC_ALC4, 0x05));
        CHECK(Es8316RegWrite(Ctx, ES8316_ADC_ALC5, 0x06));
        CHECK(Es8316RegWrite(Ctx, ES8316_ADC_ALC_NG, 0x61));
    }
    CHECK(Es8316RegUpdate(Ctx, ES8316_ADC_PDN_LINSEL,
                          ES8316_ADC_PDN_LINSEL_PGA_PDN | ES8316_ADC_PDN_LINSEL_ADC_PDN,
                          ES8316_ADC_PDN_LINSEL_PGA_PDN | ES8316_ADC_PDN_LINSEL_ADC_PDN));

    Ctx->CodecInitialized = TRUE;

    CHECK(Es8316CodecSetOutputEnabled(Ctx, Ctx->Config.MuteWhenUnplugged ? FALSE : TRUE));
    CHECK(Es8316CodecSetMicEnabled(Ctx, Ctx->Config.MicAlwaysOn ? TRUE : FALSE));

    Trace(TRACE_LEVEL_INFORMATION, TRACE_CODEC, "codec initialised");

    return STATUS_SUCCESS;
}

VOID
Es8316CodecShutdown(
    _In_ PDEVICE_CONTEXT Ctx
    )
{
    if (!Ctx->CodecInitialized)
    {
        return;
    }

    Es8316RegUpdate(Ctx, ES8316_DAC_SET1, ES8316_DAC_SET1_MUTE, ES8316_DAC_SET1_MUTE);
    Es8316Sleep(Ctx->Config.HpMuteDelayMs);
    Es8316RegUpdate(Ctx, ES8316_ADC_MUTE, ES8316_ADC_MUTE_MUTE, ES8316_ADC_MUTE_MUTE);
    Es8316RegWrite(Ctx, ES8316_CPHP_OUTEN, 0x00);
    Es8316RegWrite(Ctx, ES8316_CPHP_ICAL_VOL, 0x88);
    Es8316RegWrite(Ctx, ES8316_CPHP_PDN2, 0x30);
    Es8316RegWrite(Ctx, ES8316_HPMIX_PDN, ES8316_HPMIX_PDN_ALL);
    Es8316RegWrite(Ctx, ES8316_DAC_PDN, ES8316_DAC_PDN_ALL);
    Es8316RegUpdate(Ctx, ES8316_SERDATA_ADC, ES8316_SERDATA_ADC_MUTE, ES8316_SERDATA_ADC_MUTE);
    Es8316RegUpdate(Ctx, ES8316_ADC_PDN_LINSEL,
                    ES8316_ADC_PDN_LINSEL_PGA_PDN | ES8316_ADC_PDN_LINSEL_ADC_PDN,
                    ES8316_ADC_PDN_LINSEL_PGA_PDN | ES8316_ADC_PDN_LINSEL_ADC_PDN);
    Es8316RegWrite(Ctx, ES8316_SYS_PDN, ES8316_SYS_PDN_ALL);
    Es8316RegWrite(Ctx, ES8316_CLKMGR_CLKSW, 0x00);

    Ctx->CodecInitialized = FALSE;
    Ctx->OutputEnabled = FALSE;
    Ctx->MicEnabled = FALSE;

    Trace(TRACE_LEVEL_INFORMATION, TRACE_CODEC, "codec powered down");
}

NTSTATUS
Es8316JackSetInterruptEnabled(
    _In_ PDEVICE_CONTEXT Ctx,
    _In_ BOOLEAN Enable
    )
{
    if (!Ctx->CodecInitialized)
    {
        return STATUS_DEVICE_NOT_READY;
    }

    return Es8316RegWrite(Ctx, ES8316_GPIO_DEBOUNCE,
                          (UCHAR)(ES8316_GPIO_DEBOUNCE_VENDOR |
                                  (Enable ? ES8316_GPIO_ENABLE_INTERRUPT : 0)));
}

static
PCSTR
Es8316JackStateName(
    _In_ ES8316_JACK_STATE State
    )
{
    switch (State)
    {
    case Es8316JackRemoved:   return "removed";
    case Es8316JackHeadphone: return "headphone";
    case Es8316JackHeadset:   return "headset";
    default:                  return "unknown";
    }
}

static
NTSTATUS
Es8316JackReadFlags(
    _In_ PDEVICE_CONTEXT Ctx,
    _Out_ PUCHAR Flags
    )
{
    NTSTATUS status;
    UCHAR flags;

    status = Es8316RegRead(Ctx, ES8316_GPIO_FLAG, &flags);
    if (!NT_SUCCESS(status))
    {
        *Flags = 0;
        return status;
    }

    Ctx->LastGpioFlag = flags;

    if (Ctx->Config.JackDetectInverted)
    {
        flags ^= ES8316_GPIO_FLAG_HP_NOT_INSERTED;
    }

    *Flags = flags;
    return STATUS_SUCCESS;
}

static
VOID
Es8316JackApplyState(
    _In_ PDEVICE_CONTEXT Ctx,
    _In_ ES8316_JACK_STATE NewState
    )
{
    BOOLEAN wantMic;
    BOOLEAN wantOutput;

    if (NewState == Ctx->JackState)
    {
        return;
    }

    Trace(TRACE_LEVEL_INFORMATION, TRACE_JACK, "jack: %s -> %s (flag 0x%02X)",
          Es8316JackStateName(Ctx->JackState), Es8316JackStateName(NewState), Ctx->LastGpioFlag);
    Ctx->JackState = NewState;
    Ctx->JackEvents++;

    if (NewState != Es8316JackHeadset)
    {
        Ctx->ButtonPressed = FALSE;
    }

    wantMic = Ctx->Config.MicAlwaysOn ? TRUE : (NewState == Es8316JackHeadset);
    wantOutput = Ctx->Config.MuteWhenUnplugged ? (NewState != Es8316JackRemoved) : TRUE;

    Es8316CodecSetMicEnabled(Ctx, wantMic);
    Es8316CodecSetOutputEnabled(Ctx, wantOutput);
}

NTSTATUS
Es8316JackEvaluate(
    _In_ PDEVICE_CONTEXT Ctx,
    _In_ BOOLEAN FromInterrupt
    )

{
    NTSTATUS status;
    UCHAR flags;
    BOOLEAN inserted;

    UNREFERENCED_PARAMETER(FromInterrupt);

    if (!Ctx->CodecInitialized)
    {
        return STATUS_DEVICE_NOT_READY;
    }

    status = Es8316JackReadFlags(Ctx, &flags);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    if (Ctx->LastGpioFlag == 0x00)
    {

        Trace(TRACE_LEVEL_WARNING, TRACE_JACK, "GPIO_FLAG reads 0 - codec not detecting");
        return STATUS_SUCCESS;
    }

    inserted = (flags & ES8316_GPIO_FLAG_HP_NOT_INSERTED) ? FALSE : TRUE;

    if (!inserted)
    {
        Es8316JackApplyState(Ctx, Es8316JackRemoved);
        return STATUS_SUCCESS;
    }

    if (Ctx->JackState != Es8316JackHeadphone && Ctx->JackState != Es8316JackHeadset)
    {

        CHECK(Es8316RegUpdate(Ctx, ES8316_SYS_PDN,
                              ES8316_SYS_PDN_MICBIAS | ES8316_SYS_PDN_BIAS | ES8316_SYS_PDN_ANALOG, 0));
        Es8316Sleep(20);

        status = Es8316JackReadFlags(Ctx, &flags);
        if (!NT_SUCCESS(status))
        {
            return status;
        }

        if (flags & ES8316_GPIO_FLAG_HP_NOT_INSERTED)
        {
            Es8316JackApplyState(Ctx, Es8316JackRemoved);
        }
        else if (flags & ES8316_GPIO_FLAG_GM_NOT_SHORTED)
        {
            Es8316JackApplyState(Ctx, Es8316JackHeadset);
        }
        else
        {
            Es8316JackApplyState(Ctx, Es8316JackHeadphone);
        }

        return STATUS_SUCCESS;
    }

    if (Ctx->JackState == Es8316JackHeadset)
    {
        BOOLEAN pressed;

        pressed = (flags & ES8316_GPIO_FLAG_GM_NOT_SHORTED) ? FALSE : TRUE;
        if (pressed != Ctx->ButtonPressed)
        {
            Ctx->ButtonPressed = pressed;
            Trace(TRACE_LEVEL_INFORMATION, TRACE_JACK, "headset button %s", pressed ? "pressed" : "released");
        }
    }

    return STATUS_SUCCESS;
}
