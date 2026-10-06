#include <Arduino.h>
#include <EEPROM.h>
#include <string.h>

#if !defined(ARDUINO_TEENSY41)
#error "Select Teensy 4.1 as the target board."
#endif

// DESTRUCTIVE: after the dump, overwrite unused program flash and logical
// EEPROM with 0xAA. This includes any LittleFS data in unused program flash.
// Firmware, its final erase-sector padding, and board-reserved flash remain.
// This runs on every boot. Replace the sketch after use.
// EEPROM emulation does not guarantee erasure of older physical records.
// Requires PJRC's Teensy core and its internal flash helpers below.
constexpr uint8_t kFillValue = 0xAA;
constexpr uintptr_t kFlashBase = 0x60000000u;
constexpr uintptr_t kProgramEnd = 0x607C0000u;
constexpr uintptr_t kEepromBackingEnd = 0x607FF000u;
constexpr uintptr_t kFlashEnd = 0x60800000u;
constexpr uint32_t kSectorBytes = 4096;
constexpr uint32_t kPageBytes = 256;

// Absolute linker symbol: its ADDRESS is the firmware image length.
extern unsigned long _flashimagelen;
extern "C" void eepromemu_flash_write(void* addr, const void* data, uint32_t len);
extern "C" void eepromemu_flash_erase_sector(void* addr);

uintptr_t userFlashStart = 0;
bool layoutValid = false;

bool findUserFlashStart(uintptr_t imageBytes, uintptr_t& start) {
  const uintptr_t programBytes = kProgramEnd - kFlashBase;
  if (imageBytes == 0 || imageBytes > programBytes) return false;
  // Preserve the whole sector containing the end of the firmware.
  const uintptr_t alignedBytes =
      (imageBytes + kSectorBytes - 1u) & ~uintptr_t(kSectorBytes - 1u);
  start = kFlashBase + alignedBytes;
  return start <= kProgramEnd;
}

void printRange(const char* title, uintptr_t start, uintptr_t end) {
  Serial.println();
  Serial.println(title);
  if (start == end) {
    Serial.println("Empty range.");
    return;
  }
  Serial.printf("0x%08lX through 0x%08lX (%lu bytes)\r\n",
                static_cast<unsigned long>(start),
                static_cast<unsigned long>(end - 1),
                static_cast<unsigned long>(end - start));

  constexpr char kHex[] = "0123456789ABCDEF";
  // These ranges are multiples of 16 bytes and together cover exactly 8 MiB.
  // Reading a region restricted by board security may still fault.
  for (uintptr_t address = start; address < end; address += 16) {
    char line[60];
    for (uint32_t digit = 0; digit < 8; ++digit) {
      line[digit] = kHex[(address >> (28u - 4u * digit)) & 0x0Fu];
    }
    line[8] = ':';
    line[9] = ' ';
    const volatile uint8_t* bytes =
        reinterpret_cast<const volatile uint8_t*>(address);
    for (uint32_t column = 0; column < 16; ++column) {
      const uint8_t value = bytes[column];
      const uint32_t position = 10u + 3u * column;
      line[position] = kHex[value >> 4];
      line[position + 1] = kHex[value & 0x0F];
      line[position + 2] = ' ';
    }
    line[58] = '\r';
    line[59] = '\n';

    size_t sent = 0;
    while (sent < sizeof(line)) {
      if (!Serial) {
        delay(10);
        continue;
      }
      const size_t written = Serial.write(
          reinterpret_cast<const uint8_t*>(line) + sent, sizeof(line) - sent);
      sent += written;
      if (written == 0) delay(1);
    }
  }
}

void printFlashMemory() {
  Serial.println("=== PRE-OVERWRITE FLASH DUMP: 8 MiB ===");
  Serial.println("Preserved means excluded by this sketch, not a hardware lock.");
  printRange("[PRESERVED] Firmware / boot headers / erase-sector padding",
             kFlashBase, userFlashStart);
  printRange("[USER-CONFIGURABLE] Unused program flash / LittleFS storage",
             userFlashStart, kProgramEnd);
  printRange("[USER-CONFIGURABLE, API-MANAGED] EEPROM backing records",
             kProgramEnd, kEepromBackingEnd);
  printRange("[PRESERVED] Board-reserved final 4 KiB",
             kEepromBackingEnd, kFlashEnd);
  Serial.println("=== FLASH DUMP COMPLETE: 8388608 bytes read ===");
  Serial.flush();
}

