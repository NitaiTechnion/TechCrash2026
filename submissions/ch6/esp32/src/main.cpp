// ============================================================
// CrashTech VLSI-2026 — Challenge 6: Frequency Detector (ESP32 side)
// ============================================================
// Communicates with FPGA frequency detector via UART.
// Sends commands to configure measurement and receives frequency data.
// ============================================================

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "../../../../projects/common/esp32/pin_config.h"

// UART to FPGA
HardwareSerial FpgaSerial(2);

// Buffer for incoming UART data
String fpgaBuffer = "";

int8_t sinewave[256];

void createSineWave(uint32_t frequency) {
    for (int i = 0; i < 256; i++) {
        sinewave[i] = (int8_t)(127 * sin(2 * PI * frequency * i / 8000));
    }
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.println("========================================");
    Serial.println(" CrashTech VLSI-2026 — Challenge 6");
    Serial.println(" Frequency Detector (ESP32 side)");
    Serial.println("========================================");

    // ADC setup for potentiometer
    pinMode(PIN_ANALOG_IN, INPUT);
    analogReadResolution(12);           // 12-bit resolution (0-4095)
    analogSetAttenuation(ADC_11db);     // Full 3.3V range

    // FPGA UART init
    FpgaSerial.begin(115200, SERIAL_8N1, PIN_FPGA_RX, PIN_FPGA_TX);
}

void loop() {
    // Read potentiometer
    int adcRaw = analogRead(PIN_ANALOG_IN);
    uint32_t freq = map(adcRaw, 0, 4095, 100, 2000); // Map to 100-2000

    // Generate sine wave
    createSineWave(freq);

    Serial.println("adcRaw = " + String(adcRaw) + ", freq=" + String(freq));
    Serial.println("Last 5 samples:" + String(sinewave[251]) + ", " + String(sinewave[252]) + ", " + String(sinewave[253]) + ", " + String(sinewave[254]) + ", " + String(sinewave[255]));
    Serial.println("Sending " + String(freq) + " Hz sine wave to FPGA...");

    // Send sine wave to FPGA
    for (int i = 0; i < 256; i++) {
        FpgaSerial.write(sinewave[i]);
    }

    Serial.println("Done.");

    delay(500);
}
