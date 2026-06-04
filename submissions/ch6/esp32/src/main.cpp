// ============================================================
// CrashTech VLSI-2026 — Challenge 6: Frequency Detector (ESP32 side)
// ============================================================
// Communicates with FPGA frequency detector via UART.
// Sends commands to configure measurement and receives frequency data.
// ============================================================

#include <Arduino.h>

// UART to FPGA
HardwareSerial FpgaSerial(2);

// Buffer for incoming UART data
String fpgaBuffer = "";

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.println("========================================");
    Serial.println(" CrashTech VLSI-2026 — Challenge 6");
    Serial.println(" Frequency Detector (ESP32 side)");
    Serial.println("========================================");

    // Initialize FPGA UART (RX=GPIO16, TX=GPIO17, 9600 baud)
    FpgaSerial.begin(9600, SERIAL_8N1, 16, 17);
    
    Serial.println("Waiting for FPGA frequency data...");
}

void loop() {
    // Check for incoming FPGA data
    while (FpgaSerial.available()) {
        char c = FpgaSerial.read();
        if (c == '\n') {
            // Process complete line from FPGA
            Serial.print("FPGA: ");
            Serial.println(fpgaBuffer);
            fpgaBuffer = "";
        } else {
            fpgaBuffer += c;
        }
    }

    // Check for commands from serial console
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        
        if (cmd == "START") {
            FpgaSerial.println("START");
            Serial.println("Command sent: START");
        } else if (cmd == "STOP") {
            FpgaSerial.println("STOP");
            Serial.println("Command sent: STOP");
        } else if (cmd == "READ") {
            FpgaSerial.println("READ");
            Serial.println("Command sent: READ");
        } else if (cmd != "") {
            Serial.println("Unknown command: " + cmd);
            Serial.println("Available: START, STOP, READ");
        }
    }

    delay(10);
}