bool overwriteUserFlash() {
  // Recheck bounds immediately before any destructive operation.
  uintptr_t expectedStart = 0;
  if (!layoutValid ||
      !findUserFlashStart(reinterpret_cast<uintptr_t>(&_flashimagelen),
                         expectedStart) ||
      userFlashStart != expectedStart ||
      userFlashStart < kFlashBase || userFlashStart > kProgramEnd ||
      (userFlashStart % kSectorBytes) != 0) {
    Serial.println("FAIL: invalid flash bounds; no raw flash writes performed.");
    return false;
  }

  Serial.println("=== OVERWRITING USER FLASH WITH 0xAA ===");
  // RAM-backed page buffer: no reads from flash while programming is busy.
  uint8_t page[kPageBytes];
  memset(page, kFillValue, sizeof(page));
  uint32_t rewrittenSectors = 0;
  for (uintptr_t sector = userFlashStart; sector < kProgramEnd;
       sector += kSectorBytes) {
    const volatile uint8_t* bytes =
        reinterpret_cast<const volatile uint8_t*>(sector);
    bool alreadyFilled = true;
    for (uint32_t offset = 0; offset < kSectorBytes; ++offset) {
      if (bytes[offset] != kFillValue) {
        alreadyFilled = false;
        break;
      }
    }
    if (alreadyFilled) continue;  // Avoid repeat erases on later boots.

    // PJRC's helpers manage flash-controller access and cache invalidation.
    // Every erased sector lies wholly outside firmware and EEPROM backing.
    eepromemu_flash_erase_sector(reinterpret_cast<void*>(sector));
    for (uint32_t offset = 0; offset < kSectorBytes; offset += kPageBytes) {
      eepromemu_flash_write(reinterpret_cast<void*>(sector + offset),
                            page, sizeof(page));
    }
    for (uint32_t offset = 0; offset < kSectorBytes; ++offset) {
      if (bytes[offset] != kFillValue) {
        Serial.printf("FAIL: flash readback mismatch at 0x%08lX\r\n",
                      static_cast<unsigned long>(sector + offset));
        return false;
      }
    }
    ++rewrittenSectors;
    if ((rewrittenSectors % 16) == 0) {
      Serial.printf("Rewritten and verified %lu sectors.\r\n",
                    static_cast<unsigned long>(rewrittenSectors));
    }
    yield();
  }
  Serial.printf("PASS: %lu user flash bytes read as 0xAA; %lu sectors rewritten.\r\n",
                static_cast<unsigned long>(kProgramEnd - userFlashStart),
                static_cast<unsigned long>(rewrittenSectors));
  return true;
}

bool overwriteEeprom() {
  Serial.println("=== OVERWRITING LOGICAL EEPROM WITH 0xAA ===");
  const int eepromBytes = EEPROM.length();
  if (eepromBytes <= 0) {
    Serial.println("FAIL: EEPROM range is empty.");
    return false;
  }
  for (int address = 0; address < eepromBytes; ++address) {
    EEPROM.update(address, kFillValue);
  }
  uint32_t mismatchCount = 0;
  int firstMismatch = -1;
  for (int address = 0; address < eepromBytes; ++address) {
    if (EEPROM.read(address) != kFillValue) {
      ++mismatchCount;
      if (firstMismatch < 0) firstMismatch = address;
    }
  }
  if (mismatchCount != 0) {
    Serial.printf("FAIL: %lu EEPROM mismatches; first at offset %d.\r\n",
                  static_cast<unsigned long>(mismatchCount), firstMismatch);
    return false;
  }
  Serial.printf("PASS: all %d logical EEPROM bytes read as 0xAA.\r\n",
                eepromBytes);
  return true;
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  Serial.begin(115200);
  while (!Serial) delay(10);

  layoutValid = findUserFlashStart(
      reinterpret_cast<uintptr_t>(&_flashimagelen), userFlashStart);
  if (!layoutValid) {
    Serial.println("FAIL: invalid firmware image length; no overwrite performed.");
    return;
  }

  // Do not mount LittleFS here: begin() can format an unrecognized filesystem,
  // modifying evidence before the dump. Raw overwriting invalidates LittleFS.
  printFlashMemory();
  const bool flashOk = overwriteUserFlash();
  const bool eepromOk = overwriteEeprom();
  digitalWrite(LED_BUILTIN, (flashOk && eepromOk) ? HIGH : LOW);
  Serial.println((flashOk && eepromOk)
                     ? "COMPLETE: selected user regions verified as 0xAA."
                     : "FAILED: see verification results above.");
  Serial.println("Firmware and board-reserved flash were excluded from overwriting.");
}

void loop() {
  delay(10);
}
