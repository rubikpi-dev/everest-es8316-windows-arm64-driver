/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Licensed under the GNU General Public License, version 2 only.
 * Provided without warranty. See LICENSE for the license text.
 */

/* ES8316 Device Manager settings. Defaults match Device.c and ES8316.inf. */
#pragma once

typedef struct _CHOICE { DWORD Value; PCWSTR Label; } CHOICE;
typedef struct _SETTING {
    PCWSTR Name;
    PCWSTR Label;
    DWORD DefaultValue;
    DWORD Minimum;
    DWORD Maximum;
    const CHOICE *Choices;
    UINT ChoiceCount;
    BOOL Hex;
    PCWSTR Help;
} SETTING;

static const CHOICE OnOff[] = { {0, L"Off"}, {1, L"On"} };
static const CHOICE MicInputs[] = {
    {0, L"None"}, {1, L"MIC1 (lin1/rin1)"}, {2, L"MIC2 (lin2/rin2)"}
};
static const CHOICE WordLengths[] = {
    {16, L"16 bits"}, {18, L"18 bits"}, {20, L"20 bits"}, {24, L"24 bits"}, {32, L"32 bits"}
};
static const CHOICE PgaGains[] = {
    {0, L"-3.5 dB"}, {1, L"0 dB"}, {2, L"+2.5 dB"}, {3, L"+4.5 dB"},
    {4, L"+7 dB"}, {5, L"+10 dB"}, {6, L"+13 dB"}, {7, L"+16 dB"},
    {8, L"+18 dB"}, {9, L"+21 dB"}, {10, L"+24 dB"}
};
static const CHOICE HpVolumes[] = {
    {0, L"-48 dB"}, {1, L"-24 dB"}, {2, L"-12 dB"}, {3, L"0 dB"}
};
static const CHOICE TraceLevels[] = {
    {0, L"Off"}, {1, L"Level 1 (no current messages)"}, {2, L"Errors"},
    {3, L"Warnings"}, {4, L"Information"}, {5, L"Verbose (register writes)"}
};
static const CHOICE DacRampRates[] = {
    {4, L"Keep chip default"}, {0, L"Rate step 0"}, {1, L"Rate step 1"},
    {2, L"Rate step 2"}, {3, L"Rate step 3"}
};

