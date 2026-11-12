# Third-party notices

Project code authored for emu51 uses GPL-2.0-only; see [LICENSE](LICENSE) and
[COPYRIGHT](COPYRIGHT). The components below retain their own licenses. Preserve
their attribution and license notices when redistributing sources or binaries.

## Dear ImGui

Dear ImGui is provided by Omar Cornut and contributors under the MIT license.
emu51 builds the vendored core and the platform/rendering backends needed for
each frontend. Dear ImGui also includes stb-derived components with notices in
their source headers.

- [Vendored license](third_party/imgui/LICENSE.txt)
- [Upstream documentation](third_party/imgui/docs/README.md)

### Embedded default fonts

Dear ImGui embeds MIT-licensed default font data:

- **ProggyClean:** Copyright (c) 2004, 2005 Tristan Grimmer.
- **ProggyForever:** Copyright (c) 2026 Disco Hello; Copyright (c) 2019, 2023
  Tristan Grimmer.

The original notices are retained in `third_party/imgui/imgui_draw.cpp`. The
[font documentation](third_party/imgui/docs/FONTS.md#creditslicenses-for-fonts-included-in-repository)
records these fonts and the separate licenses of additional example font files
under `third_party/imgui/misc/fonts/`. Those extra font files are not loaded or
installed by emu51. MIT permission and warranty terms are included in the
vendored MIT license text above; retain the font copyright notices as well.

## ImGuiColorTextEdit

The vendored editor is provided by Johan A. Goossens under the MIT license. emu51
uses it for the assembly editor and configures its assembly language separately.

- [Vendored license](third_party/ImGuiColorTextEdit/LICENSE)

## KaSON

KaSON (Kasolik's JSON Parser) is provided by Mariusz Kasolik under the MIT license.
The vendored revision is `dc4a9b17d4008b384ad787d0d508a38fd39d899f`. emu51 builds
its C99 core parser and schema API for execution settings.

- [Upstream project](https://github.com/marioc64/kason)
- [Vendored license](third_party/kason/LICENSE)
- [Integration notes](third_party/kason/README.emu51.md)

## portable-file-dialogs

Portable File Dialogs is provided by Sam Hocevar under the Do What the Fuck You
Want To Public License, version 2 (WTFPL). The header retains its author notice.
emu51 uses it for file selection and confirmation dialogs.

- [Vendored license](third_party/portable-file-dialogs.COPYING)

## Codicons

Codicons are provided by Microsoft Corporation and contributors under Creative
Commons Attribution 4.0 International (CC BY 4.0).

- [Upstream project](https://github.com/microsoft/vscode-codicons)
- [License text](https://creativecommons.org/licenses/by/4.0/)
- [Vendored license](third_party/codicons/LICENSE)

The toolbar uses glyphs from Codicons v0.0.39. Its font was converted to compressed
font data using ImGui's `binary_to_compressed_c` tool and embedded in
`emu51/imgui/emu51_imgui_codicons_data.cpp`. The conversion is an adaptation of the
original font; the embedded font data remains CC-BY-4.0. The corresponding
`codicon.csv` is retained under `third_party/codicons/`. This attribution does not
imply endorsement by Microsoft.

## GLFW

Linux and Windows frontends use GLFW. CMake first looks for an installed CMake
package, then pkg-config; Windows also has a fallback static library bundled
with the vendored Dear ImGui examples. The macOS frontend does not use GLFW.

The bundled fallback carries the zlib/libpng license and copyright notices of
Marcus Geelnard and Camilla Berglund:

- [Bundled GLFW notice](third_party/imgui/examples/libs/glfw/COPYING.txt)

An externally supplied GLFW runtime may have different copyright years and
contributors. When bundling such a runtime, include its matching notice/license,
not just the fallback library's older notice. The optional Linux bundling path
requires `EMU51_GLFW_LICENSE` for that purpose.

## Installed notices

Default binary installations include this document and full vendor license
texts under `share/doc/emu51/` and `share/doc/emu51/licenses/`. The macOS app also
contains this notice under `Contents/Resources/THIRD_PARTY_NOTICES.md` and license
texts under `Contents/Resources/licenses/`. Project LICENSE and COPYRIGHT are
installed alongside the documentation and inside the app bundle.

Paths described here refer to the source repository unless explicitly marked as
installed locations. GNUInstallDirs settings can change installation directories.
Upstream example documentation and source notices remain part of the vendored
source tree; they describe those components, not additional emu51 frontends.
