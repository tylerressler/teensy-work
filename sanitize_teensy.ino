#include <Arduino.h>
#include <EEPROM.h>
#include <LittleFS.h>


#if !defined(ARDUINO_TEENSY41)
#error "Select Teensy 4.1 as the target board."
#endif

// Destructive: overwrites every logical EEPROM byte on each boot.
// Keep power connected until verification completes. Remove this sketch
// afterward if future firmware should retain EEPROM settings.
// Flash-emulated EEPROM: this is NOT a physical secure-erasure guarantee.

constexpr uint8_t kFillValue = 0x41;  // ASCII 'A'

int eepromBytes = 0;
int mismatchCount = 0;
int firstMismatch = -1;
bool reported = false;

// Instantiate the upper flash filesystem
LittleFS_Program myFS;

void printFlashMemory() {
  constexpr uintptr_t kFlashBase = 0x60000000u;
  constexpr uint32_t kFlashBytes = 8u * 1024u * 1024u;
  constexpr uint32_t kBytesPerLine = 16;
  constexpr char kHex[] = "0123456789ABCDEF";
  const volatile uint8_t* flash =
      reinterpret_cast<const volatile uint8_t*>(kFlashBase);

  Serial.println("--- 3. Reading 8 MiB Memory-Mapped Flash ---");
  Serial.println("Address range: 0x60000000 through 0x607FFFFF");
  Serial.println("Each line contains an address and 16 hexadecimal bytes.");

  // Read through the CPU's flash mapping, without erasing or programming it.
  // Protected/unreadable regions can fault; validate on the target board.
  // This runs after the EEPROM writes and LittleFS initialization above.
  for (uint32_t offset = 0; offset < kFlashBytes; offset += kBytesPerLine) {
    char line[60];  // 8 address digits, ': ', 16 'XX ' bytes, and CRLF.
    const uint32_t address = static_cast<uint32_t>(kFlashBase + offset);
    for (uint32_t digit = 0; digit < 8; ++digit) {
      line[digit] = kHex[(address >> (28u - 4u * digit)) & 0x0Fu];
    }
    line[8] = ':';
    line[9] = ' ';

    for (uint32_t column = 0; column < kBytesPerLine; ++column) {
      const uint8_t value = flash[offset + column];
      const uint32_t position = 10u + 3u * column;
      line[position] = kHex[value >> 4];
      line[position + 1] = kHex[value & 0x0F];
      line[position + 2] = ' ';
    }
    line[58] = '\r';
    line[59] = '\n';

    // Retry unsent bytes if the host is slow; pause if USB disconnects.
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
  Serial.println("--- Flash Dump Complete: 8388608 bytes read ---");
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  eepromBytes = EEPROM.length();
  for (int address = 0; address < eepromBytes; ++address) {
    // Avoid an unnecessary write when the byte already equals 'A'.
    EEPROM.update(address, kFillValue);
  }

  for (int address = 0; address < eepromBytes; ++address) {
    if (EEPROM.read(address) != kFillValue) {
      ++mismatchCount;
      if (firstMismatch < 0) firstMismatch = address;
    }
  }

  // A steady LED means the entire logical EEPROM range verified.
  digitalWrite(LED_BUILTIN,
               (eepromBytes > 0 && mismatchCount == 0) ? HIGH : LOW);
              
  // Memory print

  Serial.begin(9600);
  while (!Serial) { } // Wait for Serial Monitor to open

  Serial.println("\n==============================================");
  Serial.println("         TEENSY 4.1 MEMORY VIEWER               ");
  Serial.println("==============================================\n");

  // ----------------------------------------------------
  // PART A: Print LittleFS Upper Flash File System
  // ----------------------------------------------------
  Serial.println("--- 1. Reading Upper Flash Filesystem (LittleFS) ---");
  
  // Initialize LittleFS allocating 1MB of space for testing
  // (Adjust the size mapping to match flash disk setup if initialized previously)
  if (myFS.begin(1 * 1024 * 1024)) {
    Serial.print("Filesystem Size: ");
    Serial.print(myFS.totalSize());
    Serial.print(" bytes | Used: ");
    Serial.print(myFS.usedSize());
    Serial.println(" bytes");

    // Open root directory to list stored configuration files
    File root = myFS.open("/");
    File entry = root.openNextFile();
    
    if (!entry) {
      Serial.println("No files found on the upper flash filesystem.");
    } else {
      while (entry) {
        Serial.print("  [File] Name: ");
        Serial.print(entry.name());
        Serial.print(" \t Size: ");
        Serial.print(entry.size(), DEC);
        Serial.println(" bytes");
        
        entry.close();
        entry = root.openNextFile();
      }
    }
    root.close();
  } else {
    Serial.println("Warning: LittleFS upper flash system not initialized or unformatted.");
  }
  Serial.println();

  // ----------------------------------------------------
  // PART B: Print Emulated EEPROM Bytes
  // ----------------------------------------------------
  Serial.println("--- 1. Reading EEPROM Bytes  ---");
  int eepromPrintLimit = 4284; // limits lines printed, max 4284
  
  for (int address = 0; address < eepromPrintLimit; address++) {
    byte value = EEPROM.read(address);
    Serial.print("Addr ");
    Serial.print(address);
    Serial.print(": ");
    Serial.println(value);
  }

  // ----------------------------------------------------
  // PART C: Print Entire 8 MiB Memory-Mapped Flash Range
  // ----------------------------------------------------
  Serial.println();
  printFlashMemory();

  Serial.println("\n--- Memory Dump Complete ---");
}


void loop() {
  // Sanitization does not depend on a connected USB serial monitor.
  // Report once when the monitor connects, even after setup has finished.
  if (Serial && !reported) {
    Serial.print("EEPROM bytes checked: ");
    Serial.println(eepromBytes);
    if (eepromBytes > 0 && mismatchCount == 0) {
      Serial.println("PASS: every logical EEPROM byte reads 0x41 ('A').");
    } else {
      Serial.print("FAIL: mismatched bytes: ");
      Serial.println(mismatchCount);
      Serial.print("First mismatch address (-1 if none): ");
      Serial.println(firstMismatch);
    }
    reported = true;
  }
  delay(10);
}