#define ENUM_SETTING(name, label, def, choices, help) \
    {L##name, L##label, def, 0, MAXDWORD, choices, ARRAYSIZE(choices), FALSE, L##help}
#define NUMBER_SETTING(name, label, def, min, max, hex, help) \
    {L##name, L##label, def, min, max, NULL, 0, hex, L##help}

enum { SettingMclkHz, SettingMclkId, SettingMclkPolicy, SettingI2sMaster,
       SettingBclkDiv, SettingSampleRate, SettingWordLength };
static const SETTING Settings[] = {
    NUMBER_SETTING("MclkHz", "Clock: MCLK frequency (Hz)", 12288000, 1, MAXDWORD, FALSE,
        "Clock requested from the DSP and used by the codec. Rubik default: 12288000 Hz. Must match the sample rate and DSP configuration. Changing codec settings does not change the DSP/ACDB."),
    NUMBER_SETTING("MclkId", "Clock: LPASS clock ID", 0x300, 0, MAXDWORD, TRUE,
        "LPASS clock voted through ADCM. Rubik MCLK_1 is 0x300. Accepts decimal or 0x hexadecimal. Changing this can stop audio; use a verified LPASS clock ID."),
    ENUM_SETTING("MclkPolicy", "Clock: vote for MCLK in D0", 1, OnOff,
        "On keeps the codec clock voted while the device is powered. Off requires another component to provide MCLK. The ES8316 has no PLL and needs MCLK."),
    ENUM_SETTING("I2sMaster", "Clock: codec provides BCLK / WS", 1, OnOff,
        "On is required by the working stage F ACDB (external WS source), including recording without playback. This must agree with the ACDB clock direction. Both ends driving the clock is invalid."),
    NUMBER_SETTING("BclkDiv", "Clock: bit-clock divider", 0, 0, 31, FALSE,
        "0 selects the driver's automatic divider (32 fs at 48 kHz / 16 bits). 1-31 overrides it in codec master mode. The working Rubik setting is 0; an incorrect divider can hang capture."),
    NUMBER_SETTING("SampleRate", "Clock: sample rate (Hz)", 48000, 1, MAXDWORD, FALSE,
        "I2S LRCK rate, not the Windows endpoint format. The working DSP link is 48000 Hz. MCLK / rate must be 256, 384, 400, 500, 512, 768 or 1024, optionally after halving MCLK."),
    ENUM_SETTING("WordLength", "Clock: I2S payload width", 16, WordLengths,
        "I2S payload bits per channel. Must match the DSP link. The working Rubik configuration uses 16 bits; this page does not reconfigure the DSP."),
    ENUM_SETTING("MicInput", "Microphone: analog input", 2, MicInputs,
        "Rubik wires the headset microphone to MIC2 (lin2/rin2). None disables the microphone path. MIC1 selects lin1/rin1."),
    ENUM_SETTING("MicBoost", "Microphone: +15 dB boost", 0, OnOff,
        "Adds 15 dB of analog microphone boost. It can also amplify noise and cause clipping."),
    ENUM_SETTING("MicAlc", "Microphone: automatic level control", 1, OnOff,
        "On applies the vendor electret-headset automatic level control tuning. Off uses steady gain. For the reported microphone hiss, try Off, then reduce the PGA gain if needed."),
    ENUM_SETTING("MicPgaGain", "Microphone: PGA gain", 6, PgaGains,
        "Analog microphone gain. Default: +13 dB (step 6). Lower gain may reduce microphone hiss; step 4 is +7 dB."),
    ENUM_SETTING("HpVolume", "Headphones: output attenuation", 3, HpVolumes,
        "Headphone output stage attenuation. 0 dB is the default (step 3). This is independent of the Windows endpoint volume."),
    NUMBER_SETTING("HpMixerGain", "Headphones: mixer gain step", 11, 0, 11, FALSE,
        "Headphone mixer gain, 0-11. Step 11 is 0 dB (default). Lower values reduce mixer gain."),
    NUMBER_SETTING("DacVolume", "Playback: digital attenuation step", 0, 0, 192, FALSE,
        "DAC attenuation in 0.5 dB steps: 0 = 0 dB, 20 = -10 dB, 192 = -96 dB. Applies to both channels."),
    NUMBER_SETTING("AdcVolume", "Capture: digital attenuation step", 0, 0, 192, FALSE,
        "ADC attenuation in 0.5 dB steps: 0 = 0 dB, 20 = -10 dB, 192 = -96 dB."),
    ENUM_SETTING("JackDetectInverted", "Jack: invert detection", 1, OnOff,
        "Inverts plug-presence detection. Rubik requires On (everest,jack-detect-inverted)."),
    NUMBER_SETTING("JackPollIntervalMs", "Jack: polling interval (ms)", 1000, 0, MAXDWORD, FALSE,
        "Polls codec jack status every N milliseconds. Default: 1000. 0 disables polling. Very short intervals increase I2C traffic; with interrupts also off, jack changes will not be tracked."),
    ENUM_SETTING("UseJackInterrupt", "Jack: use GPIO interrupt", 0, OnOff,
        "Default: Off (polling). The original edge/both-polarities ACPI interrupt caused device restart loops, and the current ISR is written for edges. Enable only with a verified compatible ACPI/interrupt implementation."),
    ENUM_SETTING("MuteWhenUnplugged", "Jack: mute when unplugged", 0, OnOff,
        "Powers down the headphone output while no plug is detected. Requires working jack detection."),
    ENUM_SETTING("MicAlwaysOn", "Microphone: keep powered", 0, OnOff,
        "Keeps mic bias and the ADC on regardless of headset detection (line-in style use). Default: Off."),
    ENUM_SETTING("KeepPowerInD3", "Power: keep codec supply enabled", 1, OnOff,
        "Leaves GPIO117 asserted when the device leaves D0 or is removed. Default: On, matching the board's always-on supply."),
    ENUM_SETTING("TraceLevel", "Diagnostics: trace level", 4, TraceLevels,
        "Kernel debugger logging threshold. Default: Information (4). Verbose (5) includes every register write and can generate substantial output."),
    NUMBER_SETTING("HpEnableDelayMs", "Timing: headphone enable (ms)", 10, 0, 1000, FALSE,
        "Delay before DAC unmute when the driver enables headphone output. Default: 10 ms. 0 skips the wait. Runs during codec initialization/output changes, not on each playback start. Shorter delays can cause pops."),
    NUMBER_SETTING("HpMuteDelayMs", "Timing: headphone mute (ms)", 10, 0, 1000, FALSE,
        "Delay between DAC mute and headphone power-down, including codec shutdown. Default: 10 ms. 0 skips the wait. Runs on output/power changes, not on each playback stop. Shorter delays can cause pops."),
    ENUM_SETTING("DacRampRate", "Playback: soft ramp rate", 4, DacRampRates,
        "Overrides the DAC soft-ramp rate field (register 0x30 bits 3:2) when soft ramp is enabled. Keep chip default preserves the reset rate. Steps 0-3 are hardware encodings, not audio-buffer durations.")
};
#undef ENUM_SETTING
#undef NUMBER_SETTING

#define SETTING_COUNT ARRAYSIZE(Settings)
