# MCS-51 assembler modules

Select one device module at the start of a source file. Module directives are
case-insensitive; `$MOD51` and `$mod51` both include the packaged `mod51` file.
`$INCLUDE(mod51)` is equivalent. Derived modules include their base automatically,
so do not also include the base module in the same assembly unit.

| Directive | Device/register map | Additions or differences |
| --- | --- | --- |
| `$MOD51` or `$MOD8051` | Standard 8051/80C51 | Ports 0–3, timers 0/1, serial, interrupt and CPU register symbols |
| `$MOD52` or `$MOD8052` | Standard 8052/80C52 | 8051 symbols plus Timer 2 registers, control bits and pin/interrupt aliases |
| `$MOD851` | Philips 80C851/83C851 | Existing 8051 symbols and EEPROM registers; preserved unchanged |
| `$MOD89S51` | Atmel/Microchip AT89S51 | 8051 symbols, dual data pointers, AUXR/AUXR1, watchdog reset register and masks |
| `$MOD89S52` | Atmel/Microchip AT89S52 | 8052 symbols, dual data pointers, AUXR/AUXR1, watchdog reset and T2MOD |
| `$MOD89C2051` | Atmel/Microchip AT89C2051 | Reduced register map with ports 1/3; no P0, P2, WR, RD or Timer 2 symbols |

These files define assembler symbols only. They do not select an emulated device,
change memory sizes or implement peripherals. Select the 8051 or 8052 runtime
separately in Execution settings (or with `--model` in console tools). The 8052
runtime implements upper RAM and classic Timer 2. Watchdogs, dual-DPTR switching,
EEPROM and analog comparators remain unsupported.
Assembly for an external device can still use its documented register addresses.
The assembler does not enforce a device's flash size or pin restrictions.

For example, this source uses the existing Timer 0 register symbols:

```asm
$MOD51
org 0
mov TMOD,#01h
mov TH0,#0FCh
mov TL0,#018h
setb TR0
loop: sjmp loop
end
```

For AT89S52 source, select `$MOD89S52` instead. `CP_RL2` and `C_T2` use
underscores in place of the datasheet's slashes. `DPS_MASK`, `DISALE_MASK`,
`DISRTO_MASK`, `WDIDLE_MASK`, `POF_MASK`, `DCEN_MASK` and `T2OE_MASK` are byte
masks in the modules where those fields exist. For example,
`ORL AUXR1,#DPS_MASK` selects the second data pointer on the real AT89S51/S52.
Those fields are in registers that are not bit addressable; use `ANL`/`ORL`
instead of `CLR`/`SETB` on the masks.

Custom modules can use `$MOD<suffix>` where the suffix contains letters, digits
or underscores. The assembler looks for the lower-case filename `mod<suffix>`
using the normal include lookup: current source directory, packaged resources,
then explicit `-I` directories. Use `$INCLUDE(...)` for other filenames or paths.
An unavailable module produces an include diagnostic at the directive's location.

All eight module files are copied alongside build executables, installed in
`share/emu51/asm/` by default and bundled in the macOS app's `Contents/Resources/asm/`.
Normal builds do not download device definitions.

The new files are project-authored from register facts in public manufacturer
documentation, rather than copied assembler headers. References:

- [Philips 80C51/87C51/80C52/87C52 datasheet](https://www.nxp.com/docs/en/data-sheet/8XC51_8XC52.pdf),
  standard register subset and Timer 2 register/bit definitions. Vendor extensions
  such as T2MOD are excluded from the generic 8052 module.
- [AT89S51 datasheet 2487D](https://ww1.microchip.com/downloads/en/DeviceDoc/doc2487.pdf),
  section 5, SFR map, AUXR and AUXR1.
- [AT89S52 datasheet 1919D](https://ww1.microchip.com/downloads/en/DeviceDoc/doc1919.pdf),
  sections 5 and 10, SFR map, Timer 2 control and T2MOD.
- [AT89C2051 datasheet 0368H](https://ww1.microchip.com/downloads/en/DeviceDoc/doc0368.pdf),
  sections 4 and 6, port functions and reduced SFR map.

The existing `mod851` records its Intel/Philips references in its file header.
