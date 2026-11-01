# ES8316 codec driver (Rubik Pi 3 WoA)

KMDF function driver for the Everest ES8316 audio codec behind the 3.5 mm jack of the
Rubik Pi 3. It binds `ACPI\ESSX8316` (`\_SB.ES16` in the board's DSDT) and owns the codec
only:

| Resource (ACPI `_CRS`) | Use |
| --- | --- |
| `I2CSerialBusV2 0x11` on `\_SB.I2C1` (QUP0 SE0) | register access |
| `GpioIo 117` (output, active high) | codec supply enable (`es8316_enable_vreg`) |
| `GpioInt 63` (edge, both) | jack-detect interrupt (optional; polling fallback) |
| `_DEP \_SB.PEP0` + `es8316_resources.asl` | PEP muxes TLMM 96..100 to MI2S0 while the driver is in D0 |

Audio data does **not** flow through this driver. The PRIMARY MI2S link
(`MI2S-LPAIF-RX-PRIMARY` / `MI2S-LPAIF-TX-PRIMARY`, TLMM 96..100) is connected to
Qualcomm's Q6A AudioReach stack, and the Windows endpoint is Qualcomm's
`qcaudminiport6490` miniport. The working ACDB uses the codec as the I2S BCLK/WS
provider (`I2sMaster=1`), including capture without playback; the ADSP provides MCLK.

```
ES8316/            KMDF kernel driver
es8316ctl/         ARM64 console tool for status/registers/output control
ES8316Prop/        Device Manager "Codec settings" property page (ARM64 DLL, static CRT)
ES8316Prop/tests/  host tests and cross-component consistency checks
```

## MCLK_1 (the part no other Windows component provides)

The ES8316 has no PLL: its MCLK pin (TLMM 96, LPASS external `MCLK_1`) must run, and
keeping it on permanently avoids pops. On this platform the same DSP-side clock
request is reachable from any kernel driver: `qcadcm6490.sys` publishes the
**AudioHwRsc** query-interface `{5BE1E2D0-4323-4D19-81FE-5C4185AE7C05}` whose single
method forwards a `PARAM_ID_RSC_AUDIO_HW_CLK` request (`clock_id 0x300`, frequency,
`attri 1`) as `PRM_CMD_REQUEST_HW_RSC` to the DSP. `Mclk.c` registers for the
interface's arrival, opens it, queries the interface (Size 0x28 / Version 1) and
votes `ENABLE_CLOCK` while the device is in D0, retrying every 2 s by default because
ADCM rejects requests until its ADSP link is up; the vote is released at D0
exit/removal and re-established after an ADSP restart (interface removal/arrival).
Once the vote is granted the codec programming is replayed so the chip's state
machine starts under a live clock. `es8316ctl status` shows `MCLK_1 vote: ... voted 1`
when the DSP accepted it; `MclkPolicy=0` turns the mechanism off if a future ACDB
graph owns the clock.

## What it programs

At D0 entry (`Codec.c`, `Es8316CodecInitialize`):

1. Reset (`0x00 = 0x3F`, 5 ms, `0x00 = 0x80` CSM on, 30 ms), the vendor values
   `SYS_VMIDSEL = 0xFF` and `CLKMGR_ADCOSR = 0x32`.
2. Serial port: standard I2S, normal polarities, clock provider by default,
   word length from the registry, LRCK/BCLK dividers for `MclkHz / SampleRate`
   (12.288 MHz / 48 kHz = 256fs; MCLK halving is used automatically when only
   `MclkHz/2` is a supported ratio), MCLK/BCLK/DAC/ADC/CP clocks on.
3. Analog supplies (bias, analog, DAC Vref), both DACs, DAC -> headphone mixer,
   mixer gain, charge pump and HP output stage (including the reserved
   `pdn_Lical/pdn_Rical` bits that must be cleared), HP volume, then unmute.
4. ADC defaults (PGA gain, digital volume); the mic path (mic bias, PGA, ADC) is
   powered only when a headset is detected unless `MicAlwaysOn=1`.

Jack detection (`Es8316JackEvaluate`): `GPIO_FLAG` bit 2 (`HP_NOT_INSERTED`,
inverted on this board) decides plug presence; on insertion the mic bias is enabled
for 20 ms and bit 1 (`GM_NOT_SHORTED`) separates a 4-pole headset (mic present,
bias kept on, later toggles = button) from 3-pole headphones. Evaluation runs from
the GpioInt work item and from a 1 s poll timer, so a board on which the interrupt
does not connect still tracks the jack. Transitions are printed to the kernel
debugger (`DbgPrintEx`, IHV filter).

Windows cannot be told about the jack by a non-PortCls driver, so the jack state is
used locally (mic bias / optional output muting) and exposed through the diagnostic
interface.

## Device Manager settings

Install the package once, then open **Device Manager > System devices >
Everest ES8316 Audio Codec > Properties > Codec settings**. Select a setting and
change its value. The page exposes 25 configuration values, with descriptions,
named choices, numeric bounds and a **Restore default** button for the selected
setting. Open Device Manager as administrator to edit; without write access the
page is read-only.

Stop playback/recording before clicking **Apply** or **OK**. The page saves only
changed values and sets `DI_FLAGSEX_PROPCHANGE_PENDING` so Windows can restart the
device. Close Properties to finish; if the new settings are not effective or
Windows requests a reboot, restart Windows. These are saved settings, not live
hardware readbacks. `es8316ctl reinit` does **not** reread the registry:
configuration is read in `Es8316CreateDevice`, including interrupt and
MCLK-notification setup.

**Cancel** discards edits made since the last successful Apply. Save failures keep
the page open and attempt to restore the previous values. Concurrent changes
detected before saving require closing and reopening the page. An incomplete
rollback is reported and editing is disabled until the page is reopened.

The settings remain in the device hardware key; no manual registry editing, INF
editing or re-signing is needed for later adjustments. The INF seeds defaults with
`DWORD | NOCLOBBER` (`0x00010003`), preserving existing values on upgrades. A full
device removal, or installing an older INF that overwrites values, can reset them.

Clock settings must agree with the DSP/ACDB. This page does not change the Windows
endpoint format or reconfigure the DSP. Keep the working defaults unless testing a
matching DSP configuration. **UseJackInterrupt** remains off: the current ISR
expects edges, and the historical edge/both-polarities resource caused restart
loops.

For microphone hiss, try **Microphone: automatic level control = Off**
(`MicAlc=0`), then **PGA gain = +7 dB** (`MicPgaGain=4`) if needed. Apply and close
the page between listening tests.

Implementation: `ES8316Prop/` is a native ARM64, static-CRT user-mode DLL, loaded
from the driver store via `EnumPropPages32`. It accesses the selected device
through `SetupDiOpenDevRegKey(DIREG_DEV)`, matching the kernel's
`WdfDeviceOpenRegistryKey(PLUGPLAY_REGKEY_DEVICE)`; it does not open a hard-coded
registry path or select the first matching device.

References:
[property-page provider contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/specific-requirements-for-device-property-page-providers--property-pag),
[Apply/restart handling](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/handling-windows-messages-for-property-pages),
[hardware-key APIs](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/opening-a-device-s-hardware-key)
and [AddReg/NOCLOBBER](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/inf-addreg-directive).

## Configuration values

Seeded by `ES8316.inf` under the device hardware key. The settings page and kernel
use the corresponding SetupAPI/WDF hardware-key APIs. A change takes effect when
the device is removed/restarted (or after reboot).

| Value | Default | Meaning |
| --- | --- | --- |
| `MclkHz` | 12288000 | MCLK frequency requested from the DSP and used for the dividers (256fs; 24576000 also works, the codec halves it) |
| `MclkId` | 0x300 | LPASS clock id (`CLOCK_ID_MCLK_1`) |
| `MclkPolicy` | 1 | 0 never vote, 1 vote while in D0 |
| `I2sMaster` | 1 | codec provides BCLK/WS; requires an ACDB with the LPAIF PRIMARY external WS source |
| `BclkDiv` | 0 | automatic (32 fs at 48 kHz / 16 bits), or explicit divider 1..31 |
| `SampleRate` | 48000 | LRCK rate of the MI2S link |
| `WordLength` | 16 | 16/18/20/24/32-bit slot payload (I2S is tolerant of wider BCLK slots) |
| `MicInput` | 2 | 0 none, 1 MIC1 (`lin1/rin1`), 2 MIC2 (`lin2/rin2`, Rubik headset mic) |
| `MicBoost` | 0 | +15 dB mic boost (`ADC_D2SEPGA` bit 0) |
| `MicAlc` | 1 | apply the electret-headset ALC tuning (0x29..0x2E) |
| `MicPgaGain` | 6 | ADC PGA step 0..10 (6 = +13 dB) |
| `HpVolume` | 3 | headphone attenuation 3 = 0 dB, 2 = -12, 1 = -24, 0 = -48 dB (register field is inverted) |
| `HpMixerGain` | 11 | HP mixer 0..11 (11 = 0 dB) |
| `DacVolume` / `AdcVolume` | 0 | digital volume 0..192 in -0.5 dB steps |
| `JackDetectInverted` | 1 | `everest,jack-detect-inverted` |
| `JackPollIntervalMs` | 1000 | poll period, 0 disables polling |
| `UseJackInterrupt` | 0 | connect the GpioInt when present; keep off unless the interrupt contract is verified |
| `MuteWhenUnplugged` | 0 | power the HP driver down while nothing is plugged |
| `MicAlwaysOn` | 0 | keep mic bias + ADC on regardless of jack |
| `KeepPowerInD3` | 1 | leave GPIO117 asserted when the device leaves D0/is removed |
| `TraceLevel` | 4 | 2 errors, 3 warnings, 4 info, 5 every register write |

### Headphone output-transition controls

The only added timing controls are directly in the headphone output path:

| Value | Default | Accepted values / purpose |
| --- | --- | --- |
| `HpEnableDelayMs` | 10 | 0..1000 ms after headphone-stage enable, before DAC unmute |
| `HpMuteDelayMs` | 10 | 0..1000 ms after DAC mute, before output power-down; also used at shutdown |
| `DacRampRate` | 4 | 4 preserves the chip's reset setting; 0..3 override the soft-ramp rate in register 0x30 bits 3:2 |

The two delays accept 0 to skip the wait. The kernel rejects out-of-range tuning
values by falling back to their defaults. DAC rate overrides preserve other DAC
control bits; the default adds no extra register operation.


## Diagnostic interface / `es8316ctl`

`es8316ctl` (ARM64 console tool, sources in `es8316ctl/`) uses
`GUID_DEVINTERFACE_ES8316` (`ES8316/Es8316Ioctl.h`):

```text
es8316ctl status                  jack/codec state, clocking, MCLK transport/DSP result, counters, last I2C error
es8316ctl dump                    registers 0x00..0x53
es8316ctl read 0x4f               one register
es8316ctl write 0x18 0x33         one register (takes effect immediately)
es8316ctl output 1 1 3 0          HP on, mic on, HP volume 3, DAC volume 0 dB
es8316ctl reinit                  re-run the D0 programming
```

## Build / install

Build all three projects with the EWDK (Enterprise WDK, ARM64):

```text
msbuild ES8316\ES8316.vcxproj      /p:Configuration=Release /p:Platform=ARM64 /p:SignMode=Off
msbuild es8316ctl\es8316ctl.vcxproj /p:Configuration=Release /p:Platform=ARM64
msbuild ES8316Prop\ES8316Prop.vcxproj /p:Configuration=Release /p:Platform=ARM64
```

Outputs: `ES8316\ARM64\Release\ES8316.sys` (and the stamped INF next to it),
`es8316ctl\ARM64\Release\es8316ctl.exe`, `ES8316Prop\ARM64\Release\ES8316Prop.dll`.

Deliver the package as `ES8316.sys`, `ES8316.inf`, `ES8316Prop.dll`, `ES8316.cat`
and `es8316ctl.exe`. Sign both the SYS and the property-page DLL, then generate the
catalog (`Inf2Cat /os:10_X64_ARM64...` after `InfVerif /u` validation); the INF
references `ES8316Prop.dll` in the driver store (`%13%`) and therefore must ship in
the same package.

On the board (test signing on):

```text
pnputil /add-driver ES8316.inf /install
```

The device appears under "System devices" as "Everest ES8316 Audio Codec"; the kd
log shows `ES8316: config: ...`, `codec initialised` and the first
`jack: unknown -> ...` line.

### Settings-page validation

```text
python ES8316Prop\tests\check-settings.py
```

The Python check compares all 25 defaults between the INF, the kernel reader and
the settings page, verifies the tuning bounds match the page bounds, and confirms
NOCLOBBER seeding. The host C tests in the same directory compile the production
page/save code and the production codec programming against in-memory registry,
PnP, register and delay mock backends; they do not edit installed devices. They
cover parsing/overflow, bounds, clock ratios, no-op saves, changed-only writes,
conflict detection, rollback, restart flags, Cancel/Apply, read-only controls,
description text fit and page allocation/release, plus output sequenced-delays,
DAC-ramp bit isolation, fixed reset/ALC/debounce behavior and I/O error
propagation.

## Hardware acceptance gate (codec side)

1. `ACPI\ESSX8316` enumerates with no yellow bang; `es8316ctl status` reports
   `codec init: 1` and `last I2C error: 0x00000000`.
2. `es8316ctl status` reports `MCLK_1 vote: ... ADCM interface 1, voted 1` (needs the
   Q6A ADSP/ADCM stack installed and running); `es8316ctl dump` shows the programmed
   values (`0x01 = 0x7F`, `0x09 = 0x04`, `0x0A/0x0B = 0x0C`, `0x14 = 0x88`,
   `0x17 = 0x66`, `0x18 = 0x00`, `0x2F = 0x00`, `0x4E = 0xF2`).
3. Plugging headphones / a headset produces `jack: removed -> headphone|headset` and
   `es8316ctl status` follows it; a headset button press toggles `button pressed`.
4. With the vote granted, a scope sees 12.288 MHz on TLMM 96 even while idle (the PEP
   has muxed the pin). With the Q6A endpoint streaming, BCLK (1.536 MHz) on 97,
   WS (48 kHz) on 100 and data on 98; audio is audible on the jack.

Item 4 also depends on the Qualcomm miniport/graph and ACDB configuration, not only
on this driver.
