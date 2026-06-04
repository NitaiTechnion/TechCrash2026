// ============================================================
// CrashTech VLSI-2026 — Game Controller (ESP32 side)
// ============================================================
// Receives SW/KEY state from FPGA over UART and displays on OLED.
//   byte 0: {2'b01, SW[4:0], KEY[0]}   header bits [7:6] = 01
//   byte 1: {2'b10, SW[9:5], KEY[1]}   header bits [7:6] = 10
//
// KEY on DE10-Lite is active-low: bit=0 means pressed.
// FPGA UART: 9600 8N1, ESP32 RX=GPIO17, TX=GPIO16
// ============================================================

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "../../../../projects/common/esp32/pin_config.h"

// ---- OLED ----
Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
bool oledOk = false;

// ---- UART to FPGA ----
HardwareSerial FpgaSerial(2);

// ---- Decoded FPGA state ----
uint8_t fpga_sw_lo = 0;    // SW[4:0]
uint8_t fpga_sw_hi = 0;    // SW[9:5]
bool    fpga_key0  = true;  // raw bit (active-low: false = pressed)
bool    fpga_key1  = true;
bool    fpga_valid = false;
uint16_t last_packed = 0xFFFF;

// ---- Display throttle ----
unsigned long displayTimer = 0;
const unsigned long DISPLAY_INTERVAL = 50;  // ~20 FPS

// Draw a filled or empty 10x12 box at (x, y)
void drawSwBox(int x, int y, bool on) {
    if (on)
        oled.fillRect(x, y, 10, 12, SSD1306_WHITE);
    else
        oled.drawRect(x, y, 10, 12, SSD1306_WHITE);
}

void emitPackedStateIfChanged() {
    uint16_t sw10 = ((uint16_t)(fpga_sw_hi & 0x1F) << 5) | (fpga_sw_lo & 0x1F);
    uint16_t key1Pressed = fpga_key1 ? 0 : 1;
    uint16_t key0Pressed = fpga_key0 ? 0 : 1;
    uint16_t packed = (sw10 << 2) | (key1Pressed << 1) | key0Pressed;

    if (packed != last_packed) {
        last_packed = packed;
        Serial.printf("%03X\n", packed);
    }
}

void setup() {
    Serial.begin(115200);

    Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
    oledOk = oled.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR);
    if (!oledOk)
        Serial.println("[!] OLED init failed");

    FpgaSerial.begin(FPGA_BAUD, SERIAL_8N1, PIN_FPGA_RX, PIN_FPGA_TX);
    Serial.println("Game Controller RX ready");
}

void loop() {
    unsigned long now = millis();

    // ---- FPGA UART receive ----
    while (FpgaSerial.available()) {
        uint8_t b = FpgaSerial.read();
        uint8_t hdr = (b >> 6) & 0x03;
        if (hdr == 0x01) {
            // byte 0: {2'b01, SW[4:0], KEY[0]}
            fpga_sw_lo = (b >> 1) & 0x1F;
            fpga_key0  = (b & 0x01) != 0;
            fpga_valid = true;
            emitPackedStateIfChanged();
        } else if (hdr == 0x02) {
            // byte 1: {2'b10, SW[9:5], KEY[1]}
            fpga_sw_hi = (b >> 1) & 0x1F;
            fpga_key1  = (b & 0x01) != 0;
            fpga_valid = true;
            emitPackedStateIfChanged();
        }
    }

    // ---- Update OLED ----
    if (oledOk && (now - displayTimer >= DISPLAY_INTERVAL)) {
        displayTimer = now;

        oled.clearDisplay();
        oled.setTextColor(SSD1306_WHITE);
        oled.setTextSize(1);

        // Title
        oled.setCursor(8, 0);
        oled.print("FPGA GAME CONTROLLER");
        oled.drawFastHLine(0, 9, 128, SSD1306_WHITE);

        if (!fpga_valid) {
            oled.setCursor(16, 28);
            oled.print("Waiting for FPGA...");
        } else {
            // KEY row (active-low: bit=0 means pressed)
            oled.setCursor(0, 12);
            oled.printf("KEY0: %-7s KEY1: %s",
                        !fpga_key0 ? "PRESSED" : "---",
                        !fpga_key1 ? "PRESSED" : "---");

            // SW label row
            oled.setCursor(0, 24);
            oled.print("SW9              SW0");

            // Switch boxes: SW9 on left, SW0 on right
            // 10 boxes * (10px wide + 2px gap) = 120px, starting at x=4
            for (int i = 0; i < 10; i++) {
                int bit = 9 - i;  // i=0 -> SW9, i=9 -> SW0
                bool on;
                if (bit >= 5)
                    on = (fpga_sw_hi >> (bit - 5)) & 1;
                else
                    on = (fpga_sw_lo >> bit) & 1;
                drawSwBox(4 + i * 12, 34, on);
            }

            // Numeric labels under boxes
            oled.setCursor(4, 49);
            oled.print("9 8 7 6 5 4 3 2 1 0");
        }

        oled.display();
    }
}
