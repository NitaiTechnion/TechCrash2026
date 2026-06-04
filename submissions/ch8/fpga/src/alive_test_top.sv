// ============================================================
// CrashTech VLSI-2026 -- Alive Test (FPGA side)
// ============================================================
// Bidirectional UART demo:
//   HEX0  = rolling digit 0-9 (count UP, sent to ESP32 via TX)
//   HEX1  = digit received from ESP32 (countdown 9-0)
//   HEX2-5 = 4-digit BCD seconds counter
//   LEDR  = LED sweep XOR switches
//
// ARDUINO_IO[0] = UART RX (from ESP32 GPIO16)  Arduino header IO0
// ARDUINO_IO[1] = UART TX (to ESP32 GPIO17)    Arduino header IO1
// Arduino header GND pin
// 9600 baud 8N1, 50 MHz clock
// ============================================================

module alive_test_top (
    input           MAX10_CLK1_50,
    input   [9:0]   SW,
    input   [1:0]   KEY,
    output  [9:0]   LEDR,
    output  [7:0]   HEX0, HEX1, HEX2, HEX3, HEX4, HEX5,
    inout   [15:0]  ARDUINO_IO
);

    wire clk   = MAX10_CLK1_50;
    wire rst_n = KEY[0];

    // ---- Arduino Header IO ----
    wire uart_tx_out;
    wire uart_rx_in = ARDUINO_IO[0];
    assign ARDUINO_IO[0]    = 1'bz;       // explicit input (tri-state driver)
    assign ARDUINO_IO[1]    = uart_tx_out;
    assign ARDUINO_IO[15:2] = 14'bz;

    localparam CLKS_PER_BIT = 13'd5208;  // 50_000_000 / 9600

    // ================================================================
    //  1-second tick + counters
    // ================================================================
    localparam SEC_TICKS = 26'd50_000_000;
    reg [25:0] tick_cnt;
    reg        send_trigger;
    reg [3:0]  tx_digit;       // 0-9 count up, shown on HEX0
    reg [3:0]  bcd [0:3];     // seconds counter for HEX2-5

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            tick_cnt     <= 0;
            send_trigger <= 0;
            tx_digit     <= 0;
            bcd[0] <= 0; bcd[1] <= 0; bcd[2] <= 0; bcd[3] <= 0;
        end else begin
            send_trigger <= 0;
            if (tick_cnt == SEC_TICKS - 1) begin
                tick_cnt     <= 0;
                send_trigger <= 1;
                tx_digit <= (tx_digit == 4'd9) ? 4'd0 : tx_digit + 1;
                if (bcd[0] < 9) bcd[0] <= bcd[0] + 1;
                else begin bcd[0] <= 0;
                    if (bcd[1] < 9) bcd[1] <= bcd[1] + 1;
                    else begin bcd[1] <= 0;
                        if (bcd[2] < 9) bcd[2] <= bcd[2] + 1;
                        else begin bcd[2] <= 0;
                            bcd[3] <= (bcd[3] < 9) ? bcd[3] + 1 : 0;
                        end
                    end
                end
            end else
                tick_cnt <= tick_cnt + 1;
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

    assign HEX0 = seg7(tx_digit);    // FPGA rolling UP digit (TX to ESP32)
    assign HEX1 = seg7(rx_digit);    // ESP32 countdown digit (RX from ESP32)
    assign HEX2 = 8'hFF;
    assign HEX3 = 8'hFF;
    assign HEX4 = 8'hFF;
    assign HEX5 = 8'hFF;

    // ================================================================
    //  LED sweep
    // ================================================================
    reg [25:0] led_cnt;
    reg [3:0]  led_pos;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin led_cnt <= 0; led_pos <= 0; end
        else begin
            led_cnt <= led_cnt + 1;
            if (led_cnt == 0)
                led_pos <= (led_pos == 4'd9) ? 4'd0 : led_pos + 1;
        end
    end
    // Debug LEDs:
    // LEDR[9] = raw ARDUINO_IO[0] level (should toggle when ESP32 sends)
    // LEDR[8] = toggles on each valid rx_done pulse
    // LEDR[7:4] = rx_digit value
    // LEDR[3:0] = LED sweep
    reg rx_toggle;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) rx_toggle <= 0;
        else if (rx_done) rx_toggle <= ~rx_toggle;
    end
    assign LEDR[9]   = rx_bit;
    assign LEDR[8]   = rx_toggle;
    assign LEDR[7:4] = rx_digit;
    assign LEDR[3:0] = (4'd1 << led_pos[1:0]) ^ SW[3:0];

    // ================================================================
    //  UART TX engine
    // ================================================================
    reg [12:0] tx_clk_cnt;
    reg [3:0]  tx_bit_idx;
    reg [9:0]  tx_shift;
    reg        tx_busy;
    reg        tx_out_reg;
    reg        tx_start;
    reg [7:0]  tx_data;

    assign uart_tx_out = tx_out_reg;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            tx_busy <= 0; tx_out_reg <= 1; tx_clk_cnt <= 0; tx_bit_idx <= 0;
        end else if (!tx_busy && tx_start) begin
            tx_shift   <= {1'b1, tx_data, 1'b0};
            tx_busy    <= 1;
            tx_bit_idx <= 0;
            tx_clk_cnt <= 0;
            tx_out_reg <= 0;
        end else if (tx_busy) begin
            if (tx_clk_cnt == CLKS_PER_BIT - 1) begin
                tx_clk_cnt <= 0;
                tx_bit_idx <= tx_bit_idx + 1;
                if (tx_bit_idx < 9)
                    tx_out_reg <= tx_shift[tx_bit_idx + 1];
                else begin
                    tx_busy <= 0; tx_out_reg <= 1;
                end
            end else
                tx_clk_cnt <= tx_clk_cnt + 1;
        end
    end

    // ================================================================
    //  TX dispatch: digit + newline every second
    // ================================================================
    reg [1:0] txd_state;
    localparam TXD_IDLE = 2'd0, TXD_DIGIT = 2'd1, TXD_NL = 2'd2;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            txd_state <= TXD_IDLE; tx_start <= 0; tx_data <= 0;
        end else begin
            tx_start <= 0;
            case (txd_state)
                TXD_IDLE:  if (send_trigger) txd_state <= TXD_DIGIT;
                TXD_DIGIT: if (!tx_busy && !tx_start) begin
                    tx_data <= tx_digit + 8'h30;
                    tx_start <= 1;
                    txd_state <= TXD_NL;
                end
                TXD_NL: if (!tx_busy && !tx_start) begin
                    tx_data <= 8'h0A;
                    tx_start <= 1;
                    txd_state <= TXD_IDLE;
                end
                default: txd_state <= TXD_IDLE;
            endcase
        end
    end

    // ================================================================
    //  UART RX engine
    // ================================================================
    reg rx_s1, rx_s2;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin rx_s1 <= 1; rx_s2 <= 1; end
        else begin rx_s1 <= uart_rx_in; rx_s2 <= rx_s1; end
    end
    wire rx_bit = rx_s2;

    reg [1:0]  rx_state;
    reg [12:0] rx_clk_cnt;
    reg [2:0]  rx_bit_idx;
    reg [7:0]  rx_shift;
    reg        rx_done;
    reg [7:0]  rx_byte;

    localparam RX_IDLE = 2'd0, RX_START = 2'd1, RX_DATA = 2'd2, RX_STOP = 2'd3;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            rx_state <= RX_IDLE; rx_clk_cnt <= 0; rx_bit_idx <= 0;
            rx_shift <= 0; rx_byte <= 0; rx_done <= 0;
        end else begin
            rx_done <= 0;
            case (rx_state)
                RX_IDLE: if (rx_bit == 0) begin
                    rx_clk_cnt <= 0;
                    rx_state   <= RX_START;
                end
                RX_START: begin
                    if (rx_clk_cnt == (CLKS_PER_BIT-1)/2) begin
                        if (rx_bit == 0) begin
                            rx_clk_cnt <= 0;
                            rx_bit_idx <= 0;
                            rx_state   <= RX_DATA;
                        end else
                            rx_state <= RX_IDLE;
                    end else
                        rx_clk_cnt <= rx_clk_cnt + 1;
                end
                RX_DATA: begin
                    if (rx_clk_cnt == CLKS_PER_BIT - 1) begin
                        rx_clk_cnt <= 0;
                        rx_shift[rx_bit_idx] <= rx_bit;
                        if (rx_bit_idx == 7)
                            rx_state <= RX_STOP;
                        else
                            rx_bit_idx <= rx_bit_idx + 1;
                    end else
                        rx_clk_cnt <= rx_clk_cnt + 1;
                end
                RX_STOP: begin
                    if (rx_clk_cnt == CLKS_PER_BIT - 1) begin
                        rx_byte  <= rx_shift;
                        rx_done  <= 1;
                        rx_state <= RX_IDLE;
                    end else
                        rx_clk_cnt <= rx_clk_cnt + 1;
                end
            endcase
        end
    end

    // ================================================================
    //  RX digit latch: ASCII '0'-'9' -> 0-9
    // ================================================================
    reg [3:0] rx_digit;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            rx_digit <= 4'hF;
        else if (rx_done && rx_byte >= 8'h30 && rx_byte <= 8'h39)
            rx_digit <= rx_byte[3:0];
    end

    // ================================================================
    //  Debug counters (shown on HEX2-HEX4)
    // ================================================================
    reg [3:0] dbg_start_cnt;  // counts start-bit detections
    reg [3:0] dbg_done_cnt;   // counts completed rx_done pulses
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            dbg_start_cnt <= 0;
            dbg_done_cnt  <= 0;
        end else begin
            if (rx_state == RX_IDLE && rx_bit == 0)
                dbg_start_cnt <= (dbg_start_cnt == 9) ? 4'd0 : dbg_start_cnt + 1;
            if (rx_done)
                dbg_done_cnt <= (dbg_done_cnt == 9) ? 4'd0 : dbg_done_cnt + 1;
        end
    end

endmodule
