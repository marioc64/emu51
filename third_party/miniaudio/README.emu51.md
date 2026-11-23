# miniaudio provenance

The unmodified `miniaudio.h` and `LICENSE` are from David Reid's miniaudio
**0.11.26** release (2026-10-31):

- [Upstream release](https://github.com/mackron/miniaudio/releases/tag/0.11.26)
- [Header](https://raw.githubusercontent.com/mackron/miniaudio/0.11.26/miniaudio.h)
- [License](https://raw.githubusercontent.com/mackron/miniaudio/0.11.26/LICENSE)

SHA-256:

```text
6b376c7fbb1352ba1e0e432ab66069b335ea9405e45cb6c30b97ef0f3c9c3e0e  miniaudio.h
457f1b500e0adf6bc059edddfa78a2f62012e7c3bb43476c20e0bd23b25ba0eb  LICENSE
```

emu51 uses the upstream **MIT No Attribution (MIT-0)** option and retains the
complete upstream license and embedded notices. The optional audio host builds
only the low-level device API; decoding, encoding, resource management, node
graphs and the high-level engine are disabled. Its project-owned implementation
glue and C ABI adapter live in `peripheral-sdk/src/` under LGPL-2.1-only.

The platform-independent SDK/audio bridge has no miniaudio dependency. The native
host is a separate static library, and automated tests select the null device.
