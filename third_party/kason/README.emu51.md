# KaSON integration in emu51

[KaSON](https://github.com/marioc64/kason) is Mariusz Kasolik's C JSON parser and
schema library. This directory vendors revision
`dc4a9b17d4008b384ad787d0d508a38fd39d899f` under the [MIT license](LICENSE).

`kason.c`, `kason.h`, `kason_internal.h`, `kason_schema.c`, `kason_schema.h`, and
`LICENSE` are unmodified copies from that revision. This integration note belongs
to emu51 rather than upstream KaSON.

CMake builds the parser and schema API as C99 in `emu51_kason`.
`emu51/imgui/emu51_execution_settings.cpp` uses the schema API to read and write
`settings.json`; the GUI and regression tests share `emu51_settings_lib`. No
network access is required to configure or build this dependency.

For emu51 configuration behavior and project licensing, see the repository's
[README](../../README.md#configuration) and
[third-party notices](../../THIRD_PARTY_NOTICES.md#kason). KaSON's MIT license is
independent of the surrounding project's GPL-2.0-only license.
