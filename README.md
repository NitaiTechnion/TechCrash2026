# CrashTech VLSI 2026

**Technion VLSI hackathon** — DE10-Lite FPGA + ESP32 controller kit.

---

## Prerequisites

| Tool | Version | Download |
|------|---------|----------|
| **VS Code** | Latest | https://code.visualstudio.com/download |
| **Quartus Prime Lite** | 17.1 | [Intel FPGA downloads](https://www.intel.com/content/www/us/en/software-kit/669444/intel-quartus-prime-lite-edition-design-software-version-17-1-for-windows.html) |
| **PlatformIO IDE** | Latest | VS Code extension — search "PlatformIO IDE" |
| **CP210x USB driver** | Latest | https://www.silabs.com/developer-tools/usb-to-uart-bridge-vcp-drivers |
| **USB-Blaster driver** | (bundled) | `C:\intelFPGA_lite\17.1\quartus\drivers\usb-blaster` |
| **Git** | Latest | https://git-scm.com/downloads |

### Quartus installation notes

- Select **MAX 10 device support** during install (required for DE10-Lite).
- Include **ModelSim-Intel FPGA Edition** for simulation.
- Default install path: `C:\intelFPGA_lite\17.1\`

### PlatformIO installation notes

- Install the **PlatformIO IDE** extension in VS Code.
- PlatformIO auto-installs its own Python, ESP32 toolchain, and esptool. You do NOT need to install these separately.
- First build downloads ~500 MB of platform packages (needs internet).

---

## Clone and Setup

```powershell
git clone https://github.com/avisalmon/TechCrash2026.git
cd TechCrash2026
```

Open the folder in VS Code. PlatformIO will detect `platformio.ini` files automatically.

### Python environment (optional, for helper scripts)

```powershell
python -m venv env
env\Scripts\activate
pip install -r requirements.txt   # if present
```

The local Python venv lives at `env/` (not `.venv/`).

---

## Hardware Wiring

### ESP32 ↔ FPGA UART (Arduino Header)

| ESP32 Pin | Direction | FPGA Pin | Function |
|-----------|-----------|----------|----------|
| GPIO 16 | → | ARDUINO_IO[0] | ESP32 TX → FPGA RX |
| GPIO 17 | ← | ARDUINO_IO[1] | FPGA TX → ESP32 RX |
| GND | — | GND (Arduino header) | Common ground |

**UART config:** 9600 baud, 8N1, 3.3V logic.

> **Default:** Use the **Arduino header** on the DE10-Lite for ESP32 ↔ FPGA UART.
> The JP1 40-pin GPIO header is only used by challenges that explicitly call for it
> (e.g. speed loopback, PC retro game).

### Arduino Header Pin Map (DE10-Lite)

| Signal | FPGA Pin |
|--------|----------|
| ARDUINO_IO[0] | PIN_AB5 |
| ARDUINO_IO[1] | PIN_AB6 |
| ARDUINO_IO[2] | PIN_AB7 |
| ARDUINO_IO[3] | PIN_AB8 |
| ARDUINO_IO[4] | PIN_AB9 |
| ARDUINO_IO[5] | PIN_Y10 |
| ARDUINO_IO[6] | PIN_AA11 |
| ARDUINO_IO[7] | PIN_AA12 |
| ARDUINO_IO[8] | PIN_AB17 |
| ARDUINO_IO[9] | PIN_AA17 |
| ARDUINO_IO[10] | PIN_AB19 |
| ARDUINO_IO[11] | PIN_AA19 |
| ARDUINO_IO[12] | PIN_Y19 |
| ARDUINO_IO[13] | PIN_AB20 |
| ARDUINO_IO[14] | PIN_AB21 |
| ARDUINO_IO[15] | PIN_AA20 |
| ARDUINO_RESET_N | PIN_F16 |

### Shared ESP32 Pin Config

All ESP32 projects use `projects/common/esp32/pin_config.h` for pin definitions. Include it in your `main.cpp`:

```cpp
#include "../../../../projects/common/esp32/pin_config.h"
```

---

## Build and Program

### FPGA (Quartus CLI)

```powershell
# Compile (from the project's fpga/ folder)
cd demos\alive_test\fpga
& "C:\intelFPGA_lite\17.1\quartus\bin64\quartus_sh.exe" --flow compile alive_test

# List programmers (should show "USB-Blaster [USB-0]")
& "C:\intelFPGA_lite\17.1\quartus\bin64\quartus_pgm.exe" --list

# Program the FPGA (volatile — lost on power-off)
& "C:\intelFPGA_lite\17.1\quartus\bin64\quartus_pgm.exe" -c "USB-Blaster [USB-0]" -m JTAG -o "P;output_files\alive_test.sof"
```

### ESP32 (PlatformIO CLI)

```powershell
# From the project's esp32/ folder
cd demos\alive_test\esp32

# Build
pio run

# Upload to board (auto-detects COM port)
pio run -t upload

# Serial monitor
pio device monitor
```

If `pio` is not on PATH, use the full path: `$env:USERPROFILE\.platformio\penv\Scripts\pio.exe`

---

## Project Structure

```
TechCrash2026/
├── index.html                    # Main website
├── style.css
├── README.md                     # You are here
├── projects/
│   └── common/
│       └── esp32/
│           └── pin_config.h      # Shared ESP32 pin definitions
├── demos/                        # Practice projects (open, pre-competition)
│   ├── alive_test/               # Full kit smoke test
│   │   ├── esp32/                # PlatformIO project
│   │   └── fpga/                 # Quartus project
│   ├── bad_apple_oled/           # ESP32 OLED animation demo
│   │   └── esp32/                # PlatformIO project
│   └── internet_clock/           # WiFi NTP clock on 7-segment
│       ├── esp32/
│       └── fpga/
├── challenges/                   # Competition challenges (gitignored)
├── images/
└── docs/
```

Each project has a parallel structure:
- `esp32/` — PlatformIO project with `platformio.ini` and `src/main.cpp`
- `fpga/` — Quartus project with `.qpf`, `.qsf`, and `src/*.sv`

---

## Alive Test — Quick Start

The alive test verifies your entire kit is working (LEDs, OLED, buttons, buzzer, analog input, FPGA UART echo).

1. **Program the FPGA:** compile and upload `demos/alive_test/fpga/`
2. **Flash the ESP32:** build and upload `demos/alive_test/esp32/`
3. **Wire:** ESP32 GPIO16 → ARDUINO_IO[0], ARDUINO_IO[1] → ESP32 GPIO17, GND ↔ GND
4. **Verify:** LEDs chase, OLED shows sensor data, buttons buzz, FPGA echoes UART

---

## Copilot Skills

This repo is built to be used with **GitHub Copilot Chat** in VS Code. Two layers
of context auto-load when you open the workspace:

1. **Project-wide rules** — `.github/copilot-instructions.md` loads into every
   Copilot chat in this workspace. It encodes pin conventions, toolchain paths,
   and the "Arduino header by default" rule.
2. **Topic skills** — `.copilot/skills/<name>/SKILL.md` files are reference
   guides Copilot can pull into context when relevant.

### Available skills

| Skill | Use when working on |
|-------|---------------------|
| `esp32-firmware` | PlatformIO, OLED, buzzer, ADC, UART, build/flash |
| `de10lite-board-and-build` | Quartus 17.1, pin assignments, FSM patterns, PLL, 7-seg, compile/program |
| `de10lite-vga-graphics` | VGA 640×480 controller, sprites, text screen, font ROM |
| `de10lite-addon-peripherals` | Onboard ADC for joystick/buttons/wheel, LCD via Arduino header |
| `max10-adc-fpga` | MAX 10 internal ADC, Qsys Modular ADC IP, voltage read FSM |
| `adxl345-spi` | Onboard ADXL345 accelerometer over SPI, register map, X/Y/Z read |
| `esp32-fpga-high-speed-link` | High-rate UART, dual channels, parallel bus, SPI as transport |
| `fpga-dsp-frequency-detection` | Sample buffering, zero-crossing, light FFT on MAX 10 |
| `fp8-e4m3-and-pll-tuning` | FP8 E4M3 format, adder pipelining, ALTPLL tuning, timing closure |
| `python-pc-serial-bridge` | pyserial, COM detection, framing, pygame input from ESP32 |

### How to use them

- **Open the folder as a workspace** in VS Code (not just a single file).
- **Enable** the GitHub Copilot and GitHub Copilot Chat extensions.
- In a Copilot Chat prompt, **mention the topic by name** to nudge retrieval:
  - "Using the `de10lite-board-and-build` skill, write a 7-segment hex decoder."
  - "Reference the `adxl345-spi` skill and add accelerometer reading to my top module."
- Or just ask naturally ("read the accelerometer X axis on the FPGA") — Copilot
  will usually find the right skill via the workspace index.

### Verify Copilot sees the skills

In Copilot Chat, ask: *"What skills are available in `.copilot/skills/`?"*
You should see the list above echoed back.

