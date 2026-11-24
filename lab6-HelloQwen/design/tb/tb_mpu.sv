`timescale 1ns/1ps
`default_nettype none

module tb_mpu #(
    parameter integer K_DIM = 2560,
    parameter integer N_DIM = 4096
);
    localparam [7:0] REG_STATUS   = 8'h00;
    localparam [7:0] REG_COMMAND  = 8'h04;
    localparam [7:0] REG_ACT_ADDR = 8'h08;
    localparam [7:0] REG_ACT_DATA = 8'h0c;
    localparam [7:0] REG_PC       = 8'h18;
    localparam [7:0] REG_SHAPE    = 8'h24;
    localparam integer ACTIVATION_WORDS = (K_DIM + 7) / 8;

    reg clk = 1'b0;
    always #5 clk = ~clk;

    reg rst = 1'b1;
    reg host_valid = 1'b0;
    reg host_write = 1'b0;
    reg [7:0] host_addr = 8'b0;
    reg [31:0] host_wdata = 32'b0;
    wire [31:0] host_rdata;
    wire host_ready;

    wire weight_req;
    wire [31:0] weight_byte_addr;
    wire weight_gnt;
    reg weight_rvalid = 1'b0;
    reg [7:0] weight_rdata = 8'b0;

    wire result_valid;
    wire [31:0] result_index;
    wire signed [31:0] result_data;
    reg result_ready = 1'b1;

    wire busy;
    wire done;
    wire error;
    integer result_count = 0;
    reg signed [31:0] observed [0:N_DIM-1];
    integer i;
    integer j;
    integer test_output;
    integer test_input;
    integer expected_sum;
    integer signed activation_value;
    integer signed weight_value;
    reg [31:0] packed_activations;
    reg [31:0] read_value;

    assign weight_gnt = weight_req;

    // Optional observations for the worked example in the Lab 4 tutorial.
    bit show_tile;
    initial show_tile = $test$plusargs("show_tile");
    always @(posedge clk) begin
        if (show_tile && !rst && dut.state == dut.S_CAPTURE &&
            dut.tile_n_base == 0 && dut.tile_k_base == 0 &&
            dut.activation_bit_plane == 0 && dut.weight_bit_plane == 0)
            $display("MPU first tile: x_plane=0 w_plane=0 adc[0]=%0d", dut.macro_output[0]);
        if (show_tile && result_valid && result_ready && result_index == 0)
            $display("MPU writeback: y[0]=%0d", result_data);
    end

    mpu #(
        .K_DIM(K_DIM),
        .N_DIM(N_DIM),
        .FIFO_DEPTH(4)
    ) dut (
        .clk(clk),
        .rst(rst),
        .host_valid(host_valid),
        .host_write(host_write),
        .host_addr(host_addr),
        .host_wdata(host_wdata),
        .host_rdata(host_rdata),
        .host_ready(host_ready),
        .weight_req(weight_req),
        .weight_byte_addr(weight_byte_addr),
        .weight_gnt(weight_gnt),
        .weight_rvalid(weight_rvalid),
        .weight_rdata(weight_rdata),
        .result_valid(result_valid),
        .result_index(result_index),
        .result_data(result_data),
        .result_ready(result_ready),
        .busy(busy),
        .done(done),
        .error(error)
    );

    function [3:0] test_activation_nibble;
        input integer index;
        reg [31:0] value;
        begin
            value = index * 7 + 3;
            test_activation_nibble = value[3:0];
        end
    endfunction

    function [3:0] test_weight_nibble;
        input integer index;
        reg [31:0] value;
        begin
            value = index * 13 + index / 7 + 3;
            test_weight_nibble = value[3:0];
        end
    endfunction

    function integer signed decode_int4;
        input [3:0] nibble;
        begin
            decode_int4 = nibble[3] ? $signed({28'b0, nibble}) - 32'sd16 :
                                      $signed({28'b0, nibble});
        end
    endfunction

    function [7:0] test_weight_byte;
        input [31:0] byte_address;
        integer first_index;
        begin
            first_index = byte_address * 2;
            test_weight_byte = {test_weight_nibble(first_index + 1),
                                test_weight_nibble(first_index)};
        end
    endfunction

    always @(posedge clk) begin
        if (rst) begin
            weight_rvalid <= 1'b0;
            weight_rdata <= 8'b0;
            result_count <= 0;
        end else begin
            weight_rvalid <= weight_req && weight_gnt;
            if (weight_req && weight_gnt)
                weight_rdata <= test_weight_byte(weight_byte_addr);

            if (result_valid && result_ready) begin
                observed[result_index] <= result_data;
                result_count <= result_count + 1;
            end
        end
    end

    task automatic host_write32;
        input [7:0] address;
        input [31:0] value;
        begin
            @(negedge clk);
            host_valid = 1'b1;
            host_write = 1'b1;
            host_addr = address;
            host_wdata = value;
            while (!host_ready)
                @(negedge clk);
            @(posedge clk);
            @(negedge clk);
            host_valid = 1'b0;
            host_write = 1'b0;
            host_addr = 8'b0;
            host_wdata = 32'b0;
        end
    endtask

    task automatic host_read32;
        input [7:0] address;
        output [31:0] value;
        begin
            @(negedge clk);
            host_valid = 1'b1;
            host_write = 1'b0;
            host_addr = address;
            #1 value = host_rdata;
            @(negedge clk);
            host_valid = 1'b0;
            host_addr = 8'b0;
        end
    endtask

    initial begin
        repeat (4) @(posedge clk);
        @(negedge clk);
        rst = 1'b0;

        host_read32(REG_SHAPE, read_value);
        if (read_value !== {K_DIM[15:0], N_DIM[15:0]})
            $fatal(1, "shape register got %08x, expected %04x%04x",
                   read_value, K_DIM, N_DIM);

        host_write32(REG_ACT_ADDR, 32'b0);
        for (i = 0; i < ACTIVATION_WORDS; i = i + 1) begin
            packed_activations = 32'b0;
            for (j = 0; j < 8; j = j + 1) begin
                if ((i * 8 + j) < K_DIM)
                    packed_activations[j*4 +: 4] = test_activation_nibble(i * 8 + j);
            end
            host_write32(REG_ACT_DATA, packed_activations);
        end

        host_write32(REG_COMMAND, 32'd1);
        host_write32(REG_COMMAND, 32'd7);
        wait (result_valid);
        @(negedge clk);
        result_ready = 1'b0;
        repeat (3) @(negedge clk);
        result_ready = 1'b1;

        wait (done);
        @(negedge clk);

        if (error)
            $fatal(1, "accelerator reported an error");
        if (result_count != N_DIM)
            $fatal(1, "captured %0d outputs, expected %0d", result_count, N_DIM);

        for (test_output = 0; test_output < N_DIM; test_output = test_output + 1) begin
            expected_sum = 0;
            for (test_input = 0; test_input < K_DIM; test_input = test_input + 1) begin
                activation_value = decode_int4(test_activation_nibble(test_input));
                weight_value = decode_int4(test_weight_nibble(test_output * K_DIM + test_input));
                expected_sum = expected_sum + activation_value * weight_value;
            end
            if (observed[test_output] !== expected_sum)
                $fatal(1, "output %0d got %0d, expected %0d",
                       test_output, observed[test_output], expected_sum);
        end

        host_read32(REG_PC, read_value);
        if (read_value !== 32'd2)
            $fatal(1, "program counter is %0d, expected 2", read_value);

        host_read32(REG_STATUS, read_value);
        if (read_value[2:0] !== 3'b010)
            $fatal(1, "final status is %08x, expected done", read_value);

        $display("PASS pim_linear_i4 K=%0d N=%0d outputs=%0d", K_DIM, N_DIM, result_count);
        $finish;
    end

    initial begin
        #1_000_000_000;
        $fatal(1, "timed out waiting for the Q projection");
    end
endmodule

`default_nettype wire
