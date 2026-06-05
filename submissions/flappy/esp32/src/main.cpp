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

constexpr int POP_SIZE = 30;
constexpr int INPUT_COUNT = 4;
constexpr int HIDDEN_COUNT = 4;
constexpr int ELITE_COUNT = 4;
constexpr int PARENT_POOL = 8;

constexpr int BIRD_X = 24;
constexpr int BIRD_SIZE = 3;
constexpr float GRAVITY = 0.32f;
constexpr float FLAP_VELOCITY = -3.0f;
constexpr float MAX_VELOCITY = 4.0f;

constexpr int PIPE_WIDTH = 16;
constexpr int PIPE_COUNT = 2;
constexpr int PIPE_SPACING = 74;
constexpr int BASE_GAP = 30;
constexpr int MIN_GAP = 16;
constexpr float BASE_SPEED = 1.15f;
constexpr float SPEED_STEP = 0.08f;
constexpr int GAP_STEP = 1;

constexpr uint32_t GAME_TICK_MS = 18;
constexpr uint32_t RENDER_MS = 100;
constexpr uint16_t MAX_GENERATION_TICKS = 6000;
constexpr uint32_t COURSE_SEED = 0xC0FFEE21UL;
}

struct Brain {
    float hidden[HIDDEN_COUNT][INPUT_COUNT + 1];
    float output[HIDDEN_COUNT + 1];
};

struct Bird {
    float y;
    float velocity;
    float fitness;
    uint16_t ticksAlive;
    uint16_t score;
    uint16_t lastScoredPipeId;
    bool alive;
    Brain brain;
};

struct Pipe {
    float x;
    int gapY;
    uint16_t id;
};

bool oledOk = false;
Bird birds[POP_SIZE];
Pipe pipes[PIPE_COUNT];

uint16_t generation = 1;
uint16_t generationTick = 0;
uint16_t aliveCount = 0;
uint16_t bestScoreThisGeneration = 0;
uint16_t bestScoreEver = 0;
float bestFitnessThisGeneration = 0.0f;
float bestFitnessEver = 0.0f;
int bestBirdIndex = 0;

int difficulty = 0;
float obstacleSpeed = BASE_SPEED;
int obstacleGap = BASE_GAP;
uint32_t courseState = COURSE_SEED;
uint16_t nextPipeId = 1;

String rxLine;
uint32_t tickTimer = 0;
uint32_t renderTimer = 0;

