# NUCLEO-F412ZG validation

Verified on the connected MB1137 Nucleo on 2026-11-16. STM32CubeProgrammer
identified STM32F412 device ID 0x441 and verified the written HEX before reset.

| Check | Result |
| --- | --- |
| Cortex-M4 link with GNU Arm 13.3.1 and STM32CubeF4 v1.27.1 | Passed |
| Native console test with ASan/UBSan and integer/local-bounds | Passed |
| Prompt, middle-of-line Delete, Backspace and immediate BASIC | Passed on COM25 |
| SINGLE, strings, edited source and INPUT | Passed on COM25 |
| Ctrl+C interrupts a loop and restores the prompt | Passed on COM25 |
| SDIO at PC12 mounts FAT and lists the root | Passed on the wired board |
| Editor save to a new long filename, LOAD, SAVE overwrite, RUN, TYPE and DEL | Passed on the wired board |

The card test removed its own scratch file. PB15 and ADC electrical
performance were outside this hardware check.

The initial 8 KiB hardware baseline is 67,316 bytes. GNU size reports 66,824 bytes of text/RO
data, 480 bytes of initialized RAM and 53,848 bytes of zero-init RAM. Static
RAM includes two 16 KiB source buffers, the 8 KiB BASIC pool, a 2 KiB UART
input ring and an 8 KiB Newlib heap bound for decimal conversion. The linker
reserves at least 8 KiB for the C stack.

The 8 KiB build with updated help was also built and flashed. The serial smoke
test verified the overview, editor and SD help pages and mounted the card
on PC12. Its raw BIN is 69,540 bytes.

## 16 KiB BASIC arena

The native console test passed with ASan, UBSan, integer and local-bounds
checks. It runs a program with a 12,000-byte array twice, rejects an oversized
array and confirms the prompt still runs BASIC after the memory error.

The Cortex-M4 firmware linked with GNU Arm 13.3.1 and STM32CubeF4 v1.27.1.
GNU size reports 69,048 bytes of text/RO data, 480 bytes of initialized RAM
and 62,040 bytes of zero-init RAM. The linker still checks that at least
8 KiB remain for the C stack. The compiler reports a 2,112-byte stack frame
for the bytecode validator, including its bitset.

STM32CubeProgrammer verified the 16 KiB image after writing it to a NUCLEO-F412ZG
with device ID `0x441`. The COM25 smoke test at 115200 8N1 passed:

- A 3,000-element integer array (12,000 bytes of data) returned the expected
  sum of its first and last elements on two consecutive runs. A 4,500-element
  array reported pool exhaustion, and the next command ran successfully.
- The VT100 editor handled Backspace and Ctrl+Q. The edited program then ran
  from the buffer. Ctrl+C stopped a running loop and returned to the prompt.
- The card mounted with SDIO clock on PC12. The test listed the root, saved
  a unique file with Ctrl+S, loaded and ran it, overwrote it with SAVE, checked
  it with RUN and TYPE, and deleted the test file.
