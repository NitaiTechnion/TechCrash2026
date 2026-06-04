#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "../../../../projects/common/esp32/pin_config.h"

Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
HardwareSerial FpgaSerial(2);

namespace {
constexpr int SCREEN_W = OLED_WIDTH;
constexpr int SCREEN_H = OLED_HEIGHT;

constexpr int BIRD_X = 24;
constexpr int BIRD_SIZE = 6;
constexpr float GRAVITY = 0.32f;
constexpr float FLAP_VELOCITY = -2.9f;

constexpr int PIPE_WIDTH = 16;
constexpr int PIPE_COUNT = 2;
constexpr int PIPE_SPACING = 74;

constexpr int BASE_GAP = 30;
constexpr int MIN_GAP = 16;

constexpr float BASE_SPEED = 1.15f;
constexpr float SPEED_STEP = 0.08f;
constexpr int GAP_STEP = 1;

constexpr uint32_t GAME_TICK_MS = 20;
constexpr uint32_t RENDER_MS = 20;
}

enum class GameState : uint8_t {
    Running,
    GameOver
};

struct Pipe {
    float x;
    int gapY;
};

bool oledOk = false;
GameState gameState = GameState::Running;
Pipe pipes[PIPE_COUNT];

float birdY = SCREEN_H / 2.0f;
float birdV = 0.0f;
uint16_t score = 0;
bool pendingFlap = false;
int difficulty = 0;
float obstacleSpeed = BASE_SPEED;
int obstacleGap = BASE_GAP;

String rxLine;

uint32_t tickTimer = 0;
uint32_t renderTimer = 0;