int clampInt(int value, int low, int high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

float clampFloat(float value, float low, float high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

float randomUnit() {
    return (float)random(0, 20001) / 10000.0f - 1.0f;
}

float randomWeight() {
    return randomUnit() * 1.25f;
}

float mutationNoise(float amount) {
    return randomUnit() * amount;
}

uint32_t nextCourseRandom() {
    courseState = courseState * 1664525UL + 1013904223UL;
    return courseState;
}

int nextGapY() {
    int margin = obstacleGap / 2 + 6;
    int low = margin;
    int high = SCREEN_H - margin;
    if (high <= low) return SCREEN_H / 2;
    return low + (int)(nextCourseRandom() % (uint32_t)(high - low + 1));
}

float activate(float value) {
    return tanhf(value);
}

float sigmoid(float value) {
    value = clampFloat(value, -12.0f, 12.0f);
    return 1.0f / (1.0f + expf(-value));
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

void applyDifficulty(int diff) {
    difficulty = clampInt(diff, 0, 15);
    obstacleSpeed = BASE_SPEED + SPEED_STEP * (float)difficulty;
    obstacleGap = BASE_GAP - GAP_STEP * difficulty;
    if (obstacleGap < MIN_GAP) obstacleGap = MIN_GAP;
}

void randomizeBrain(Brain &brain) {
    for (int hidden = 0; hidden < HIDDEN_COUNT; ++hidden) {
        for (int input = 0; input <= INPUT_COUNT; ++input) {
            brain.hidden[hidden][input] = randomWeight();
        }
    }

    for (int hidden = 0; hidden <= HIDDEN_COUNT; ++hidden) {
        brain.output[hidden] = randomWeight();
    }
}

void mutateBrain(Brain &brain) {
    for (int hidden = 0; hidden < HIDDEN_COUNT; ++hidden) {
        for (int input = 0; input <= INPUT_COUNT; ++input) {
            int roll = random(100);
            if (roll < 16) brain.hidden[hidden][input] += mutationNoise(0.22f);
            if (roll < 3) brain.hidden[hidden][input] += mutationNoise(0.90f);
            brain.hidden[hidden][input] = clampFloat(brain.hidden[hidden][input], -4.0f, 4.0f);
        }
    }

    for (int hidden = 0; hidden <= HIDDEN_COUNT; ++hidden) {
        int roll = random(100);
        if (roll < 16) brain.output[hidden] += mutationNoise(0.22f);
        if (roll < 3) brain.output[hidden] += mutationNoise(0.90f);
        brain.output[hidden] = clampFloat(brain.output[hidden], -4.0f, 4.0f);
    }
}

Pipe &nextPipeForBird() {
    int bestIndex = 0;
    float bestDistance = 10000.0f;
    for (int i = 0; i < PIPE_COUNT; ++i) {
        float distance = (pipes[i].x + PIPE_WIDTH) - BIRD_X;
        if (distance >= -2.0f && distance < bestDistance) {
            bestDistance = distance;
            bestIndex = i;
        }
    }
    return pipes[bestIndex];
}

bool shouldFlap(const Bird &bird) {
    Pipe &pipe = nextPipeForBird();
    float inputs[INPUT_COUNT];
    inputs[0] = (bird.y / (float)SCREEN_H) * 2.0f - 1.0f;
    inputs[1] = clampFloat(bird.velocity / MAX_VELOCITY, -1.0f, 1.0f);
    inputs[2] = clampFloat((pipe.x - (float)BIRD_X) / (float)SCREEN_W, -1.0f, 1.0f);
    inputs[3] = clampFloat(((float)pipe.gapY - bird.y) / (float)SCREEN_H, -1.0f, 1.0f);

    float hiddenValues[HIDDEN_COUNT];
    for (int hidden = 0; hidden < HIDDEN_COUNT; ++hidden) {
        float sum = bird.brain.hidden[hidden][INPUT_COUNT];
        for (int input = 0; input < INPUT_COUNT; ++input) {
            sum += inputs[input] * bird.brain.hidden[hidden][input];
        }
        hiddenValues[hidden] = activate(sum);
    }

    float outputSum = bird.brain.output[HIDDEN_COUNT];
    for (int hidden = 0; hidden < HIDDEN_COUNT; ++hidden) {
        outputSum += hiddenValues[hidden] * bird.brain.output[hidden];
    }

    return sigmoid(outputSum) > 0.55f;
}

void resetPipes() {
    courseState = COURSE_SEED;
    nextPipeId = 1;
    for (int i = 0; i < PIPE_COUNT; ++i) {
        pipes[i].x = SCREEN_W + i * PIPE_SPACING;
        pipes[i].gapY = nextGapY();
        pipes[i].id = nextPipeId++;
    }
}

void resetBirdPhysics(Bird &bird) {
    bird.y = SCREEN_H / 2.0f;
    bird.velocity = 0.0f;
    bird.fitness = 0.0f;
    bird.ticksAlive = 0;
    bird.score = 0;
    bird.lastScoredPipeId = 0;
    bird.alive = true;
}

void startGeneration() {
    resetPipes();
    generationTick = 0;
    aliveCount = POP_SIZE;
    bestScoreThisGeneration = 0;
    bestFitnessThisGeneration = 0.0f;
    bestBirdIndex = 0;

    for (int i = 0; i < POP_SIZE; ++i) {
        resetBirdPhysics(birds[i]);
    }
}

void initPopulation() {
    for (int i = 0; i < POP_SIZE; ++i) {
        randomizeBrain(birds[i].brain);
    }
    startGeneration();
}

void sortBirdsByFitness() {
    for (int i = 0; i < POP_SIZE - 1; ++i) {
        int bestIndex = i;
        for (int j = i + 1; j < POP_SIZE; ++j) {
            if (birds[j].fitness > birds[bestIndex].fitness) {
                bestIndex = j;
            }
        }
        if (bestIndex != i) {
            Bird temp = birds[i];
            birds[i] = birds[bestIndex];
            birds[bestIndex] = temp;
        }
    }
}

void evolvePopulation() {
    sortBirdsByFitness();

    if (birds[0].fitness > bestFitnessEver) bestFitnessEver = birds[0].fitness;
    if (birds[0].score > bestScoreEver) bestScoreEver = birds[0].score;

    Brain nextBrains[POP_SIZE];
    for (int i = 0; i < ELITE_COUNT; ++i) {
        nextBrains[i] = birds[i].brain;
    }

    for (int i = ELITE_COUNT; i < POP_SIZE; ++i) {
        int parentIndex = random(PARENT_POOL);
        nextBrains[i] = birds[parentIndex].brain;
        mutateBrain(nextBrains[i]);
    }

    if (random(100) < 25) {
        randomizeBrain(nextBrains[POP_SIZE - 1]);
    }

    for (int i = 0; i < POP_SIZE; ++i) {
        birds[i].brain = nextBrains[i];
    }

    generation++;
    startGeneration();
}

bool birdHitsPipe(const Bird &bird, const Pipe &pipe) {
    float birdLeft = (float)BIRD_X;
    float birdRight = (float)(BIRD_X + BIRD_SIZE);
    float pipeLeft = pipe.x;
    float pipeRight = pipe.x + PIPE_WIDTH;

    if (birdRight < pipeLeft || birdLeft > pipeRight) {
        return false;
    }

    int gapTop = pipe.gapY - obstacleGap / 2;
    int gapBottom = pipe.gapY + obstacleGap / 2;
    int birdTop = (int)bird.y;
    int birdBottom = (int)bird.y + BIRD_SIZE;

    return (birdTop < gapTop) || (birdBottom > gapBottom);
}

void updatePipes() {
    for (int i = 0; i < PIPE_COUNT; ++i) {
        pipes[i].x -= obstacleSpeed;

        if (pipes[i].x + PIPE_WIDTH < 0) {
            float maxX = pipes[0].x;
            for (int j = 1; j < PIPE_COUNT; ++j) {
                if (pipes[j].x > maxX) maxX = pipes[j].x;
            }
            pipes[i].x = maxX + PIPE_SPACING;
            pipes[i].gapY = nextGapY();
            pipes[i].id = nextPipeId++;
        }
    }
}

void updatePopulation() {
    generationTick++;
    updatePipes();

    aliveCount = 0;
    bestFitnessThisGeneration = 0.0f;
    bestScoreThisGeneration = 0;

    for (int i = 0; i < POP_SIZE; ++i) {
        Bird &bird = birds[i];
        if (!bird.alive) {
            if (bird.fitness > bestFitnessThisGeneration) {
                bestFitnessThisGeneration = bird.fitness;
                bestBirdIndex = i;
            }
            if (bird.score > bestScoreThisGeneration) bestScoreThisGeneration = bird.score;
            continue;
        }

        bool flap = shouldFlap(bird);
        if (flap) bird.velocity = FLAP_VELOCITY;

        bird.velocity += GRAVITY;
        bird.velocity = clampFloat(bird.velocity, -MAX_VELOCITY, MAX_VELOCITY);
        bird.y += bird.velocity;
        bird.ticksAlive++;

        bool dead = (bird.y <= 0) || ((bird.y + BIRD_SIZE) >= SCREEN_H);

        for (int p = 0; p < PIPE_COUNT && !dead; ++p) {
            if (birdHitsPipe(bird, pipes[p])) dead = true;
        }

        for (int p = 0; p < PIPE_COUNT; ++p) {
            if (pipes[p].x + PIPE_WIDTH < BIRD_X && pipes[p].id > bird.lastScoredPipeId) {
                bird.score++;
                bird.lastScoredPipeId = pipes[p].id;
            }
        }

        bird.fitness = (float)bird.ticksAlive + (float)bird.score * 600.0f + max(0.0f, pipes[0].x < pipes[1].x ? pipes[0].x : pipes[1].x);

        if (dead) {
            bird.alive = false;
        } else {
            aliveCount++;
        }

        if (bird.fitness > bestFitnessThisGeneration) {
            bestFitnessThisGeneration = bird.fitness;
            bestBirdIndex = i;
        }
        if (bird.score > bestScoreThisGeneration) bestScoreThisGeneration = bird.score;
    }

    if (bestFitnessThisGeneration > bestFitnessEver) bestFitnessEver = bestFitnessThisGeneration;
    if (bestScoreThisGeneration > bestScoreEver) bestScoreEver = bestScoreThisGeneration;

    if (aliveCount == 0 || generationTick >= MAX_GENERATION_TICKS) {
        evolvePopulation();
    }
}

void processLine(const String &line) {
    if (line == "FLAP") {
        evolvePopulation();
        return;
    }

    if (line.startsWith("DIFF:")) {
        int parsed = parseDifficultyValue(line.substring(5));
        int oldDifficulty = difficulty;
        applyDifficulty(parsed);
        if (difficulty != oldDifficulty) {
            startGeneration();
        }
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

void drawPipe(const Pipe &pipe, int yOffset, int height) {
    int x = (int)pipe.x;
    int scaledGap = max(5, obstacleGap * height / SCREEN_H);
    int gapY = yOffset + pipe.gapY * height / SCREEN_H;
    int gapTop = gapY - scaledGap / 2;
    int gapBottom = gapY + scaledGap / 2;

    if (x >= SCREEN_W || x + PIPE_WIDTH < 0) return;

    oled.fillRect(x, yOffset, PIPE_WIDTH, max(0, gapTop - yOffset), SSD1306_WHITE);
    oled.fillRect(x, gapBottom, PIPE_WIDTH, max(0, yOffset + height - gapBottom), SSD1306_WHITE);
}

void renderTraining() {
    if (!oledOk) return;

    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.setTextSize(1);

    oled.setCursor(0, 0);
    oled.printf("Gen:%u Alive:%u", generation, aliveCount);
    oled.setCursor(0, 10);
    oled.printf("Best:%u All:%u", bestScoreThisGeneration, bestScoreEver);
    oled.setCursor(0, 20);
    oled.printf("Fit:%u D:%X", (uint16_t)min(bestFitnessEver, 65535.0f), difficulty);

    int fieldTop = 32;
    int fieldHeight = 31;
    oled.drawFastHLine(0, fieldTop - 1, SCREEN_W, SSD1306_WHITE);

    for (int i = 0; i < PIPE_COUNT; ++i) {
        drawPipe(pipes[i], fieldTop, fieldHeight);
    }

    for (int i = 0; i < POP_SIZE; ++i) {
        if (!birds[i].alive) continue;
        int y = fieldTop + (int)(birds[i].y * (float)fieldHeight / (float)SCREEN_H);
        oled.drawPixel(BIRD_X + (i % 4), clampInt(y, fieldTop, fieldTop + fieldHeight - 1), SSD1306_WHITE);
    }

    if (birds[bestBirdIndex].alive) {
        int y = fieldTop + (int)(birds[bestBirdIndex].y * (float)fieldHeight / (float)SCREEN_H);
        oled.fillRect(BIRD_X - 2, clampInt(y - 1, fieldTop, fieldTop + fieldHeight - 2), 3, 3, SSD1306_WHITE);
    }

    oled.display();
}

void setup() {
    Serial.begin(115200);
    delay(200);

    randomSeed((uint32_t)esp_random());

    Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
    oledOk = oled.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR);

    FpgaSerial.begin(FPGA_BAUD, SERIAL_8N1, PIN_FPGA_RX, PIN_FPGA_TX);

    applyDifficulty(0);
    initPopulation();
}

void loop() {
    uint32_t now = millis();

    pollFpgaUart();

    if (now - tickTimer >= GAME_TICK_MS) {
        tickTimer = now;
        updatePopulation();
    }

    if (now - renderTimer >= RENDER_MS) {
        renderTimer = now;
        renderTraining();
    }
}
