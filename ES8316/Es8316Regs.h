/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Licensed under the GNU General Public License, version 2 only.
 * Provided without warranty. See LICENSE for the license text.
 */

/*++

    ES8316 register map (Everest Semiconductor ES8316 low-power audio codec).

    Register numbers and field definitions follow the Linux ASoC driver
    (sound/soc/codecs/es8316.h, GPL-2.0, Everest Semiconductor / Endless)
    and the ES8316 datasheet. Only values the Rubik Pi 3 port needs are
    defined here; see Codec.c for the programming sequences.

--*/

#pragma once

#define ES8316_RESET                0x00
#define ES8316_RESET_CSM_ON         0x80
#define ES8316_RESET_ALL            0x3F

#define ES8316_CLKMGR_CLKSW         0x01
#define ES8316_CLKMGR_CLKSW_MCLK_DIV    0x80
#define ES8316_CLKMGR_CLKSW_MCLK_ON     0x40
#define ES8316_CLKMGR_CLKSW_BCLK_ON     0x20
#define ES8316_CLKMGR_CLKSW_CP_ON       0x10
#define ES8316_CLKMGR_CLKSW_ADC_ON      0x08
#define ES8316_CLKMGR_CLKSW_DAC_ON      0x04
#define ES8316_CLKMGR_CLKSW_ALL_ON      0x7F
#define ES8316_CLKMGR_CLKSEL        0x02
#define ES8316_CLKMGR_ADCOSR        0x03
#define ES8316_CLKMGR_ADCDIV1       0x04
#define ES8316_CLKMGR_ADCDIV2       0x05
#define ES8316_CLKMGR_DACDIV1       0x06
#define ES8316_CLKMGR_DACDIV2       0x07
#define ES8316_CLKMGR_CPDIV         0x08

#define ES8316_SERDATA1             0x09
#define ES8316_SERDATA1_MASTER          0x80
#define ES8316_SERDATA1_BCLK_INV        0x20
#define ES8316_SERDATA1_BCLKDIV_MASK    0x1F
#define ES8316_SERDATA_ADC          0x0A
#define ES8316_SERDATA_DAC          0x0B
#define ES8316_SERDATA2_FMT_MASK        0x03
#define ES8316_SERDATA2_FMT_I2S         0x00
#define ES8316_SERDATA2_FMT_LEFTJ       0x01
#define ES8316_SERDATA2_FMT_RIGHTJ      0x02
#define ES8316_SERDATA2_FMT_PCM         0x03
#define ES8316_SERDATA2_LEN_MASK        0x1C
#define ES8316_SERDATA2_LEN_24          0x00
#define ES8316_SERDATA2_LEN_20          0x04
#define ES8316_SERDATA2_LEN_18          0x08
#define ES8316_SERDATA2_LEN_16          0x0C
#define ES8316_SERDATA2_LEN_32          0x10
#define ES8316_SERDATA2_ADCLRP          0x20
#define ES8316_SERDATA_ADC_MUTE         0x40

#define ES8316_SYS_VMIDSEL          0x0C
#define ES8316_SYS_PDN              0x0D
#define ES8316_SYS_PDN_DAC_VREF         0x01
#define ES8316_SYS_PDN_ADC_VREF         0x02
#define ES8316_SYS_PDN_ADC_BIAS         0x04
#define ES8316_SYS_PDN_BIAS             0x08
#define ES8316_SYS_PDN_ANALOG           0x10
#define ES8316_SYS_PDN_MICBIAS          0x20
#define ES8316_SYS_PDN_ALL              0x3F
#define ES8316_SYS_LP1              0x0E
#define ES8316_SYS_LP2              0x0F
#define ES8316_SYS_VMIDLOW          0x10
#define ES8316_SYS_VSEL             0x11
#define ES8316_SYS_REF              0x12

#define ES8316_HPMIX_SEL            0x13
#define ES8316_HPMIX_SWITCH         0x14
#define ES8316_HPMIX_SWITCH_LDAC        0x80
#define ES8316_HPMIX_SWITCH_LLIN        0x40
#define ES8316_HPMIX_SWITCH_RDAC        0x08
#define ES8316_HPMIX_SWITCH_RLIN        0x04
#define ES8316_HPMIX_PDN            0x15
#define ES8316_HPMIX_PDN_LMIX           0x20
#define ES8316_HPMIX_PDN_LMIXOUT        0x10
#define ES8316_HPMIX_PDN_RMIX           0x02
#define ES8316_HPMIX_PDN_RMIXOUT        0x01
#define ES8316_HPMIX_PDN_ALL            0x33
#define ES8316_HPMIX_VOL            0x16

