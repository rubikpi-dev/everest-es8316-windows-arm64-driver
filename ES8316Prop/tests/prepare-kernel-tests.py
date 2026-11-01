# SPDX-License-Identifier: GPL-2.0-only
# Licensed under the GNU General Public License, version 2 only.
# Provided without warranty. See LICENSE for the license text.

"""Build host fixtures from the current production sources, changing only includes.

The codec runs against a register/delay mock. These tests cannot simulate
hardware timing or the Qualcomm playback-start path.
"""
from pathlib import Path

here = Path(__file__).resolve().parent
driver = here.parents[1] / "ES8316"
out = here / "x64"
out.mkdir(exist_ok=True)
device = (driver / "Device.h").read_text()
config = device[device.index("typedef struct _ES8316_CONFIG"):device.index("typedef struct _DEVICE_CONTEXT")]
(out / "Config.inc").write_text(config)
codec = (driver / "Codec.c").read_text().replace('#include "Driver.h"', '')
(out / "Codec.inc").write_text(codec)
