# Preparing an emu51 release

The release version is set by `project("emu51" VERSION ...)` in the top-level
CMake file. It is currently **1.0.0**. Keep the README heading, history, and release
examples consistent when changing that version. Project code uses GPL-2.0-only,
except the independent peripheral-sdk/ subtree, which uses LGPL-2.1-only;
retain [LICENSE](LICENSE), [COPYRIGHT](COPYRIGHT), and all
[third-party notices](THIRD_PARTY_NOTICES.md).

This repository contains generic 8051 sources and examples. Board ROMs,
proprietary includes, private history, and local settings must remain excluded.

## Build and test

Run the [pre-push checks](CONTRIBUTING.md#before-pushing-to-github) on the exact
release commit before tagging. Check the four strict compiler jobs, the separate
Linux sanitizer job, the repeated macOS tests, and the source and history audit.
Use the candidate branch's CI run to verify the commit SHA and build results.

From the repository root, use a Release build and compile every default target:

```sh
python3 scripts/check_public_sources.py
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DEMU51_WERROR=ON -DEMU51_CODESIGN_IDENTITY=-
cmake --build build-release --config Release --parallel
ctest --test-dir build-release -C Release --output-on-failure
```

Use your Python 3 command (`python` on many Windows installations). The signing
setting above requests local ad-hoc signing on macOS and has no effect on other
platforms. It needs no Apple account. `CMAKE_BUILD_TYPE` selects Release for
single-configuration generators; `--config Release` selects it for Visual Studio
and other multi-configuration generators.

Build on each supported platform and architecture. A successful macOS run does
not validate Windows or Linux runtime deployment. Automated tests exercise the
core, assembler, application workflows, console tools, resource lookup, and a
real relocated installation; they do not launch the desktop frontend.

## Binary archives

Create and verify an archive:

```sh
python3 scripts/check_release.py --build build-release --config Release --output dist/binary
```

Use an empty output directory, or one containing only the same binary archive.
The script expects exactly one matching `.tar.gz` or `.zip`; keep source archives
in a different directory. It runs CPack, extracts the archive to a new path,
checks manifests/notices and all three console tools, then checks Linux runtime
dependencies or macOS signatures. On macOS, use the ad-hoc or Developer ID signing
configuration described here so its signature checks can succeed.

CPack defaults to TGZ on macOS/Linux and ZIP on Windows. The name is
`emu51-<version>-<system>-<architecture>` with an adjacent `.sha256` file. The
architecture comes from the configured build, including the selected macOS
architectures or Visual Studio platform when provided.

The default install/archive tree contains:

| Location | Content |
| --- | --- |
| `bin/` | Assembler, console runner, benchmark; desktop executable on Linux/Windows |
| `emu51.app/` | Desktop app on macOS |
| `share/emu51/asm/mod*` | Packaged 8051-family assembler modules |
| `share/applications/emu51.desktop` | Linux desktop launcher |
| `share/icons/hicolor/*/apps/emu51.png` | Linux application icons |
| `share/doc/emu51/` | README, contributor/timing/release/history guides, LICENSE, COPYRIGHT, notices, and vendor license texts |
| `include/emu51/`, `lib/` | Peripheral SDK C header, static library and relocatable CMake package (library directory follows GNUInstallDirs) |
| `share/doc/emu51-peripheral-sdk/` | SDK guide, LGPL 2.1 and referenced GPL 2 license texts |
| `share/doc/emu51-peripheral-sdk/AUDIO.md` | Audio bridge/native host guide; optional host installs the miniaudio MIT-0 license as well |
| `SHA256SUMS` | Checksums for installed files |

Windows executables have an `.exe` suffix. GNUInstallDirs overrides can change
these paths; the release-check wrapper assumes the defaults. The macOS app
contains its own device modules, LICENSE, COPYRIGHT, and notices/licenses and can be
moved independently. The `progs/` examples and test executables are not installed
in binary packages.

The macOS bundle and Windows executable contain the same application icon.
Linux installs the launcher and icons under the selected data directory. The
launcher expects `emu51` on `PATH`; for an unpacked archive, add its `bin/`
directory to `PATH` and copy the launcher and icons into the corresponding
`~/.local/share/` directories for desktop-menu integration.

For unsigned archives or a direct CPack invocation:

```sh
cpack --config build-release/CPackConfig.cmake -C Release -B dist/binary
cmake -DARCHIVE=/absolute/path/to/dist/binary/emu51-1.0.0-Darwin-arm64.tar.gz -DWORK_DIR=/tmp/emu51-package-check -P cmake/VerifyPackage.cmake
```

Replace the archive path with the actual filename. `VerifyPackage.cmake` checks
files, checksums, notices, relocation, and console behavior; the Python wrapper
adds platform dependency/signature checks. For custom install paths, supply
`BINDIR` and `DOCDIR` to the CMake verifier as well.

## Platform runtime requirements

- **macOS:** the native frontend links system Cocoa/Metal frameworks, not GLFW.
  Build each supported architecture separately, or configure
  `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` and test both slices. The semicolon
  must be quoted in a shell command.
- **Windows:** install rules include the MSVC redistributable runtime when using
  MSVC and resolve non-system DLL dependencies, including shared GLFW if used.
  Unresolved DLLs abort installation. System DLLs remain supplied by Windows.
  Preserve the external GLFW runtime's own notice when distributing one.
- **Linux:** the host normally supplies GLFW, compatible libc/libstdc++, OpenGL
  drivers, and window-system libraries. Build on the oldest distribution you
  intend to support; the TGZ is not a universal Linux binary. Check dependencies
  with `ldd bin/emu51` after extraction.

Linux can bundle GLFW itself with `-DEMU51_BUNDLE_GLFW=ON` and
`-DEMU51_GLFW_LICENSE=/path/to/that/runtime/copyright`. This requires a CMake
`SHARED_LIBRARY` GLFW target rather than a pkg-config interface or static target.
The install step copies the library/SONAME symlink chains and its license, and
sets a relative runtime search path. Graphics drivers and other platform
libraries remain host dependencies.

## macOS signing and notarization

Signing runs on staged installed files, after resources are copied and before
`SHA256SUMS` is written. It does not modify the build app in place.

| `EMU51_CODESIGN_IDENTITY` | Result |
| --- | --- |
| Empty (default) | No explicit install-time signing |
| `-` | Free local ad-hoc signing |
| Developer ID Application identity | Signing with hardened runtime and a secure timestamp |

The script signs the app and all three console tools and verifies the app's
signature. Signing failure aborts installation/packaging. An ad-hoc signature
does not establish an Apple-verified developer identity or notarization. An empty
identity does not promise that compiler-produced executables lack an ad-hoc
signature; it simply skips the install-time signing step.

To use your distribution identity, reconfigure before packaging:

```sh
cmake -S . -B build-release -DEMU51_CODESIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)"
cmake --build build-release --config Release --parallel
```

Notarization is a separate operation requiring your Apple developer credentials.
Follow Apple's [notarization workflow](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution)
and [customization guidance](https://developer.apple.com/documentation/security/customizing-the-notarization-workflow).
Submit all executables intended for distribution, including the console tools,
then staple supported bundles such as the app. Local signing alone does not
perform that submission.

If you staple an already staged app, regenerate the manifest after the final
bundle change:

```sh
cmake -DPACKAGE_ROOT=/absolute/path/to/stage -DWRITE_MANIFEST=ON -P cmake/AuditPackage.cmake
```

Archive that final staged tree and create a new archive checksum. Do not rerun
installation or CPack over it: the build app would replace the stapled copy.
Verify the final app with `codesign --verify --deep --strict` and assess the
intended distribution build with `spctl --assess --type execute`. Validate the
final archive after extraction too.

## Matching source archives

Commit the release sources before making an archive. `git archive` reads the
selected commit, not uncommitted working-tree edits. Use the same revision for
source and binary releases:

```sh
mkdir -p dist/source
git archive --format=tar.gz --prefix=emu51-1.0.0/ -o dist/source/emu51-1.0.0-source.tar.gz HEAD
cmake -E sha256sum dist/source/emu51-1.0.0-source.tar.gz
```

The source archive contains project sources, build scripts, generic examples,
LICENSE, COPYRIGHT, and vendor notices. `.gitattributes` exclusions are honored.
CPack source packaging is disabled to avoid sweeping untracked local files into
source releases. Publish the corresponding source archive alongside each binary
release and retain all notices required by the included licenses.

## Desktop release check

On each release OS/architecture, extract to a new path and verify that the desktop
app starts, opens a sample, assembles/loads editor source, runs and pauses, steps,
resets, and restores saved layout/settings. Check dialogs, shortcuts, scrolling,
and unsaved-buffer/recovery behavior. These checks complement the automated
package tests; record the actual platforms tested with the release.