#define ES8316_CPHP_OUTEN           0x17
#define ES8316_CPHP_OUTEN_LCP           0x40
#define ES8316_CPHP_OUTEN_LDRV          0x20
#define ES8316_CPHP_OUTEN_RCP           0x04
#define ES8316_CPHP_OUTEN_RDRV          0x02
#define ES8316_CPHP_OUTEN_ALL           0x66
#define ES8316_CPHP_ICAL_VOL        0x18
#define ES8316_CPHP_ICAL_VOL_LICAL_PDN  0x80
#define ES8316_CPHP_ICAL_VOL_RICAL_PDN  0x08
#define ES8316_CPHP_ICAL_VOL_LVOL_MASK  0x30
#define ES8316_CPHP_ICAL_VOL_RVOL_MASK  0x03
#define ES8316_CPHP_PDN1            0x19
#define ES8316_CPHP_PDN1_HPOUT          0x04
#define ES8316_CPHP_PDN2            0x1A
#define ES8316_CPHP_PDN2_CP             0x20
#define ES8316_CPHP_LDOCTL          0x1B

#define ES8316_CAL_TYPE             0x1C
#define ES8316_CAL_SET              0x1D
#define ES8316_CAL_HPLIV            0x1E
#define ES8316_CAL_HPRIV            0x1F
#define ES8316_CAL_HPLMV            0x20
#define ES8316_CAL_HPRMV            0x21

#define ES8316_ADC_PDN_LINSEL       0x22
#define ES8316_ADC_PDN_LINSEL_PGA_PDN   0x80
#define ES8316_ADC_PDN_LINSEL_ADC_PDN   0x40
#define ES8316_ADC_PDN_LINSEL_SEL_MASK  0x30
#define ES8316_ADC_PDN_LINSEL_LIN1      0x00
#define ES8316_ADC_PDN_LINSEL_LIN2      0x10
#define ES8316_ADC_PDN_LINSEL_LIN1_BOOST 0x20
#define ES8316_ADC_PDN_LINSEL_LIN2_BOOST 0x30
#define ES8316_ADC_PGAGAIN          0x23
#define ES8316_ADC_D2SEPGA          0x24
#define ES8316_ADC_D2SEPGA_MIC_BOOST    0x01
#define ES8316_ADC_DMIC             0x25
#define ES8316_ADC_MUTE             0x26
#define ES8316_ADC_MUTE_MUTE            0x20
#define ES8316_ADC_VOLUME           0x27
#define ES8316_ADC_ALC1             0x29
#define ES8316_ADC_ALC2             0x2A
#define ES8316_ADC_ALC3             0x2B
#define ES8316_ADC_ALC4             0x2C
#define ES8316_ADC_ALC5             0x2D
#define ES8316_ADC_ALC_NG           0x2E

#define ES8316_DAC_PDN              0x2F
#define ES8316_DAC_PDN_L                0x10
#define ES8316_DAC_PDN_R                0x01
#define ES8316_DAC_PDN_ALL              0x11
#define ES8316_DAC_SET1             0x30
#define ES8316_DAC_SET1_MUTE            0x20
#define ES8316_DAC_SET1_SRC_MASK        0xC0
#define ES8316_DAC_SET2             0x31
#define ES8316_DAC_SET3             0x32
#define ES8316_DAC_VOLL             0x33
#define ES8316_DAC_VOLR             0x34

#define ES8316_GPIO_SEL             0x4D
#define ES8316_GPIO_DEBOUNCE        0x4E
#define ES8316_GPIO_ENABLE_INTERRUPT    0x02
#define ES8316_GPIO_DEBOUNCE_VENDOR     0xF0
#define ES8316_GPIO_FLAG            0x4F
#define ES8316_GPIO_FLAG_GM_NOT_SHORTED 0x02
#define ES8316_GPIO_FLAG_HP_NOT_INSERTED 0x04

#define ES8316_TESTMODE             0x50
#define ES8316_TEST1                0x51
#define ES8316_TEST2                0x52
#define ES8316_TEST3                0x53

#define ES8316_MAX_REGISTER         0x53
