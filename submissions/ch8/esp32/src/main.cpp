// ============================================================
// CrashTech VLSI-2026 — Alive Test (ESP32 side)
// ============================================================
// Interactive demo that exercises ALL kit peripherals at once:
//   - 3 LEDs blink continuously (chasing pattern)
//   - OLED shows button states + analog value live
//   - Button 1 (SW1) → low buzz (800 Hz)
//   - Button 2 (SW2) → high buzz (1500 Hz)
//   - Analog input (potentiometer) controls servo position
//   - FPGA UART: sends analog value, listens for echo
// ============================================================

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "../../../../projects/common/esp32/pin_config.h"

// Set to 0 when servo is wired (yellow=GPIO18, red=5V, brown=GND)
#define SKIP_SERVO 1

// ---- OLED ----
Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
bool oledOk = false;

// ---- UART to FPGA ----
HardwareSerial FpgaSerial(2);

// ---- LED state ----
const int ledPins[] = {PIN_LED_1, PIN_LED_2, PIN_LED_3};
int ledIdx = 0;
unsigned long ledTimer = 0;
const unsigned long LED_INTERVAL = 150;  // ms per step

// ---- Servo helpers ----
#if !SKIP_SERVO
bool servoAttached = false;

uint32_t angleToDuty(int deg) {
    // 50Hz PWM, 16-bit: 0°=0.5ms, 180°=2.5ms
    uint32_t us = 500 + ((uint32_t)deg * 2000 / 180);
    return (uint32_t)(us * 65536UL / 20000UL);
}
#endif

// ---- Button debounce ----
bool sw1Pressed = false;
bool sw2Pressed = false;
bool sw1Last = false;
bool sw2Last = false;
unsigned long sw1Debounce = 0;
unsigned long sw2Debounce = 0;
const unsigned long DEBOUNCE_MS = 50;

// ---- FPGA UART receive ----
String fpgaLine  = "";
String fpgaCount = "------";  // shown on OLED until first packet arrives

// ---- ESP32 countdown digit (sent to FPGA) ----
int espDigit = 9;
unsigned long espDigitTimer = 0;
const unsigned long ESP_DIGIT_INTERVAL = 1000;  // 1 second

// ---- Display throttle ----
unsigned long displayTimer = 0;
const unsigned long DISPLAY_INTERVAL = 80;  // ~12 FPS

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.println("========================================");
    Serial.println(" CrashTech VLSI-2026 — Alive Test");
    Serial.println("========================================");
    Serial.println(" LEDs: chasing");
    Serial.println(" Buttons: press for buzz + OLED");
    Serial.println(" Pot: controls servo + OLED bar");
    Serial.println("========================================");

    // GPIO init
    for (int i = 0; i < 3; i++) pinMode(ledPins[i], OUTPUT);
    pinMode(PIN_BUZZER, OUTPUT);
    pinMode(PIN_SW_1, INPUT_PULLUP);
    pinMode(PIN_SW_2, INPUT_PULLUP);

    // OLED init
    Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
    oledOk = oled.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR);
    if (!oledOk) {
        Serial.println("[!] OLED init failed (SDA=21, SCL=22)");
    }

    // Servo init
#if !SKIP_SERVO
    ledcAttach(PIN_SERVO, 50, 16);
    servoAttached = true;
    ledcWrite(PIN_SERVO, angleToDuty(90));  // center
#endif

    // FPGA UART init
    FpgaSerial.begin(FPGA_BAUD, SERIAL_8N1, PIN_FPGA_RX, PIN_FPGA_TX);
}

