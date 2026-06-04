// ============================================================
// CrashTech VLSI-2026 -- Challenge 2: Accelerometer 3D Cube (FPGA side)
// ============================================================
// ADXL345 G-sensor via I2C: Z-axis read every 50 ms, displayed on HEX5-HEX0
//   HEX5       = '-' when negative, blank when positive
//   HEX4-HEX0  = signed decimal magnitude
// UART TX: rolling digit 0-9 sent to ESP32 every second
// UART RX: received digit reflected on LEDs
//
// ARDUINO_IO[0] = UART RX (from ESP32 GPIO16)
// ARDUINO_IO[1] = UART TX (to ESP32 GPIO17)
// GSENSOR_SDI / GSENSOR_SCLK = I2C SDA / SCL
// 9600 baud 8N1, 50 MHz clock
// ============================================================

module ch2_3d_cube_top (
    input           MAX10_CLK1_50,
    input   [9:0]   SW,
    input   [1:0]   KEY,
    output  [9:0]   LEDR,
    output  [7:0]   HEX0, HEX1, HEX2, HEX3, HEX4, HEX5,
    inout   [15:0]  ARDUINO_IO,
    // GSENSOR pins (I2C/SPI accelerometer interface)
    inout           GSENSOR_SDI,       // PIN_V11 - I2C SDA / SPI data
    inout           GSENSOR_SDO,       // PIN_V12 - I2C addr select / SPI data out
    output          GSENSOR_CS_n,      // PIN_AB16 - I2C/SPI mode select
    inout           GSENSOR_SCLK,      // PIN_AB15 - I2C SCL / SPI clock
    input           GSENSOR_INT1,      // PIN_Y14 - Interrupt 1
    input           GSENSOR_INT2       // PIN_Y13 - Interrupt 2
);

    wire clk   = MAX10_CLK1_50;
    wire rst_n = KEY[0];

    // ---- Arduino Header IO ----
    wire uart_tx_out;
    wire uart_rx_in = ARDUINO_IO[0];
    assign ARDUINO_IO[0]    = 1'bz;       // explicit input (tri-state driver)
    assign ARDUINO_IO[1]    = uart_tx_out;
    assign ARDUINO_IO[15:2] = 14'bz;

    // ---- GSENSOR I2C master ----
    // CS_n=1 => I2C mode; SDO=1 => address 0x1D (write 0x3A, read 0x3B)
    assign GSENSOR_CS_n = 1'b1;
    assign GSENSOR_SDO  = 1'b1;

    reg        i2c_start;
    reg [7:0]  i2c_addr;
    reg [23:0] i2c_data_send;
    reg [1:0]  i2c_num_bytes_send;
    reg [1:0]  i2c_num_bytes_receive;
    wire [23:0] i2c_data_received;

    master_I2C #(
        .BYTES_SEND_LOG    (2),
        .BYTES_RECEIVE_LOG (2)
    ) u_i2c_master (
        .rst               (rst_n),
        .clk               (clk),
        .start             (i2c_start),
        .addr_target       (i2c_addr),
        .data_send         (i2c_data_send),
        .num_bytes_send    (i2c_num_bytes_send),
        .num_bytes_receive (i2c_num_bytes_receive),
        .SDA_bidir         (GSENSOR_SDI),
        .SCL_bidir         (GSENSOR_SCLK),
        .data_received     (i2c_data_received)
    );

    // ================================================================
    //  ADXL345 measurement FSM  (50 MHz clock, I2C at 100 kHz)
    //
    //  master_I2C data_send byte ordering (bits_send = num_bytes_send*8):
    //    first byte sent = data_send[bits_send-1 -: 8], i.e. the LSB-aligned
    //    byte. For 2 bytes: data_send[15:8] = 1st, data_send[7:0] = 2nd.
    //    For 1 byte:        data_send[7:0]  = only byte.
    //
    //  data_received byte ordering after 2-byte read:
    //    data_received[15:8] = 1st byte received = DATAZ0 (Z LSB)
    //    data_received[7:0]  = 2nd byte received = DATAZ1 (Z MSB)
    //
    //  Sequence:
    //   1. Wait 10 ms after reset (ADXL345 power-up)
    //   2. Write POWER_CTL (0x2D) = 0x08  -> wake from standby
    //   3. Every 50 ms:
    //      a. Write register pointer 0x36  (DATAZ0)
    //      b. Read 2 bytes  -> latch Z-axis
    // ================================================================

    // Guard times: one full 9-byte I2C transaction ≈ 9 500 clocks; 2 ms >> that
    localparam [24:0] WAIT_2MS  = 25'd100_000;
    localparam [24:0] WAIT_10MS = 25'd500_000;
    localparam [24:0] WAIT_50MS = 25'd2_500_000;

    localparam [2:0]
        ST_INIT_WAIT  = 3'd0,
        ST_INIT_WR    = 3'd1,
        ST_INIT_WDONE = 3'd2,
        ST_MEAS_WAIT  = 3'd3,
        ST_MEAS_WREG  = 3'd4,
        ST_MEAS_WDONE = 3'd5,
        ST_MEAS_RD    = 3'd6,
        ST_MEAS_RDONE = 3'd7;

    reg [2:0]  meas_state;
    reg [24:0] meas_cnt;
    reg [15:0] z_raw;   // latched Z-axis value, two's complement

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            meas_state            <= ST_INIT_WAIT;
            meas_cnt              <= 0;
            i2c_start             <= 0;
            i2c_addr              <= 8'h3A;
            i2c_data_send         <= 24'h0;
            i2c_num_bytes_send    <= 2'd0;
            i2c_num_bytes_receive <= 2'd0;
            z_raw                 <= 16'h0;
        end else begin
            i2c_start <= 0;   // pulse for one clock only

            case (meas_state)

                ST_INIT_WAIT: begin
                    if (meas_cnt == WAIT_10MS - 1) begin
                        meas_cnt   <= 0;
                        meas_state <= ST_INIT_WR;
                    end else
                        meas_cnt <= meas_cnt + 1;
                end

                ST_INIT_WR: begin
                    // Write POWER_CTL (reg 0x2D) = 0x08
                    // 2 bytes: data_send[15:8]=0x2D, data_send[7:0]=0x08
                    i2c_addr              <= 8'h3A;
                    i2c_data_send         <= {8'h00, 8'h2D, 8'h08};
                    i2c_num_bytes_send    <= 2'd2;
                    i2c_num_bytes_receive <= 2'd0;
                    i2c_start  <= 1;
                    meas_cnt   <= 0;
                    meas_state <= ST_INIT_WDONE;
                end

                ST_INIT_WDONE: begin
                    if (meas_cnt == WAIT_2MS - 1) begin
                        meas_cnt   <= 0;
                        meas_state <= ST_MEAS_WAIT;
                    end else
                        meas_cnt <= meas_cnt + 1;
                end

                ST_MEAS_WAIT: begin
                    if (meas_cnt == WAIT_50MS - 1) begin
                        meas_cnt   <= 0;
                        meas_state <= ST_MEAS_WREG;
                    end else
                        meas_cnt <= meas_cnt + 1;
                end

                ST_MEAS_WREG: begin
                    // Write register pointer = 0x36 (DATAZ0)
                    // 1 byte: data_send[7:0] = 0x36
                    i2c_addr              <= 8'h3A;
                    i2c_data_send         <= {16'h00, 8'h36};
                    i2c_num_bytes_send    <= 2'd1;
                    i2c_num_bytes_receive <= 2'd0;
                    i2c_start  <= 1;
                    meas_cnt   <= 0;
                    meas_state <= ST_MEAS_WDONE;
                end

                ST_MEAS_WDONE: begin
                    if (meas_cnt == WAIT_2MS - 1) begin
                        meas_cnt   <= 0;
                        meas_state <= ST_MEAS_RD;
                    end else
                        meas_cnt <= meas_cnt + 1;
                end

                ST_MEAS_RD: begin
                    // Read 2 bytes: DATAZ0 then DATAZ1
                    i2c_addr              <= 8'h3B;
                    i2c_num_bytes_send    <= 2'd0;
                    i2c_num_bytes_receive <= 2'd2;
                    i2c_start  <= 1;
                    meas_cnt   <= 0;
                    meas_state <= ST_MEAS_RDONE;
                end

                ST_MEAS_RDONE: begin
                    if (meas_cnt == WAIT_2MS - 1) begin
                        // data_received[15:8]=DATAZ0 (LSB byte)
                        // data_received[7:0] =DATAZ1 (MSB byte)
                        z_raw      <= {i2c_data_received[7:0], i2c_data_received[15:8]};
                        meas_cnt   <= 0;
                        meas_state <= ST_MEAS_WAIT;
                    end else
                        meas_cnt <= meas_cnt + 1;
                end

                default: meas_state <= ST_INIT_WAIT;
            endcase
        end
    end

    // ================================================================
    //  HEX display: Z-axis as signed decimal
    //  ADXL345 default ±2 g range → 10-bit effective (-512 to +511)
    //  HEX5 = sign ('-' or blank), HEX4-HEX0 = decimal digits
    // ================================================================
    wire        z_neg = z_raw[15];
    wire [15:0] z_abs = z_neg ? (~z_raw + 16'd1) : z_raw;

    wire [3:0] d0 =  z_abs % 10;
    wire [3:0] d1 = (z_abs /    10) % 10;
    wire [3:0] d2 = (z_abs /   100) % 10;
    wire [3:0] d3 = (z_abs /  1000) % 10;
    wire [3:0] d4 = (z_abs / 10000) % 10;

    function automatic [7:0] seg7;
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

    localparam SEG_BLANK = 8'b1111_1111;
    localparam SEG_MINUS = 8'b1011_1111;   // segment g only

    assign HEX0 = seg7(d0);
    assign HEX1 = seg7(d1);
    assign HEX2 = seg7(d2);
    assign HEX3 = seg7(d3);
    assign HEX4 = (z_abs >= 10000) ? seg7(d4) : SEG_BLANK;
    assign HEX5 = z_neg ? SEG_MINUS : SEG_BLANK;

    localparam CLKS_PER_BIT = 13'd5208;  // 50_000_000 / 9600

    // ================================================================
    //  1-second tick + TX digit counter
    // ================================================================
    localparam SEC_TICKS = 26'd50_000_000;
    reg [25:0] tick_cnt;
    reg        send_trigger;
    reg [3:0]  tx_digit;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            tick_cnt     <= 0;
            send_trigger <= 0;
            tx_digit     <= 0;
        end else begin
            send_trigger <= 0;
            if (tick_cnt == SEC_TICKS - 1) begin
                tick_cnt     <= 0;
                send_trigger <= 1;
                tx_digit <= (tx_digit == 4'd9) ? 4'd0 : tx_digit + 1;
            end else
                tick_cnt <= tick_cnt + 1;
        end
    end

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

endmodule
