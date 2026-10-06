#include <Arduino.h>
#include <EEPROM.h>

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
