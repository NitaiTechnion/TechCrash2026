// ============================================================
// CrashTech VLSI-2026 — Bad Apple OLED Homage (ESP32 side)
// ============================================================
// Procedural 128x64 monochrome animation for the kit SSD1306.
// It aims for the high-contrast PV rhythm without storing video frames.
// ============================================================

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "../../../../projects/common/esp32/pin_config.h"

Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

const uint16_t FRAME_MS = 67;  // ~15 FPS, still comfortable over I2C
const uint16_t SCENE_FRAMES = 48;

uint32_t frameNumber = 0;
uint32_t nextFrameAt = 0;

int tri(uint32_t t, int minValue, int maxValue, uint16_t period, uint16_t phase = 0) {
    uint16_t x = (t + phase) % period;
    uint16_t half = period / 2;
    int range = maxValue - minValue;

    if (x < half) {
        return minValue + ((int32_t)range * x) / half;
    }
    return maxValue - ((int32_t)range * (x - half)) / (period - half);
}

int saw(uint32_t t, int minValue, int maxValue, uint16_t period, uint16_t phase = 0) {
    return minValue + ((int32_t)(maxValue - minValue) * ((t + phase) % period)) / period;
}

void setInk(bool whiteInk) {
    oled.setTextColor(whiteInk ? SSD1306_WHITE : SSD1306_BLACK);
}

void drawApple(int centerX, int centerY, int radius, bool whiteInk) {
    uint16_t color = whiteInk ? SSD1306_WHITE : SSD1306_BLACK;
    uint16_t background = whiteInk ? SSD1306_BLACK : SSD1306_WHITE;

    oled.fillCircle(centerX - radius / 3, centerY, radius, color);
    oled.fillCircle(centerX + radius / 3, centerY, radius, color);
    oled.fillTriangle(centerX - radius, centerY - radius / 3,
                      centerX + radius, centerY - radius / 3,
                      centerX, centerY + radius + 3, color);
    oled.fillCircle(centerX + radius / 3, centerY - radius, radius / 2, background);
    oled.drawLine(centerX + radius / 5, centerY - radius - 2,
                  centerX + radius / 2, centerY - radius - 8, color);
    oled.fillCircle(centerX + radius / 2 + 3, centerY - radius - 8, 3, color);
}

void drawGirl(int x, int y, uint32_t pose, bool whiteInk, int scale = 1) {
    uint16_t c = whiteInk ? SSD1306_WHITE : SSD1306_BLACK;
    int lean = tri(pose, -3, 4, 24);
    int step = tri(pose, -5, 5, 16, 5);
    int arm = tri(pose, -10, 10, 18, 3);
    int s = scale;

    oled.fillCircle(x + lean, y - 29 * s, 4 * s, c);
    oled.fillTriangle(x - 7 * s + lean, y - 26 * s,
                      x + 7 * s + lean, y - 26 * s,
                      x + lean, y - 18 * s, c);
    oled.fillTriangle(x - 10 * s + lean, y - 18 * s,
                      x + 10 * s + lean, y - 18 * s,
                      x + lean, y + 1 * s, c);
    oled.fillTriangle(x - 9 * s + lean, y + 1 * s,
                      x + 9 * s + lean, y + 1 * s,
                      x + lean, y + 13 * s, c);
    oled.drawLine(x - 5 * s + lean, y - 19 * s, x - 22 * s, y - 13 * s + arm / 2, c);
    oled.drawLine(x + 5 * s + lean, y - 19 * s, x + 22 * s, y - 15 * s - arm / 2, c);
    oled.drawLine(x - 4 * s + lean, y + 9 * s, x - 11 * s - step, y + 25 * s, c);
    oled.drawLine(x + 4 * s + lean, y + 9 * s, x + 11 * s + step, y + 25 * s, c);
    oled.fillTriangle(x - 3 * s + lean, y - 33 * s,
                      x - 16 * s + lean, y - 20 * s,
                      x - 8 * s + lean, y - 16 * s, c);
    oled.fillTriangle(x + 1 * s + lean, y - 33 * s,
                      x + 18 * s + lean, y - 21 * s,
                      x + 8 * s + lean, y - 15 * s, c);
}

void drawProfile(int x, int y, bool whiteInk) {
    uint16_t c = whiteInk ? SSD1306_WHITE : SSD1306_BLACK;
    uint16_t bg = whiteInk ? SSD1306_BLACK : SSD1306_WHITE;
    oled.fillCircle(x, y, 20, c);
    oled.fillTriangle(x + 6, y - 14, x + 30, y - 2, x + 6, y + 8, c);
    oled.fillTriangle(x - 7, y + 13, x + 24, y + 44, x - 24, y + 43, c);
    oled.fillCircle(x + 15, y - 4, 3, bg);
    oled.fillRect(x + 14, y + 10, 17, 2, bg);
}

void drawRays(int cx, int cy, uint32_t t, bool whiteInk) {
    uint16_t c = whiteInk ? SSD1306_WHITE : SSD1306_BLACK;
    for (int i = 0; i < 12; i++) {
        int angle = (i * 17 + t * 3) % 64;
        int x1 = cx + tri(angle, -7, 7, 32);
        int y1 = cy + tri(angle + 8, -7, 7, 32);
        int x2 = cx + tri(angle, -68, 68, 64);
        int y2 = cy + tri(angle + 16, -40, 40, 64);
        oled.drawLine(x1, y1, x2, y2, c);
    }
}

