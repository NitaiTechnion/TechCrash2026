// ============================================================================
// FP8 E4M3 Adder — SLOW REFERENCE IMPLEMENTATION
// ============================================================================
// This is the intentionally slow, multi-cycle, non-pipelined reference adder.
// Teams may modify this file and challenge_pll.v to improve end-to-end runtime.
//
// Format: FP8 E4M3 — 1 sign | 4 exponent | 3 mantissa
//   Bias = 7, no infinity (exp=15 is valid), NaN = 0x7F/0xFF
//
// Interface:
//   - start: pulse high for 1 clock to begin computation
//   - a, b: 8-bit FP8 inputs (must be stable while busy)
//   - result: 8-bit FP8 output (valid when done=1)
//   - done: pulses high for 1 clock when result is ready
//   - busy: high while computing
//
// Performance target: This reference takes 12+ clock cycles per addition.
// Teams can compete on both micro-architecture and DUT clock frequency.
// ============================================================================

module fp8_adder (
    input  wire       clk,
    input  wire       rst_n,
    input  wire       start,
    input  wire [7:0] a,
    input  wire [7:0] b,
    output wire [7:0] result,
    output wire       done,
    output wire       busy
);

    wire      special_valid;
    wire [7:0] special_result;
    integer   sum_scaled_comb;

    function integer floor_log2;
        input integer value;
        integer bit_idx;
        begin
            floor_log2 = 0;
            for (bit_idx = 0; bit_idx < 31; bit_idx = bit_idx + 1) begin
                if ((value >> bit_idx) != 0)
                    floor_log2 = bit_idx;
            end
        end
    endfunction

    function integer fp8_to_scaled;
        input [7:0] value;
        integer exp_field;
        integer man_field;
        begin
            exp_field = value[6:3];
            man_field = value[2:0];

            if (exp_field == 0)
                fp8_to_scaled = man_field;
            else
                fp8_to_scaled = (8 + man_field) << (exp_field - 1);

            if (value[7])
                fp8_to_scaled = -fp8_to_scaled;
        end
    endfunction

    function [7:0] fp8_from_sum;
        input integer sum_scaled;

        integer abs_scaled;
        integer sign_bit;
        integer exp_biased;
        integer shift;
        integer base;
        integer rem;
        integer half;
        integer man_int;
        begin
            abs_scaled = 0;
            sign_bit = 0;
            exp_biased = 0;
            shift = 0;
            base = 0;
            rem = 0;
            half = 0;
            man_int = 0;
            fp8_from_sum = 8'h00;

            if (sum_scaled == 0) begin
                fp8_from_sum = 8'h00;
            end else begin
                if (sum_scaled < 0) begin
                    sign_bit = 1;
                    abs_scaled = -sum_scaled;
                end else begin
                    abs_scaled = sum_scaled;
                end

                if (abs_scaled > 229376) begin
                    fp8_from_sum = ((sign_bit != 0) ? 8'h80 : 8'h00) | 8'h7E;
                end else if (abs_scaled < 8) begin
                    man_int = abs_scaled;
                    fp8_from_sum = ((sign_bit != 0) ? 8'h80 : 8'h00) | man_int[7:0];
                end else begin
                    exp_biased = floor_log2(abs_scaled) - 2;
                    shift = exp_biased - 1;
                    base = abs_scaled >> shift;

                    if (shift > 0) begin
                        rem = abs_scaled - (base << shift);
                        half = 1 << (shift - 1);

                        if (rem > half)
                            base = base + 1;
                        else if ((rem == half) && (base[0] == 1'b1))
                            base = base + 1;
                    end

                    if (base >= 16) begin
                        base = 8;
                        exp_biased = exp_biased + 1;
                    end

                    if (exp_biased > 15) begin
                        fp8_from_sum = ((sign_bit != 0) ? 8'h80 : 8'h00) | 8'h7E;
                    end else if ((exp_biased == 15) && (base == 15)) begin
                        fp8_from_sum = ((sign_bit != 0) ? 8'h80 : 8'h00) | 8'h7E;
                    end else begin
                        fp8_from_sum = ((sign_bit != 0) ? 8'h80 : 8'h00) |
                                       ((exp_biased & 15) << 3) |
                                       ((base - 8) & 7);
                    end
                end
            end
        end
    endfunction

    assign special_valid  = (((a[6:3] == 4'hF) && (a[2:0] == 3'h7)) ||
                              ((b[6:3] == 4'hF) && (b[2:0] == 3'h7))) ? 1'b1 :
                             ((a[6:0] == 7'd0) && (b[6:0] == 7'd0))   ? 1'b1 : 1'b0;

    assign special_result = (((a[6:3] == 4'hF) && (a[2:0] == 3'h7)) ||
                              ((b[6:3] == 4'hF) && (b[2:0] == 3'h7))) ? 8'h7F :
                             ((a[6:0] == 7'd0) && (b[6:0] == 7'd0))   ? ((a[7] && b[7]) ? 8'h80 : 8'h00) :
                                                                          8'h00;

    always @(*) sum_scaled_comb = fp8_to_scaled(a) + fp8_to_scaled(b);

    assign result = special_valid ? special_result : fp8_from_sum(sum_scaled_comb);
    assign done   = start;
    assign busy   = 1'b0;

endmodule
