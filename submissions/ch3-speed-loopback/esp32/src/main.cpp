// ============================================================
// CrashTech VLSI-2026 — CH3 Speed Loopback (ESP32 side)
// ============================================================
// Sends bytes to FPGA over UART and measures loopback latency.
//
// UART: GPIO16 (TX to FPGA RX), GPIO17 (RX from FPGA TX)
// 9600 baud, 8N1, 3.3V logic
// ============================================================

#include <Arduino.h>
#include "../../../../projects/common/esp32/pin_config.h"

HardwareSerial FpgaSerial(2);  // UART2: TX=GPIO16, RX=GPIO17

void setup() {
    Serial.begin(115200);
    FpgaSerial.begin(9600, SERIAL_8N1, FPGA_RX_PIN, FPGA_TX_PIN);
    Serial.println("CH3 Speed Loopback ready");
}

void loop() {
    // Echo any bytes received from FPGA back to Serial monitor
    while (FpgaSerial.available()) {
        uint8_t b = FpgaSerial.read();
        Serial.printf("RX: 0x%02X\n", b);
    }

    // Forward any bytes from Serial monitor to FPGA
    while (Serial.available()) {
        uint8_t b = Serial.read();
        FpgaSerial.write(b);
    }
}
