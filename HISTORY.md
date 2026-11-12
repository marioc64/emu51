# Project history

I'm **Mariusz Kasolik**, the author and maintainer of emu51. Version 1.0.0
continues my original [emu51 project on SourceForge](https://sourceforge.net/projects/emu51/),
whose website was [emu51.sf.net](https://emu51.sourceforge.net/).

## 2001: the beginning

I started working on emu51 in 2001, developing the first versions on Windows
and DOS. I published the project when I began studying information technology.
I used Allegro for the graphical interface of the SourceForge-era application.

## Original public versions

My old project pages and the surviving archives record these early versions:

- **v0.01:** my original website shows a screenshot from this version. Its source
  is not listed among the currently available downloads.
- **v0.02 alpha:** the source archive's README dates this version to 2003-10-31.
  The website announcement instead says 2003-09-23; the surviving sources do not
  resolve that discrepancy.
- **v0.03 alpha:** the website and archive listing date it to 2004-05-24.

## Returning to the project

A few years before working on version 1.0.0, I returned to emu51. I separated
the emulation engine from the old application, improved it, and made it more
portable. I then chose Dear ImGui as a fast, straightforward, multiplatform
library for the new interface.

The current application uses Cocoa/Metal on macOS and GLFW/OpenGL on Linux and
Windows. It includes a built-in assembler, assembly editor, debugger controls,
saved preferences, cycle-paced execution, console tools, and automated checks.
The build configuration identifies the current version as 1.0.0.

## License provenance

My SourceForge project lists **GNU General Public License version 2.0 (GPLv2)**.
The available v0.02 and v0.03 source archives were checked on 2026-11-12; neither
contains an explicit license notice. Without the v0.01 source, I cannot confirm
its exact notice or any “or later” option from the surviving archives.

I license this continuation under **GPL version 2 only** (`GPL-2.0-only`); see
[LICENSE](LICENSE) and [COPYRIGHT](COPYRIGHT). Vendored components retain their
separate [notices and licenses](THIRD_PARTY_NOTICES.md).

Inspected archives:

| Version | Source download | SHA-256 |
| --- | --- | --- |
| v0.02 | [emu51_source.zip](https://sourceforge.net/projects/emu51/files/emu51%20v0.02%20%28alpha%29/emu51%20v0.02%20source/emu51_source.zip/download) | `6bc0df6d9b479df1735e43a301419fd1c3fdbf1d2fb1573553d96007f143710d` |
| v0.03 | [emu51.zip](https://sourceforge.net/projects/emu51/files/emu51%20v0.03%20%28alpha%29/emu51%20source%2Bexe%2Bsamples/emu51.zip/download) | `67f4e947d79ac86c1c5fbb4a6568de08dc45c9b08237e88b961b0cd1a450515c` |

I started this repository with a public source snapshot and new Git history.
The old release archives are referenced here for provenance, but I am not
redistributing them. I have kept private history, board ROMs, proprietary
includes, and board-specific examples out of this repository.
