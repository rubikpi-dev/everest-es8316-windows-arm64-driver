# Acknowledgements and notices

Thanks to [worproject's Rockchip es8323 driver](https://github.com/worproject/Rockchip-Windows-Drivers),
whose ES8323 codec driver provided the general shape for a Windows KMDF SPB
codec driver (SPB codec initialization at D0 entry), and to Microsoft,
Bingxing Wang, LumiaWoA and DuoWoA contributors.

Special thanks to Bingxing Wang and the LumiaWoA / DuoWoA authors (see
[WOA-Project](https://github.com/WOA-Project)) for publishing the SPB
transport support this driver's I2C support is based on.

## Project license

Original project contributions are licensed under `GPL-2.0-only`; see
[LICENSE](LICENSE). Retained third-party material keeps its existing terms.

## Linux hardware references

Thanks to the Linux kernel contributors and Everest Semiconductor for the
following hardware references:

- [ES8316 ALSA SoC audio driver](https://github.com/torvalds/linux/blob/master/sound/soc/codecs/es8316.c)
  and [register header](https://github.com/torvalds/linux/blob/master/sound/soc/codecs/es8316.h):
  `GPL-2.0-only`.
  Copyright Everest Semiconductor Co.,Ltd.
  Authors: David Yang <yangxiaohua@everest-semi.com>,
  Daniel Drake <drake@endlessm.com>.
- [RUBIK Pi Qualcomm Linux kernel (vendor sources)](https://github.com/rubikpi-ai/linux):
  `GPL-2.0-only` (Linux kernel).
  The Qualcomm audio DSP sources in this tree carry the vendor register
  sequences, electret-headset microphone ALC tuning (register 0x29..0x2E
  block) and headset debounce values used on this board, and define the
  ADSP PRM clock request shape (`<&q6prm ...>` tuple: clock id and
  attributes) reused in the Windows driver.

These references apply to ES8316 register programming in `ES8316/Codec.c`,
the register map in `ES8316/Es8316Regs.h`, jack detection in `ES8316/Device.c`
and `ES8316/Gpio.c`, and the ADSP PRM clock request in `ES8316/Mclk.c` and
`ES8316/Mclk.h`.

## ADSP clock-service interface reference

`ES8316/Mclk.c` votes for the LPASS `MCLK_1` clock through the AudioHwRsc
query interface published by `qcadcm6490.sys`. The interface GUID, the
`AudioHwRsc` request shape (`PARAM_ID_RSC_AUDIO_HW_CLK`, resource command,
clock id and attributes) and the PRM command flow were established from
runtime observation of the audio firmware this board ships with. No
Qualcomm source code or proprietary documentation was used or is
redistributed here; the clock request semantics follow the vendor kernel's
Qualcomm audio sources referenced above.

The interface reference applies to `ES8316/Mclk.c` and `ES8316/Mclk.h`.

Product names such as Qualcomm, Everest and Rubik Pi are used for
identification only and are trademarks of their respective owners; this
project is not affiliated with or endorsed by them.

The GPL version 2 text is retained in [LICENSE](LICENSE).
These entries identify the referenced projects' licenses.

## SPB notices

Copyright (c) Microsoft Corporation. All Rights Reserved.
Copyright (c) Bingxing Wang. All Rights Reserved.
Copyright (c) LumiaWoA authors. All Rights Reserved.
Copyright (c) DuoWoA authors. All Rights Reserved.

`ES8316/Spb.c` carries the upstream BSD-3-Clause designation. Its license
text is retained in [LICENSES/BSD-3-Clause.txt](LICENSES/BSD-3-Clause.txt).
The Microsoft copyright notice for `ES8316/Spb.h` is retained above.