void drawFallingShapes(uint32_t t, bool whiteInk) {
    uint16_t c = whiteInk ? SSD1306_WHITE : SSD1306_BLACK;
    for (int i = 0; i < 11; i++) {
        int x = (i * 23 + t * (2 + (i % 3))) % 146 - 10;
        int y = (i * 17 + t * (3 + (i & 1))) % 78 - 8;
        if (i & 1) {
            oled.fillTriangle(x, y, x + 8, y + 3, x + 1, y + 9, c);
        } else {
            oled.fillRect(x, y, 7, 7, c);
        }
    }
}

void sceneOpening(uint32_t t) {
    bool invert = ((t / 6) & 1);
    oled.fillScreen(invert ? SSD1306_WHITE : SSD1306_BLACK);
    setInk(!invert);
    oled.setTextSize(2);
    oled.setCursor(3, 2);
    oled.print("BAD");
    oled.setCursor(50, 44);
    oled.print("APPLE");
    drawApple(91, 25, tri(t, 7, 19, SCENE_FRAMES), !invert);
    drawRays(91, 25, t, !invert);
}

void sceneProfileCut(uint32_t t) {
    oled.fillScreen(SSD1306_WHITE);
    int wipe = saw(t, -30, 158, SCENE_FRAMES);
    oled.fillTriangle(0, 0, wipe, 0, 0, 64, SSD1306_BLACK);
    oled.fillTriangle(128, 64, wipe - 12, 64, 128, 0, SSD1306_BLACK);
    drawProfile(tri(t, 33, 96, SCENE_FRAMES), 25, t < 25);
}

void sceneRun(uint32_t t) {
    oled.fillRect(0, 50, 128, 14, SSD1306_WHITE);
    for (int x = -16; x < 128; x += 18) {
        oled.fillRect(x + (t * 3) % 18, 53, 8, 11, SSD1306_BLACK);
    }
    drawFallingShapes(t, true);
    drawGirl(tri(t, 28, 100, SCENE_FRAMES), 36, t * 2, true);
}

void sceneNegativeRun(uint32_t t) {
    oled.fillScreen(SSD1306_WHITE);
    for (int y = -9; y < 64; y += 13) {
        oled.fillRect(0, y + (t % 13), 128, 5, SSD1306_BLACK);
    }
    drawGirl(64, 36, t * 3, false);
    drawApple(saw(t, 132, -20, SCENE_FRAMES), tri(t, 9, 54, 28), 8, false);
}

void sceneCurtains(uint32_t t) {
    oled.fillScreen(SSD1306_WHITE);
    int gap = tri(t, 4, 58, SCENE_FRAMES);
    oled.fillTriangle(0, 0, 64 - gap, 0, 0, 64, SSD1306_BLACK);
    oled.fillTriangle(128, 0, 64 + gap, 0, 128, 64, SSD1306_BLACK);
    oled.fillTriangle(0, 64, 64 - gap / 2, 64, 0, 0, SSD1306_BLACK);
    oled.fillTriangle(128, 64, 64 + gap / 2, 64, 128, 0, SSD1306_BLACK);
    drawGirl(64, 35, t, false);
}

void scenePinwheel(uint32_t t) {
    bool invert = (t / 12) & 1;
    oled.fillScreen(invert ? SSD1306_WHITE : SSD1306_BLACK);
    uint16_t ink = invert ? SSD1306_BLACK : SSD1306_WHITE;
    for (int i = 0; i < 8; i++) {
        int phase = t * 2 + i * 8;
        int x = 64 + tri(phase, -52, 52, 64);
        int y = 32 + tri(phase + 16, -25, 25, 64);
        oled.fillTriangle(64, 32, x, y, 64 + tri(phase + 4, -52, 52, 64), 32 + tri(phase + 20, -25, 25, 64), ink);
    }
    oled.fillCircle(64, 32, 10, invert ? SSD1306_WHITE : SSD1306_BLACK);
    drawApple(64, 31, 9, !invert);
}

void sceneStairs(uint32_t t) {
    for (int i = 0; i < 9; i++) {
        int y = 63 - i * 7;
        int x = ((i * 15 + t * 2) % 40) - 24;
        oled.fillRect(x, y, 160, 4, SSD1306_WHITE);
    }
    drawGirl(34 + (t % 3), 34 - (t % 4), t * 2, true);
    drawGirl(94 - (t % 3), 43 + (t % 4), t * 2 + 12, true);
}

void sceneFinalFlash(uint32_t t) {
    bool invert = (t / 4) & 1;
    oled.fillScreen(invert ? SSD1306_WHITE : SSD1306_BLACK);
    drawProfile(42, 26, !invert);
    drawGirl(92, 38, t * 2, !invert);
    if ((t % 12) < 6) {
        drawApple(64, 24, 14, invert);
    }
}

void renderFrame(uint32_t frame) {
    oled.clearDisplay();
    uint32_t scene = (frame / SCENE_FRAMES) % 8;
    uint32_t localFrame = frame % SCENE_FRAMES;

    switch (scene) {
        case 0: sceneOpening(localFrame); break;
        case 1: sceneProfileCut(localFrame); break;
        case 2: sceneRun(localFrame); break;
        case 3: sceneNegativeRun(localFrame); break;
        case 4: sceneCurtains(localFrame); break;
        case 5: scenePinwheel(localFrame); break;
        case 6: sceneStairs(localFrame); break;
        default: sceneFinalFlash(localFrame); break;
    }

    oled.display();
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.println("CrashTech Bad Apple OLED Homage");

    Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
    if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
        Serial.println("OLED init failed (check SDA=21, SCL=22, 3.3V, GND)");
        while (true) delay(1000);
    }

    oled.setTextWrap(false);
    nextFrameAt = millis();
}

void loop() {
    uint32_t now = millis();
    if ((int32_t)(now - nextFrameAt) >= 0) {
        nextFrameAt += FRAME_MS;
        renderFrame(frameNumber++);
    }
}
