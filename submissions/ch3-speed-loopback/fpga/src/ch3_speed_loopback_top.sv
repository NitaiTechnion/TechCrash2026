// ============================================================
// CrashTech VLSI-2026 — CH3 Speed Loopback (FPGA side)
// ============================================================
// Receives bytes from ESP32 over UART and echoes them back.
//
// ARDUINO_IO[0] = UART RX (from ESP32 TX, GPIO16)
// ARDUINO_IO[1] = UART TX (to ESP32 RX, GPIO17)
// 9600 baud, 8N1, 50 MHz clock
// ============================================================

module ch3_speed_loopback_top (
    input           MAX10_CLK1_50,
    input   [9:0]   SW,
    input   [1:0]   KEY,
    output  [9:0]   LEDR,
    output  [7:0]   HEX0, HEX1, HEX2, HEX3, HEX4, HEX5,
    inout   [15:0]  ARDUINO_IO,
    inout           ARDUINO_RESET_N
);

    wire clk   = MAX10_CLK1_50;
    wire rst_n = KEY[0];

    // ---- Arduino Header IO ----
    wire uart_tx_out;
    wire uart_rx_in = ARDUINO_IO[0];

    assign ARDUINO_IO[1]     = uart_tx_out;
    assign ARDUINO_IO[15:2]  = 14'bz;
    assign ARDUINO_RESET_N   = 1'bz;

    // ---- Unused outputs ----
    assign LEDR  = 10'b0;
    assign HEX0  = 8'hFF;
    assign HEX1  = 8'hFF;
    assign HEX2  = 8'hFF;
    assign HEX3  = 8'hFF;
    assign HEX4  = 8'hFF;
    assign HEX5  = 8'hFF;

endmodule
