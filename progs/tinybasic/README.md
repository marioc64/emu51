# TinyBASIC UART demo

This standalone demo runs PJRC's public-domain Tiny BASIC V2.3 interpreter on
emu51's **8052** profile. It uses upper internal RAM for the stack, external RAM
at 2000h–2FFFh for BASIC variables/program text, and a polled **9600-baud UART**
at **11.0592 MHz**. The supplied project configures these settings, opens the
terminal, and selects CR line endings and a 20 ms file line delay.

## Run it

1. Open **File → Open project…** and choose `tinybasic.emu51` in this directory.
2. Run the emulator. Wait for the Tiny BASIC banner, `OK`, and `>` prompt.
3. In Serial Terminal, send `PRINT (2+3)*4`. The interpreter prints `20`.
4. Choose **Send file… → squares.bas**. Wait until all lines are echoed and the
   final `>` appears. Send `LIST`, then `RUN` as separate commands.

Expected RUN output:

```text
1       1
2       4
3       9
4       16
5       25
OK
>
```

The BASIC file has ordinary LF line endings. **File endings → CR** converts
these on the way into the UART. **File line delay → 20 ms** gives the interpreter
time to insert each numbered line before the next line arrives. Leave Hex input
off. Wait for a prompt before each command; uploading `RUN` followed by more
commands in one file can cause TinyBASIC's control-character poll to consume
those later commands while the program executes.

You can enter numbered lines directly, or send immediate commands such as:

```basic
A=7
PRINT A*6
INPUT A
```

`INPUT A` displays `: ` and waits for a number; send the number as another
terminal message. TinyBASIC also supports IF/GOTO, GOSUB/RETURN, FOR/NEXT, ABS,
RND, and NEW. Send hexadecimal `03` with Ending **None** to abort a running
program with Ctrl-C; restore ordinary text input and CR afterward.

Projects save the machine configuration and firmware load recipe. BASIC program
text and variables are runtime RAM: reset or reopening the project starts a new
interpreter session. Keep programs in `.bas` files and upload them again.

## Rebuild and provenance

The built-in assembler assembles the startup code and includes the published
interpreter bytes, without downloads or another assembler:

```sh
build/emu51/emu51_asm progs/tinybasic/tinybasic.asm -o build/tinybasic.hex
```

When opening this HEX directly, select CPU **8052**, **11.0592 MHz**, split
program/data memory, and the terminal settings above. Opening the supplied
project applies them automatically.

The interpreter bytes and original Metalink-dialect source are preserved under
`third_party/tb51/`. See its [provenance notes](../../third_party/tb51/README.emu51.md)
and [upstream language documentation](https://www.pjrc.com/tech/8051/contrib/tb51/index.html).
The startup routine and demo tools authored for emu51 are GPL-2.0-only; upstream
TinyBASIC remains public domain. Demo files are provided in the source tree and
source archives, and are not installed into binary app packages.