void loop() {
    unsigned long now = millis();

    // ---- FPGA UART receive: accumulate until '\n' ----
    while (FpgaSerial.available()) {
        char c = (char)FpgaSerial.read();
        Serial.printf("[FPGA RAW] 0x%02X '%c'\n", (uint8_t)c, (c >= 32 && c < 127) ? c : '.');
        if (c == '\n' || c == '\r') {
            if (fpgaLine.length() > 0) {
                fpgaCount = fpgaLine;
                Serial.printf("FPGA count: %s\n", fpgaCount.c_str());
                fpgaLine = "";
            }
        } else if (fpgaLine.length() < 16) {
            fpgaLine += c;
        }
    }

    // ---- ESP32 countdown digit -> FPGA every second ----
    if (now - espDigitTimer >= ESP_DIGIT_INTERVAL) {
        espDigitTimer = now;
        FpgaSerial.printf("%d\n", espDigit);
        Serial.printf("ESP32->FPGA: %d\n", espDigit);
        espDigit = (espDigit == 0) ? 9 : espDigit - 1;
    }

    // ---- LED chasing pattern ----
    if (now - ledTimer >= LED_INTERVAL) {
        ledTimer = now;
        for (int i = 0; i < 3; i++) digitalWrite(ledPins[i], LOW);
        digitalWrite(ledPins[ledIdx], HIGH);
        ledIdx = (ledIdx + 1) % 3;
    }

    // ---- Read buttons with debounce ----
    bool sw1Raw = (digitalRead(PIN_SW_1) == LOW);
    bool sw2Raw = (digitalRead(PIN_SW_2) == LOW);

    if (sw1Raw != sw1Last) sw1Debounce = now;
    if (sw2Raw != sw2Last) sw2Debounce = now;
    sw1Last = sw1Raw;
    sw2Last = sw2Raw;

    if ((now - sw1Debounce) > DEBOUNCE_MS) sw1Pressed = sw1Raw;
    if ((now - sw2Debounce) > DEBOUNCE_MS) sw2Pressed = sw2Raw;

    // ---- Buzzer: different tone per button ----
    bool anyPressed = sw1Pressed || sw2Pressed;
    static bool buzzerOn = false;
    if (sw1Pressed && sw2Pressed) {
        tone(PIN_BUZZER, 2000);   // both pressed: high pitch
        buzzerOn = true;
    } else if (sw1Pressed) {
        tone(PIN_BUZZER, 800);    // SW1: low buzz
        buzzerOn = true;
    } else if (sw2Pressed) {
        tone(PIN_BUZZER, 1500);   // SW2: high buzz
        buzzerOn = true;
    } else if (buzzerOn) {
        noTone(PIN_BUZZER);       // only call once on release
        buzzerOn = false;
    }

    // ---- Read analog ----
    int adcRaw = analogRead(PIN_ANALOG_IN);
    float voltage = adcRaw * 3.3f / 4095.0f;
    int percent = map(adcRaw, 0, 4095, 0, 100);

    // ---- Servo follows analog ----
#if !SKIP_SERVO
    int angle = map(adcRaw, 0, 4095, 0, 180);
    ledcWrite(PIN_SERVO, angleToDuty(angle));
#endif

    // ---- Update OLED ----
    if (oledOk && (now - displayTimer >= DISPLAY_INTERVAL)) {
        displayTimer = now;

        oled.clearDisplay();
        oled.setTextColor(SSD1306_WHITE);

        // Title
        oled.setTextSize(1);
        oled.setCursor(0, 0);
        oled.println("  ALIVE TEST");
        oled.drawFastHLine(0, 10, 128, SSD1306_WHITE);

        // Buttons
        oled.setCursor(0, 13);
        oled.print("SW1: ");
        oled.print(sw1Pressed ? "PRESSED" : "---");
        oled.setCursor(0, 22);
        oled.print("SW2: ");
        oled.print(sw2Pressed ? "PRESSED" : "---");

        // Analog value + bar
        oled.setCursor(0, 32);
        oled.printf("Pot: %d%%  %.1fV", percent, voltage);
        // Progress bar (compact, 7px tall)
        oled.drawRect(0, 41, 128, 7, SSD1306_WHITE);
        int barW = map(adcRaw, 0, 4095, 0, 126);
        oled.fillRect(1, 42, barW, 5, SSD1306_WHITE);

        // Bidirectional UART status
        oled.setCursor(0, 53);
        oled.printf("RX:%s TX:%d", fpgaCount.c_str(), espDigit);

        oled.display();
    }

    // ---- Serial output (throttled) ----
    static unsigned long serialTimer = 0;
    if (now - serialTimer >= 500) {
        serialTimer = now;
        Serial.printf("SW1=%s SW2=%s ADC=%d (%.1fV)",
                      sw1Pressed ? "ON " : "off",
                      sw2Pressed ? "ON " : "off",
                      adcRaw, voltage);
#if !SKIP_SERVO
        Serial.printf(" Servo=%ddeg", map(adcRaw, 0, 4095, 0, 180));
#endif
        Serial.println();
    }
}
