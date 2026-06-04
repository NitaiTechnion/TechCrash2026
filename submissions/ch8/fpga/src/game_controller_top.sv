// ============================================================
// CrashTech VLSI-2026 -- Game Controller (FPGA side)
// ============================================================
// Continuously transmits SW/KEY state over UART at full channel speed.
//   byte 0: {2'b01, SW[4:0], KEY[0]}
//   byte 1: {2'b10, SW[9:5], KEY[1]}
// Alternates between the two bytes with a 20 ms gap between each.
//
// ARDUINO_IO[1] = UART TX (to ESP32 GPIO17)    Arduino header IO1
// 9600 baud 8N1, 50 MHz clock
// ============================================================

module game_controller_top (
    input           MAX10_CLK1_50,
    input   [9:0]   SW,
    input   [1:0]   KEY,
    output  [9:0]   LEDR,
    output  [7:0]   HEX0, HEX1, HEX2, HEX3, HEX4, HEX5,
    inout   [15:0]  ARDUINO_IO
);

    wire clk = MAX10_CLK1_50;

    // ---- Arduino Header IO ----
    wire uart_tx_out;
    assign ARDUINO_IO[0]    = 1'bz;
    assign ARDUINO_IO[1]    = uart_tx_out;
    assign ARDUINO_IO[15:2] = 14'bz;

    // ---- Unused outputs ----
    assign LEDR  = 10'b0;
    assign HEX0  = 8'hFF;
    assign HEX1  = 8'hFF;
    assign HEX2  = 8'hFF;
    assign HEX3  = 8'hFF;
    assign HEX4  = 8'hFF;
    assign HEX5  = 8'hFF;

    localparam CLKS_PER_BIT = 13'd5208;  // 50_000_000 / 9600

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

    always @(posedge clk) begin
        if (!tx_busy && tx_start) begin
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
    //  TX dispatch: alternate byte 0 and byte 1 with 20 ms gap
    // ================================================================
    localparam GAP_CLKS = 20'd1_000_000;  // 20 ms @ 50 MHz

    reg        byte_sel;   // 0 = send byte 0, 1 = send byte 1
    reg        waiting;    // 1 while gap counter is running
    reg [19:0] gap_cnt;

    always @(posedge clk) begin
        tx_start <= 0;
        if (waiting) begin
            if (gap_cnt == GAP_CLKS - 1)
                waiting <= 0;
            else
                gap_cnt <= gap_cnt + 1;
        end else if (!tx_busy && !tx_start) begin
            if (byte_sel == 1'b0)
                tx_data <= {2'b01, SW[4:0], KEY[0]};
            else
                tx_data <= {2'b10, SW[9:5], KEY[1]};
            tx_start  <= 1;
            byte_sel  <= ~byte_sel;
            waiting   <= 1;
            gap_cnt   <= 0;
        end
    end

endmodule