int clampInt(int value, int low, int high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

int parseDifficultyValue(const String &text) {
    if (text.length() == 1) {
        char c = text.charAt(0);
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    }
    return text.toInt();
}

float randomGapCenter() {
    const int margin = obstacleGap / 2 + 5;
    const int low = margin;
    const int high = SCREEN_H - margin;
    if (high <= low) return SCREEN_H / 2.0f;
    return (float)random(low, high + 1);
}

void applyDifficulty(int diff) {
    difficulty = clampInt(diff, 0, 15);
    obstacleSpeed = BASE_SPEED + SPEED_STEP * (float)difficulty;
    obstacleGap = BASE_GAP - GAP_STEP * difficulty;
    if (obstacleGap < MIN_GAP) obstacleGap = MIN_GAP;
}

void resetGame() {
    birdY = SCREEN_H / 2.0f;
    birdV = 0.0f;
    score = 0;
    pendingFlap = false;

    for (int i = 0; i < PIPE_COUNT; ++i) {
        pipes[i].x = SCREEN_W + i * PIPE_SPACING;
        pipes[i].gapY = (int)randomGapCenter();
    }

    gameState = GameState::Running;
}

void flapAction() {
    if (gameState == GameState::GameOver) {
        Serial.println("[GAME] restart");
        resetGame();
        return;
    }
    pendingFlap = true;
    Serial.println("[GAME] flap");
}

void processLine(const String &line) {
    Serial.printf("[UART] %s\n", line.c_str());

    if (line == "FLAP") {
        flapAction();
        return;
    }

    if (line.startsWith("DIFF:")) {
        String valueText = line.substring(5);
        int parsed = parseDifficultyValue(valueText);
        applyDifficulty(parsed);
        Serial.printf("[GAME] difficulty=%d speed=%.2f gap=%d\n", difficulty, obstacleSpeed, obstacleGap);
    }
}

void pollFpgaUart() {
    while (FpgaSerial.available()) {
        char c = (char)FpgaSerial.read();
        if (c == '\n' || c == '\r') {
            if (rxLine.length() > 0) {
                processLine(rxLine);
                rxLine = "";
            }
        } else if (rxLine.length() < 24) {
            rxLine += c;
        }
    }
}

bool birdHitsPipe(const Pipe &pipe) {
    const float birdLeft = (float)BIRD_X;
    const float birdRight = (float)(BIRD_X + BIRD_SIZE);
    const float pipeLeft = pipe.x;
    const float pipeRight = pipe.x + PIPE_WIDTH;

    if (birdRight < pipeLeft || birdLeft > pipeRight) {
        return false;
    }

    int gapTop = pipe.gapY - obstacleGap / 2;
    int gapBottom = pipe.gapY + obstacleGap / 2;

    int birdTop = (int)birdY;
    int birdBottom = (int)birdY + BIRD_SIZE;

    return (birdTop < gapTop) || (birdBottom > gapBottom);
}

void updateGameTick() {
    if (gameState != GameState::Running) return;

    if (pendingFlap) {
        birdV = FLAP_VELOCITY;
        pendingFlap = false;
    }

    birdV += GRAVITY;
    birdY += birdV;

    if (birdY <= 0 || (birdY + BIRD_SIZE) >= SCREEN_H) {
        gameState = GameState::GameOver;
        Serial.printf("[GAME] over (wall) score=%u\n", score);
        return;
    }

    for (int i = 0; i < PIPE_COUNT; ++i) {
        float oldX = pipes[i].x;
        pipes[i].x -= obstacleSpeed;

        if (oldX + PIPE_WIDTH >= BIRD_X && pipes[i].x + PIPE_WIDTH < BIRD_X) {
            score++;
        }

        if (pipes[i].x + PIPE_WIDTH < 0) {
            float maxX = pipes[0].x;
            for (int j = 1; j < PIPE_COUNT; ++j) {
                if (pipes[j].x > maxX) maxX = pipes[j].x;
            }
            pipes[i].x = maxX + PIPE_SPACING;
            pipes[i].gapY = (int)randomGapCenter();
        }

        if (birdHitsPipe(pipes[i])) {
            gameState = GameState::GameOver;
            Serial.printf("[GAME] over (pipe) score=%u\n", score);
            return;
        }
    }
}

void drawPipe(const Pipe &pipe) {
    int x = (int)pipe.x;
    int gapTop = pipe.gapY - obstacleGap / 2;
    int gapBottom = pipe.gapY + obstacleGap / 2;

    if (x >= SCREEN_W || (x + PIPE_WIDTH) < 0) {
        return;
    }

    oled.fillRect(x, 0, PIPE_WIDTH, gapTop, SSD1306_WHITE);
    oled.fillRect(x, gapBottom, PIPE_WIDTH, SCREEN_H - gapBottom, SSD1306_WHITE);
}

void renderGame() {
    if (!oledOk) return;

    oled.clearDisplay();

    for (int i = 0; i < PIPE_COUNT; ++i) {
        drawPipe(pipes[i]);
    }

    oled.fillRect(BIRD_X, (int)birdY, BIRD_SIZE, BIRD_SIZE, SSD1306_WHITE);

    oled.setTextColor(SSD1306_WHITE);
    oled.setTextSize(1);
    oled.setCursor(0, 0);
    oled.printf("S:%u D:%d", score, difficulty);

    if (gameState == GameState::GameOver) {
        oled.fillRect(13, 20, 102, 24, SSD1306_BLACK);
        oled.drawRect(13, 20, 102, 24, SSD1306_WHITE);
        oled.setCursor(22, 28);
        oled.print("GAME OVER");
        oled.setCursor(18, 37);
        oled.print("PRESS FLAP");
    }

    oled.display();
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println("[BOOT] flappy esp32 start");

    randomSeed((uint32_t)esp_random());

    Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
    oledOk = oled.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR);

    FpgaSerial.begin(FPGA_BAUD, SERIAL_8N1, PIN_FPGA_RX, PIN_FPGA_TX);

    applyDifficulty(0);
    resetGame();
    Serial.println("[BOOT] ready");
}

void loop() {
    uint32_t now = millis();

    pollFpgaUart();

    if (now - tickTimer >= GAME_TICK_MS) {
        tickTimer = now;
        updateGameTick();
    }

    if (now - renderTimer >= RENDER_MS) {
        renderTimer = now;
        renderGame();
    }
}
