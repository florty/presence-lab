# Origin and license

This directory is a copy of the `seeed_mr60bha2` component from
[ESPHome](https://github.com/esphome/esphome) 2026.9.1, with local changes marked
"Local addition" / "Lab addition" in `seeed_mr60bha2.h` and `seeed_mr60bha2.cpp`:

- Decodes the radar's point-cloud frame (type `0x0A04`): per target x and y (float, metres),
  Doppler index (int32) and cluster index (int32).
- Tracks movement (any target with a non-zero Doppler index): `get_last_motion_ms()`,
  `take_motion_frames()`.
- Keeps the latest frame for `take_points_json()`, which returns `[seq,[[x,y,doppler,cluster],...]]`.

ESPHome's licensing applies to these files, not this repository's MIT license: the C++ files
(`*.cpp`, `*.h`) are under the GNU General Public License v3.0, and the Python files (`*.py`)
are under the MIT License. See ESPHome's
[LICENSE](https://github.com/esphome/esphome/blob/dev/LICENSE) for the full text.
