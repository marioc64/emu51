# Tiny BASIC V2.3 provenance

The original source (`tb51.asm`) and compiled interpreter (`tb51.hex`) were
retrieved on 2026-11-14 from PJRC's **Tiny Basic For 8051** contribution by
Dave LeBlanc:

- [Announcement and original documentation](https://www.pjrc.com/tech/8051/contrib/tb51/index.html)
- [Source](https://www.pjrc.com/tech/8051/contrib/tb51/tb51.asm)
- [Compiled image](https://www.pjrc.com/tech/8051/contrib/tb51/tb51.hex)

The announcement declares the contribution public domain. The source retains
its original authorship and revision history: created by JHW in 1980, fixes by
lss in 1983, Metalink conversion by Jim Lum/CompTech Systems in 1992, and the
later adaptation contributed by Dave LeBlanc. Neither original file is changed;
original line endings are retained. No proprietary assembler is included.

`image.inc` is a generated representation of the published HEX for emu51's
built-in assembler. It contains the same 3,202 interpreter bytes at
8000h–8C81h and retains their public-domain status. The UART startup code lives
separately in `progs/tinybasic/tinybasic.asm`. The original Metalink source uses
macros and directives outside emu51's assembler dialect, so the demo includes
the published machine code rather than translating the interpreter source.

To regenerate or check the include, run from the source root:

```sh
python3 progs/tinybasic/generate_image.py
python3 progs/tinybasic/generate_image.py --check
```

The application workflow test independently compares loaded interpreter bytes
against the preserved upstream HEX and runs BASIC over the emulated UART.

## Preserved-file SHA-256 hashes

- `tb51.asm`: `6f50979220f56f9ebc9811e244cd5668b227a81e924000e8cc136d6928c7b5e8`
- `tb51.hex`: `bdfbda47969c910ee50c2483f07b16583689f53a7f0282da4f4a696f0e4bc984`
