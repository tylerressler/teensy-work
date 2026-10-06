# teensy-work

Arduino sketches for the Teensy 4.1.

## `sanitize_teensy.ino`

This sketch dumps the board's 8 MiB memory-mapped flash, then overwrites
unused program flash and logical EEPROM with the byte **`0xAA`**. It reads
the selected regions back to verify the pattern. `0xAA` is hexadecimal byte
170, not ASCII `A` (`0x41`).

**Destructive:** this removes data in unused program flash, including any
LittleFS filesystem stored there, and overwrites every logical EEPROM setting.
It does **not** overwrite the entire 8 MiB chip: the current firmware, padding
in its final erase sector, and board-reserved flash are preserved.

### Requirements and use

1. Install Arduino IDE with PJRC's Teensy board support.
2. Copy `sanitize_teensy.ino` into a folder named `sanitize_teensy` and open it
   in Arduino IDE. Keep it separate from the other sketch in this repository.
3. Select **Teensy 4.1**, a **USB Type** that includes Serial, and the board's
   port. Upload the sketch.
4. Open Serial Monitor at **115200**, or use a serial terminal that can capture
   a large amount of output. The sketch waits for the USB serial connection
   before dumping or overwriting memory.
5. Keep power connected and let the dump and overwrite finish. Confirm both
   `PASS` results and `COMPLETE: selected user regions verified as 0xAA.`
   The onboard LED turns on only when both flash and EEPROM checks succeed.
6. Replace this sketch afterward if subsequent applications need to retain
   settings or flash files. The procedure runs again on every boot.

The hexadecimal dump contains 16 bytes per line, with the memory address at
the start of each line. It produces approximately **30 MiB of text**, plus
section headings and status messages. If USB disconnects during the dump,
the sketch pauses until Serial reconnects; an uninterrupted capture is needed
to retain the complete output.

### Dump sections and overwrite scope

The sketch derives the current firmware size from PJRC's `_flashimagelen`
linker symbol. `userFlashStart` is the first 4 KiB sector boundary at or after
the firmware image's end. Its value changes with the compiled sketch.

All ranges below use inclusive end addresses.

| Dump section | Address range | Overwrite behavior |
| --- | --- | --- |
| Preserved firmware, boot headers, and erase-sector padding | `0x60000000` through `userFlashStart - 1` | Excluded from raw writes |
| User-configurable unused program flash, including LittleFS storage | `userFlashStart` through `0x607BFFFF` | Erase complete 4 KiB sectors, program `0xAA`, and verify every byte |
| User-configurable EEPROM backing records, managed through the EEPROM API | `0x607C0000` through `0x607FEFFF` | Set and verify all logical bytes exposed by `EEPROM.length()`; do not fill the raw backing area |
| Preserved board-reserved final 4 KiB | `0x607FF000` through `0x607FFFFF` | Excluded from raw writes |

If no complete unused program-flash sectors remain, that range is empty.
The dump labels describe this sketch's preservation policy, **not hardware
write protection**. The memory map follows PJRC's
[Teensy 4.1 linker script](https://github.com/PaulStoffregen/cores/blob/master/teensy4/imxrt1062_t41.ld)
and [EEPROM implementation](https://github.com/PaulStoffregen/cores/blob/master/teensy4/eeprom.c).

### Operation and limitations

- The complete flash dump runs before either overwrite. Uploading this sketch
  has already replaced the previous firmware, so the dump is not a backup of
  that previous program.
- LittleFS is not mounted or formatted before the dump. Raw overwriting makes
  any filesystem in the overwritten region unusable until it is recreated.
- Raw flash writes use PJRC's internal `eepromemu_flash_erase_sector` and
  `eepromemu_flash_write` helpers, programming 256-byte pages. These internal
  interfaces depend on the installed Teensy core version.
- Bounds checks prevent the raw overwrite from overlapping the current
  firmware or EEPROM backing area. A readback mismatch stops the raw flash
  overwrite and reports the failing address. The EEPROM step still runs, but
  overall success requires both steps to pass.
- Sectors already containing only `0xAA` are checked and skipped to avoid
  unnecessary erases. `EEPROM.update()` similarly avoids unchanged writes.
- Logical EEPROM verification does not prove that older physical flash records
  have been erased. Preserved firmware, sector padding, and reserved regions
  are not sanitized. This is **not a whole-chip secure-erasure guarantee**.
- The dump reads through the CPU's memory mapping. Access to regions restricted
  by board security may fault. Encrypted or locked-board behavior is unverified.

### Validation status

Host-side simulated checks passed for partition bounds, full dump output with
partial Serial writes, preservation of excluded regions, `0xAA` fill and
readback, skipping unchanged sectors, rejection of invalid bounds, and
detection of flash and EEPROM readback mismatches.

**Compilation with the Teensy toolchain and execution on physical hardware have
not been verified.** The simulated checks do not establish those capabilities.

## `eeprom_sanitize/eeprom_sanitize.ino`

The original EEPROM-only sketch fills logical EEPROM with ASCII `A` (`0x41`)
and verifies it. It does not dump or overwrite unused program flash.
