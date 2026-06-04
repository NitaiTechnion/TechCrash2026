// ============================================================
// CrashTech VLSI-2026 -- Challenge 6: Frequency Detector (FPGA side)
// ============================================================
// Frequency measurement system:
//   - Counts rising edges on an input signal
//   - Calculates frequency over a measurement window
//   - Displays result on HEX0–HEX5 7-segment displays
//   - Communicates with ESP32 via UART (9600 baud)
//
// ARDUINO_IO[0] = UART RX (from ESP32 GPIO16)  Arduino header IO0
// ARDUINO_IO[1] = UART TX (to ESP32 GPIO17)    Arduino header IO1
// ARDUINO_IO[2] = Frequency input signal       Arduino header IO2 (configurable)
// Arduino header GND pin
// 9600 baud 8N1, 50 MHz clock
// ============================================================

module ch6_frequency_detector (
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
    wire freq_input = ARDUINO_IO[2];        // Frequency input signal
    
    assign ARDUINO_IO[0]    = 1'bz;         // explicit input (tri-state driver)
    assign ARDUINO_IO[1]    = uart_tx_out;
    assign ARDUINO_IO[15:2] = 14'bz;

    localparam CLKS_PER_BIT = 13'd5208;     // 50_000_000 / 9600

    // ================================================================
    //  Frequency Counter
    // ================================================================
    localparam MEASURE_WINDOW = 26'd50_000_000;  // 1 second at 50 MHz
    
    reg [25:0] window_cnt;
    reg [19:0] edge_count;                  // count up to ~1 MHz
    reg [19:0] freq_result;                 // latched result
    reg        freq_input_r1, freq_input_r2;
    reg        measurement_active;
    wire       rising_edge = !freq_input_r2 && freq_input_r1;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            window_cnt = 0;
            edge_count = 0;
            freq_result = 0;
            freq_input_r1 = 0;
            freq_input_r2 = 0;
            measurement_active = 1;
        end else begin
            // Synchronize input
            freq_input_r2 <= freq_input_r1;
            freq_input_r1 <= freq_input;
            
            // Count edges during measurement window
            if (measurement_active) begin
                if (rising_edge) begin
                    edge_count <= edge_count + 1;
                end
                
                if (window_cnt == MEASURE_WINDOW - 1) begin
                    window_cnt <= 0;
                    freq_result <= edge_count;
                    edge_count <= 0;
                end else begin
                    window_cnt <= window_cnt + 1;
                end
            end
        end
    end

    // ================================================================
    //  7-segment decoder
    // ================================================================
    function [7:0] seg7;
        input [3:0] d;
        case (d)
            4'd0: seg7 = 8'b1100_0000;
            4'd1: seg7 = 8'b1111_1001;
            4'd2: seg7 = 8'b1010_0100;
            4'd3: seg7 = 8'b1011_0000;
            4'd4: seg7 = 8'b1001_1001;
            4'd5: seg7 = 8'b1001_0010;
            4'd6: seg7 = 8'b1000_0010;
            4'd7: seg7 = 8'b1111_1000;
            4'd8: seg7 = 8'b1000_0000;
            4'd9: seg7 = 8'b1001_0000;
            default: seg7 = 8'b1111_1111;
        endcase
    endfunction

    // Extract BCD digits from frequency result
    wire [3:0] digit0 = freq_result % 10;
    wire [3:0] digit1 = (freq_result / 10) % 10;
    wire [3:0] digit2 = (freq_result / 100) % 10;
    wire [3:0] digit3 = (freq_result / 1000) % 10;
    wire [3:0] digit4 = (freq_result / 10000) % 10;
    wire [3:0] digit5 = (freq_result / 100000) % 10;

    assign HEX0 = seg7(digit0);
    assign HEX1 = seg7(digit1);
    assign HEX2 = seg7(digit2);
    assign HEX3 = seg7(digit3);
    assign HEX4 = seg7(digit4);
    assign HEX5 = seg7(digit5);

    // ================================================================
    //  Status LEDs
    // ================================================================
    assign LEDR[0] = measurement_active;
    assign LEDR[9:1] = 9'b0;

    // ================================================================
    //  UART RX (for future commands)
    // ================================================================
    // Placeholder for UART RX logic
    // TODO: Implement command parsing for measurement control

endmodule
