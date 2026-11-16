# Structured BASIC on NUCLEO-F412ZG

~~~text
  +-- NUCLEO-F412ZG --------------------------------------+
  |  VT100 > BASIC> RUN HELLO.BAS                          |
  |          Hello from Structured BASIC!                 |
  |          2 + 2 = 4                                    |
  +-- ST-LINK / UART ------------------ SDIO / microSD ---+
~~~

Turn an MB1137 Nucleo-144 and an SD card into a serial BASIC workstation
with a command prompt, full-screen editor and FAT storage. The `basic/`
Git submodule pins the interpreter revision.

| Console | Storage | Firmware |
| --- | --- | --- |
| 115200 baud over the ST-LINK virtual COM port | FAT16/FAT32 on a 3.3 V SD card | Cortex-M4 image for STM32F412ZG |
| VT100 line editing and Ctrl+C break | Load, save and run `.BAS` files | Core compiler and VM linked from `basic/` |

## Console and programs

Open the board's **ST-LINK Virtual COM Port at 115200 baud, 8N1, no flow control**
in a VT100/xterm-compatible terminal with local echo disabled. Press Enter to
display `BASIC>`. Windows Device Manager lists the assigned COM number.

| Command | Action |
| --- | --- |
| `HELP` | Show a one-screen overview. Use `HELP BASIC`, `HELP EDIT` or `HELP SD` for details. |
| `BASIC PRINT 2+2` | Compile and run one BASIC statement. |
| `NEW [file]` | Start an empty buffer, optionally named. |
| `EDIT [file]` | Edit the buffer or load and edit a file. |
| `LIST`, `APPEND text` | Show the buffer or add a source line. |
| `RUN [file]` | Run the buffer or a file. Running a file preserves the buffer. |
| `LOAD file`, `SAVE [file]` | Load or save source to SD. Quote paths containing spaces. |
| `MOUNT`, `SD`, `UMOUNT` | Mount, check or unmount the card. |
| `DIR [path]`, `PWD`, `CD path` | Explore directories. |
| `TYPE file`, `MKDIR path`, `DEL file` | Display a file, create a directory or delete a file. |
| `SDCLK PC12` or `SDCLK PB15` | Select the SDIO clock pin and unmount the active volume. |

The prompt and BASIC `INPUT` support insertion, Backspace, Delete, arrows,
Home/End and Ctrl+U. Up recalls the most recent prompt command. Ctrl+C
cancels input or interrupts BASIC execution.

Use an 80×24 terminal for `EDIT`. Arrows, Home/End and PgUp/PgDn move the
cursor. Backspace/Delete also join lines. **Ctrl+S saves**, and
**Ctrl+Q or Esc returns** to the prompt while retaining unsaved RAM edits.

```text
NEW HELLO.BAS
APPEND PRINT "Hello, Nucleo"
RUN
EDIT
```

Press Ctrl+S and Ctrl+Q, then try `TYPE HELLO.BAS` or `RUN HELLO.BAS`.
The [example programs](examples/) can be copied straight to the SD card.
`NEW file.bas` assigns the editor's filename for new programs.
Changing directories does not change a named buffer's existing destination.
Use `UMOUNT` before removing the card. FatFs R0.12c handles FAT16/FAT32
and long filenames with CP437 encoding. exFAT and formatting are outside
the shell dialect. An interrupted overwrite can leave a `.sbbak` recovery
copy or `.sbtmp` temporary file. Inspect them before retrying. Each source
buffer holds 16 KiB. Compiled BASIC and VM data share a separate 16 KiB arena.

BASIC device calls include `MILLIS()`, `DELAY(ms)`, `PINMODE`,
`DIGITALREAD`, `DIGITALWRITE` and `ANALOGREAD`. GPIO indices 0–2 use onboard
LED pins PB0, PB7 and PB14. ADC indices 0–3 use A0–A3 and yield raw 12-bit values.
The EEPROM built-ins return a platform I/O error on this SD-backed port.

## SD wiring

The active `MIDI2/Core/Src/sdio.c` uses one-bit SDIO and PC12 for clock.
`MIDI2.ioc` lists PB15 for clock. Both support SDIO AF12 on STM32F412ZG.
PC12 is the verified default. For wiring matching the IOC, issue
`SDCLK PB15` and then `MOUNT`. A reset selects PC12.

| microSD | STM32 | Nucleo connector |
| --- | --- | --- |
| CLK | PC12 | CN8 pin 10 / D47 |
| DAT0 | PC8 | CN8 pin 2 / D43 |
| CMD | PD2 | CN8 pin 12 / D48 |
| VDD | 3.3 V | CN8 pin 7 |
| VSS | GND | CN8 pin 11 or 13 |
| Alternate CLK | PB15 | CN7 pin 3 / D17 |

Use a socket or native SDIO adapter for 3.3 V, a common ground, short
wires and suitable pull-ups on the card side, including CMD, DAT0 and DAT3.
USART3 uses PD8/PD9 through ST-LINK VCP.
[ST UM1974](https://www.st.com/resource/en/user_manual/dm00244518-stm32-nucleo144-boards-mb1137-stmicroelectronics.pdf)

## Build and test

Clone with `--recurse-submodules`, or run `git submodule update --init`
from an existing checkout. The relative URL `../structured-basic` resolves
to the sibling repository under the same GitHub owner. For an explicitly
trusted local clone, Git may require
`git -c protocol.file.allow=always submodule update --init`.

When the core repository is private, the build workflow needs the
`BASIC_CORE_DEPLOY_KEY` Actions secret in the port repository. The matching
public deploy key needs read-only access to the core repository. The workflow
uses the key to check out the commit recorded in the `basic/` submodule.

Use CMake, Ninja, Arm GNU Toolchain and the installed STM32CubeF4 v1.27.1
SDK. HAL, CMSIS and FatFs come from that SDK. The MIDI project supplies the
wiring reference.

```powershell
# Put the ARM toolchain bin directory and Ninja on PATH.
cmake -S . -B build/arm -G Ninja "-DCMAKE_TOOLCHAIN_FILE=platform/stm32/arm-gcc.cmake" "-DSTM32CUBE_F4=C:/path/to/STM32Cube_FW_F4_V1.27.1"
cmake --build build/arm

# Native shell tests with Clang on PATH.
cmake -S . -B build/host -G Ninja "-DCMAKE_C_COMPILER=clang"
cmake --build build/host
ctest --test-dir build/host --output-on-failure
./tests/com-smoke.ps1 -Port COM25 -CheckCard
```

The ARM build writes ELF, HEX, BIN and a linker map. The GitHub Actions
workflow runs the native console tests with ASan/UBSan, then builds the
16 KiB firmware with STM32CubeF4 v1.27.1. Its `sbasic-nucleo-f412zg`
artifact contains `sbasic-nucleo.hex`, `sbasic-nucleo.bin` and the
license notices. For a raw BIN, the flash base address is `0x08000000`.

The serial script tests real firmware. `-CheckCardWrite` creates, updates,
reads and removes one unique scratch file. Close other serial terminals
before running it.

To flash with STM32CubeProgrammer, connect the board over ST-LINK/SWD and
write and verify `sbasic-nucleo.hex`. The HEX already contains the target
addresses. Reset the board and use `HELP` to check the serial console.

[Hardware validation](VALIDATION.md) · [Third-party components](THIRD_PARTY.md)
· [Apache License 2.0](LICENSE)
