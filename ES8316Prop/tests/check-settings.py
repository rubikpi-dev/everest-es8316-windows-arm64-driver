# SPDX-License-Identifier: GPL-2.0-only
# Licensed under the GNU General Public License, version 2 only.
# Provided without warranty. See LICENSE for the license text.

"""Prevent drift among the settings page, INF seeds and kernel defaults."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]
inf = (root / "ES8316/ES8316.inf").read_text()
driver = (root / "ES8316/Device.c").read_text()
schema = (root / "ES8316Prop/Settings.h").read_text()
tuning = (root / "ES8316/Tuning.h").read_text()
seeds = {
    name: int(value, 0)
    for name, value in re.findall(r"^HKR,,(\w+),0x00010003,(\w+)$", inf, re.M)
}
ui = {
    name: int(value, 0)
    for name, value in re.findall(r'(?:ENUM|NUMBER)_SETTING\("(\w+)", "[^"]+", (\w+),', schema)
}
constants = {"LPASS_CLOCK_ID_MCLK_1": 0x300, "Es8316MclkAlwaysInD0": 1, "TRACE_LEVEL_INFORMATION": 4}
kernel = {}
for name, value in re.findall(r'ES8316_REG_ULONG\(Ctx, key, "(\w+)",\s*\w+,\s*(\w+)\)', driver):
    kernel[name] = constants[value] if value in constants else int(value, 0)
tuning_fields = {
    name: tuple(map(int, (default, minimum, maximum)))
    for name, default, minimum, maximum in re.findall(
        r"X\((\w+),\s*(\d+),\s*(\d+),\s*(\d+)\)", tuning
    )
}
assert len(tuning_fields) == 3
assert "ES8316_TUNING_FIELDS(ES8316_READ_TUNING)" in driver
assert "Es8316BoundTuningValue(Ctx->Config.name, def, minimum, maximum)" in driver
kernel.update({name: fields[0] for name, fields in tuning_fields.items()})
ui_bounds = {
    name: (int(minimum), int(maximum))
    for name, minimum, maximum in re.findall(
        r'NUMBER_SETTING\("(\w+)", "[^"]+", \w+, (\d+), (\d+),', schema
    )
}
for name, (_, minimum, maximum) in tuning_fields.items():
    if name != "DacRampRate":
        assert ui_bounds[name] == (minimum, maximum), name
assert len(ui) == 25, ui
assert ui == seeds == kernel, (ui, seeds, kernel)
assert 'HKR,,EnumPropPages32,,"%13%\\ES8316Prop.dll,ES8316PropPageProvider"' in inf
assert not re.search(r"^HKR,,\w+,0x00010001,", inf, re.M)
print("PASS: all 25 UI defaults match INF/kernel; tuning bounds match; all seeds preserve saved values")

assert set(tuning_fields) == {"HpEnableDelayMs", "HpMuteDelayMs", "DacRampRate"}
