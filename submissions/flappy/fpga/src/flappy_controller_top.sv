module flappy_controller_top (
    input           MAX10_CLK1_50,
    input   [9:0]   SW,
    input   [1:0]   KEY,
    output  [9:0]   LEDR,
    output  [7:0]   HEX0,
    output  [7:0]   HEX1,
    output  [7:0]   HEX2,
    output  [7:0]   HEX3,
    output  [7:0]   HEX4,
    output  [7:0]   HEX5,
    inout   [15:0]  ARDUINO_IO,
    inout           ARDUINO_RESET_N
);

    localparam CLK_FREQ_HZ      = 50_000_000;
    localparam BAUD             = 9600;
    localparam [22:0] DEBOUNCE_TICKS = 23'd2_500_000;  // 50 ms
    localparam [22:0] DIFF_TX_TICKS  = 23'd5_000_000;  // 100 ms

    localparam [7:0] C_FLAP_0 = "F";
    localparam [7:0] C_FLAP_1 = "L";
    localparam [7:0] C_FLAP_2 = "A";
    localparam [7:0] C_FLAP_3 = "P";
    localparam [7:0] C_DIFF_0 = "D";
    localparam [7:0] C_DIFF_1 = "I";
    localparam [7:0] C_DIFF_2 = "F";
    localparam [7:0] C_DIFF_3 = "F";
    localparam [7:0] C_COLON  = ":";
    localparam [7:0] C_NL     = 8'h0A;

    wire clk = MAX10_CLK1_50;
    wire rst_n = KEY[1];
    wire [3:0] difficulty = SW[3:0];

    reg key0_meta;
    reg key0_sync;
    reg key0_prev;

    reg [22:0] debounce_cnt;
    reg [22:0] diff_tick_cnt;
    reg [21:0] flap_led_cnt;

    reg flap_event;
    reg diff_tick;

    reg [2:0]  tx_state;
    reg [2:0]  tx_idx;
    reg [7:0]  tx_data;
    reg        tx_start;
    reg        tx_wait_busy;
    wire       tx_busy;
    wire       tx_out;

    localparam S_IDLE = 3'd0;
    localparam S_FLAP = 3'd1;
    localparam S_DIFF = 3'd2;

    localparam [2:0] FLAP_LEN = 3'd5;
    localparam [2:0] DIFF_LEN = 3'd7;

    wire [7:0] hex0_seg;

    uart_tx #(
        .CLK_FREQ(CLK_FREQ_HZ),
        .BAUD(BAUD)
    ) u_tx (
        .clk(clk),
        .rst_n(rst_n),
        .tx_start(tx_start),
        .tx_data(tx_data),
        .tx_busy(tx_busy),
        .tx_out(tx_out)
    );

    seven_segment_hex u_hex0 (
        .hex(difficulty),
        .blank(1'b0),
        .seg(hex0_seg)
    );

    assign HEX0 = hex0_seg;
    assign HEX1 = 8'hFF;
    assign HEX2 = 8'hFF;
    assign HEX3 = 8'hFF;
    assign HEX4 = 8'hFF;
    assign HEX5 = 8'hFF;

    assign LEDR = {4'b0, (flap_led_cnt != 22'd0), ~key0_sync, difficulty};

    assign ARDUINO_IO[0]    = 1'bz;
    assign ARDUINO_IO[1]    = tx_out;
    assign ARDUINO_IO[15:2] = {14{1'bz}};
    assign ARDUINO_RESET_N  = 1'bz;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            key0_meta     <= 1'b1;
            key0_sync     <= 1'b1;
            key0_prev     <= 1'b1;
            debounce_cnt  <= 23'd0;
            diff_tick_cnt <= 23'd0;
            flap_led_cnt  <= 22'd0;
            flap_event    <= 1'b0;
            diff_tick     <= 1'b0;
        end else begin
            key0_meta <= KEY[0];
            key0_sync <= key0_meta;

            flap_event <= 1'b0;
            diff_tick  <= 1'b0;

            if (key0_prev && !key0_sync) begin
                if (debounce_cnt == 23'd0) begin
                    flap_event   <= 1'b1;
                    debounce_cnt <= DEBOUNCE_TICKS - 23'd1;
                end
            end
            key0_prev <= key0_sync;

            if (debounce_cnt != 23'd0) begin
                debounce_cnt <= debounce_cnt - 23'd1;
            end

            if (flap_event) begin
                flap_led_cnt <= 22'd1_000_000;
            end else if (flap_led_cnt != 22'd0) begin
                flap_led_cnt <= flap_led_cnt - 22'd1;
            end

            if (diff_tick_cnt == DIFF_TX_TICKS - 23'd1) begin
                diff_tick_cnt <= 23'd0;
                diff_tick <= 1'b1;
            end else begin
                diff_tick_cnt <= diff_tick_cnt + 23'd1;
            end
        end
    end

    function [7:0] flap_byte;
        input [2:0] idx;
        begin
            case (idx)
                3'd0: flap_byte = C_FLAP_0;
                3'd1: flap_byte = C_FLAP_1;
                3'd2: flap_byte = C_FLAP_2;
                3'd3: flap_byte = C_FLAP_3;
                default: flap_byte = C_NL;
            endcase
        end
    endfunction

    function [7:0] diff_byte;
        input [2:0] idx;
        input [3:0] diff;
        begin
            case (idx)
                3'd0: diff_byte = C_DIFF_0;
                3'd1: diff_byte = C_DIFF_1;
                3'd2: diff_byte = C_DIFF_2;
                3'd3: diff_byte = C_DIFF_3;
                3'd4: diff_byte = C_COLON;
                3'd5: diff_byte = (diff < 4'd10) ? (8'h30 + {4'b0, diff}) : (8'h41 + {4'b0, (diff - 4'd10)});
                default: diff_byte = C_NL;
            endcase
        end
    endfunction

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            tx_state <= S_IDLE;
            tx_idx   <= 3'd0;
            tx_data  <= 8'h00;
            tx_start <= 1'b0;
            tx_wait_busy <= 1'b0;
        end else begin
            tx_start <= 1'b0;

            case (tx_state)
                S_IDLE: begin
                    tx_idx <= 3'd0;
                    tx_wait_busy <= 1'b0;
                    if (flap_event) begin
                        tx_state <= S_FLAP;
                    end else if (diff_tick) begin
                        tx_state <= S_DIFF;
                    end
                end

                S_FLAP: begin
                    if (!tx_wait_busy) begin
                        if (!tx_busy) begin
                            tx_data  <= flap_byte(tx_idx);
                            tx_start <= 1'b1;
                            tx_wait_busy <= 1'b1;
                        end
                    end else if (tx_busy) begin
                        tx_wait_busy <= 1'b0;
                        if (tx_idx == FLAP_LEN - 3'd1) begin
                            tx_state <= S_IDLE;
                            tx_idx <= 3'd0;
                        end else begin
                            tx_idx <= tx_idx + 3'd1;
                        end
                    end
                end

                S_DIFF: begin
                    if (!tx_wait_busy) begin
                        if (!tx_busy) begin
                            tx_data  <= diff_byte(tx_idx, difficulty);
                            tx_start <= 1'b1;
                            tx_wait_busy <= 1'b1;
                        end
                    end else if (tx_busy) begin
                        tx_wait_busy <= 1'b0;
                        if (tx_idx == DIFF_LEN - 3'd1) begin
                            tx_state <= S_IDLE;
                            tx_idx <= 3'd0;
                        end else begin
                            tx_idx <= tx_idx + 3'd1;
                        end
                    end
                end

                default: tx_state <= S_IDLE;
            endcase
        end
    end

endmodule
