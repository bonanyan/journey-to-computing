// Simulation-only monitors. Each independently clocked RTL instance reports once.
module cycle_counter #(parameter string LABEL = "") (
    input logic clk,
    input logic enabled
);
    longint unsigned cycles = 0;
    always @(posedge clk)
        if (enabled) cycles <= cycles + 1;

    // stderr keeps cycle reports separate from the binary tensor stream on stdout.
    final $fdisplay(32'h8000_0002, "RTL_CYCLES %s %0d", LABEL, cycles);
endmodule

// Exclude the MPU's one-time power-on reset. Each lane reset belongs to a dispatch.
bind mpu cycle_counter #(.LABEL("mpu")) count_cycles (.clk(clk), .enabled(!rst));
bind darkriscv cycle_counter #(.LABEL("lane")) count_cycles (.clk(CLK), .enabled(1'b1));
