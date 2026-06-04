// Hex to seven-segment decoder (active-low)
// seg = {DP, G, F, E, D, C, B, A}

module seven_segment_hex (
    input  logic [3:0] hex,
    input  logic       blank,
    output logic [7:0] seg
);

    logic [7:0] decoded;

    always_comb begin
        case (hex)
            4'h0: decoded = 8'b11000000;
            4'h1: decoded = 8'b11111001;
            4'h2: decoded = 8'b10100100;
            4'h3: decoded = 8'b10110000;
            4'h4: decoded = 8'b10011001;
            4'h5: decoded = 8'b10010010;
            4'h6: decoded = 8'b10000010;
            4'h7: decoded = 8'b11111000;
            4'h8: decoded = 8'b10000000;
            4'h9: decoded = 8'b10010000;
            4'hA: decoded = 8'b10001000;
            4'hB: decoded = 8'b10000011;
            4'hC: decoded = 8'b11000110;
            4'hD: decoded = 8'b10100001;
            4'hE: decoded = 8'b10000110;
            4'hF: decoded = 8'b10001110;
            default: decoded = 8'b11111111;
        endcase
    end

    assign seg = blank ? 8'b11111111 : decoded;

endmodule
