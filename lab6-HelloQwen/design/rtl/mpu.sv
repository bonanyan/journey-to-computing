`timescale 1ns/1ps
`default_nettype none

// MPU stage 1. Exact signed-INT4 matrix-vector projection using four Lab 1 APIM macros.
// Activations are written in eight-nibble, low-nibble-first 32-bit words.
// Packed weight bytes use low-nibble-first tensor order on the memory port.
module mpu #(
    parameter integer K_DIM = 2560,
    parameter integer N_DIM = 4096,
    parameter integer FIFO_DEPTH = 4
) (
    input  wire               clk,
    input  wire               rst,

    input  wire               host_valid,
    input  wire               host_write,
    input  wire [7:0]         host_addr,
    input  wire [31:0]        host_wdata,
    output reg  [31:0]        host_rdata,
    output logic              host_ready,

    output wire               weight_req,
    output wire [31:0]        weight_byte_addr,
    input  wire               weight_gnt,
    input  wire               weight_rvalid,
    input  wire [7:0]         weight_rdata,

    output wire               result_valid,
    output wire [31:0]        result_index,
    output wire signed [31:0] result_data,
    input  wire               result_ready,

    output wire               busy,
    output wire               done,
    output reg                error
);

    localparam [7:0] REG_STATUS   = 8'h00;
    localparam [7:0] REG_COMMAND  = 8'h04;
    localparam [7:0] REG_ACT_ADDR = 8'h08;
    localparam [7:0] REG_ACT_DATA = 8'h0c;
    localparam [7:0] REG_OUT_ADDR = 8'h10;
    localparam [7:0] REG_OUT_DATA = 8'h14;
    localparam [7:0] REG_PC       = 8'h18;
    localparam [7:0] REG_WEIGHT_BASE = 8'h20;
    localparam [7:0] REG_SHAPE    = 8'h24;
    localparam [7:0] REG_K_RANGE  = 8'h28; // exclusive end [31:16], start [15:0]

    localparam [2:0] OP_LINEAR_I4 = 3'b001;
    localparam [2:0] OP_HALT      = 3'b111;

    localparam [4:0] S_IDLE          = 5'd0;
    localparam [4:0] S_WEIGHT_REQ    = 5'd1;
    localparam [4:0] S_WEIGHT_WAIT   = 5'd2;
    localparam [4:0] S_PROGRAM       = 5'd3;
    localparam [4:0] S_COMPUTE       = 5'd4;
    localparam [4:0] S_CAPTURE       = 5'd5;
    localparam [4:0] S_WRITE_RESULTS = 5'd6;
    localparam [4:0] S_FAULT         = 5'd7;

    localparam integer ACTIVATION_WORDS = (K_DIM + 7) / 8;
    localparam integer FIFO_PTR_WIDTH = FIFO_DEPTH > 1 ? $clog2(FIFO_DEPTH) : 1;
    localparam integer FIFO_COUNT_WIDTH = $clog2(FIFO_DEPTH + 1);
    localparam [FIFO_PTR_WIDTH-1:0] FIFO_LAST_PTR = FIFO_PTR_WIDTH'(FIFO_DEPTH - 1);
    localparam [FIFO_COUNT_WIDTH-1:0] FIFO_DEPTH_COUNT = FIFO_COUNT_WIDTH'(FIFO_DEPTH);

    reg [31:0] activation_mem [0:ACTIVATION_WORDS-1];
    reg signed [31:0] result_memory [0:N_DIM-1];
    reg [31:0] command_fifo [0:FIFO_DEPTH-1];
    reg [FIFO_PTR_WIDTH-1:0] fifo_read_ptr;
    reg [FIFO_PTR_WIDTH-1:0] fifo_write_ptr;
    reg [FIFO_COUNT_WIDTH-1:0] fifo_count;

    reg [31:0] activation_addr;
    reg [31:0] output_addr;
    reg [31:0] weight_base_addr;
    reg [15:0] k_first, k_end;
    reg [31:0] program_counter;
    reg completed;

    reg [4:0] state;
    reg [31:0] tile_n_base;
    reg [31:0] tile_k_base;
    // Byte 2*c + p holds inputs 2*p and 2*p+1 for output column c.
    reg [5:0] fetch_index;
    reg [7:0] weight_tile [0:63];
    reg [4:0] program_index; // {input row, column within one macro}
    reg [1:0] weight_bit_plane;
    reg [1:0] activation_bit_plane;
    reg [4:0] result_local_index;
    reg signed [31:0] accumulator [0:31];

    reg [3:0] macro_cs;
    reg [3:0] macro_web;
    reg [3:0] macro_cimeb;
    reg [9:0] macro_address [0:3];
    reg [7:0] macro_write_data [0:3];
    reg [3:0] macro_input [0:3];
    wire [5:0] macro_output [0:31];

    wire command_push;
    wire command_pop;
    wire [31:0] weight_column = tile_n_base + 32'(fetch_index[5:1]);
    wire [31:0] input_pair = (tile_k_base >> 1) + 32'(fetch_index[0]);
    wire [31:0] result_read_data = output_addr < N_DIM ? result_memory[output_addr] : 0;

    assign command_push = host_valid && host_write && host_addr == REG_COMMAND && host_ready;
    assign command_pop = state == S_IDLE && fifo_count != 0;
    // Protect the shared operand buffers for the lifetime of queued work.
    always_comb begin
        host_ready = 1;
        if (host_valid && host_write && !error) begin
            case (host_addr)
                REG_COMMAND: host_ready = fifo_count < FIFO_DEPTH_COUNT;
                REG_ACT_ADDR, REG_ACT_DATA, REG_K_RANGE, REG_WEIGHT_BASE: host_ready = !busy;
                default: host_ready = 1;
            endcase
        end
    end

    assign weight_req = state == S_WEIGHT_REQ;
    assign weight_byte_addr = weight_base_addr + weight_column * (K_DIM / 2) + input_pair;
    assign result_valid = state == S_WRITE_RESULTS;
    assign result_index = tile_n_base + 32'(result_local_index);
    assign result_data = accumulator[result_local_index];
    assign busy = (state != S_IDLE && state != S_FAULT) || (!error && fifo_count != 0);
    assign done = completed && !busy && !error;

    function [FIFO_PTR_WIDTH-1:0] increment_fifo_pointer;
        input [FIFO_PTR_WIDTH-1:0] pointer_value;
        begin
            if (pointer_value == FIFO_LAST_PTR)
                increment_fifo_pointer = {FIFO_PTR_WIDTH{1'b0}};
            else
                increment_fifo_pointer = pointer_value + 1'b1;
        end
    endfunction

    // Each pair of set bits contributes 8 * 32 = 256 to the analog sum.
    // The macro's six-bit ADC reports exactly the number of matching rows.
    wire [15:0] input_tile = tile_k_base[2]
        ? activation_mem[tile_k_base >> 3][31:16]
        : activation_mem[tile_k_base >> 3][15:0];

    assign macro_cs = {4{state == S_PROGRAM || state == S_COMPUTE}};
    assign macro_web = {4{state != S_PROGRAM}};
    assign macro_cimeb = {4{state != S_COMPUTE}};

    genvar macro_number_gen;
    generate
        for (macro_number_gen = 0; macro_number_gen < 4; macro_number_gen++) begin : gen_acim
            localparam logic [1:0] BANK = 2'(macro_number_gen);
            wire [5:0] byte_index = {BANK, program_index[2:0], program_index[4]};
            wire [2:0] bit_index = {program_index[3], weight_bit_plane};
            assign macro_write_data[macro_number_gen] = weight_tile[byte_index][bit_index] ? 8'd32 : 0;
            assign macro_address[macro_number_gen] = state == S_PROGRAM
                ? {program_index[4:3], 3'b0, program_index[2:0], 2'b0} : 10'b0;
            assign macro_input[macro_number_gen] =
                input_tile[macro_number_gen * 4 + 32'(activation_bit_plane)] ? 4'd8 : 0;
            Basic_GeMM_CIM acim (
                .clk(clk),
                .a(macro_address[macro_number_gen]),
                .cs(macro_cs[macro_number_gen]),
                .web(macro_web[macro_number_gen]),
                .cimeb(macro_cimeb[macro_number_gen]),
                .d(macro_write_data[macro_number_gen]),
                .cim_in0(macro_input[0]),
                .cim_in1(macro_input[1]),
                .cim_in2(macro_input[2]),
                .cim_in3(macro_input[3]),
                .q(),
                .cim_out0(macro_output[macro_number_gen*8+0]),
                .cim_out1(macro_output[macro_number_gen*8+1]),
                .cim_out2(macro_output[macro_number_gen*8+2]),
                .cim_out3(macro_output[macro_number_gen*8+3]),
                .cim_out4(macro_output[macro_number_gen*8+4]),
                .cim_out5(macro_output[macro_number_gen*8+5]),
                .cim_out6(macro_output[macro_number_gen*8+6]),
                .cim_out7(macro_output[macro_number_gen*8+7])
            );
        end
    endgenerate

    always @* begin
        host_rdata = 32'b0;
        if (host_valid && !host_write) begin
            case (host_addr)
                REG_STATUS: begin
                    host_rdata[0] = busy;
                    host_rdata[1] = done;
                    host_rdata[2] = error;
                    host_rdata[15:8] = 8'(fifo_count);
                end
                REG_ACT_ADDR: host_rdata = activation_addr;
                REG_ACT_DATA: begin
                    if (activation_addr < ACTIVATION_WORDS)
                        host_rdata = activation_mem[activation_addr];
                    else
                        host_rdata = 32'b0;
                end
                REG_OUT_ADDR: host_rdata = output_addr;
                REG_OUT_DATA: host_rdata = result_read_data;
                REG_PC:       host_rdata = program_counter;
                REG_K_RANGE: host_rdata = {k_end, k_first};
                REG_WEIGHT_BASE: host_rdata = weight_base_addr;
                REG_SHAPE:    host_rdata = {K_DIM[15:0], N_DIM[15:0]};
                default:      host_rdata = 32'b0;
            endcase
        end
    end

    wire [2:0] plane_shift = 3'(activation_bit_plane) + 3'(weight_bit_plane);
    wire subtract_plane = (activation_bit_plane == 3) ^ (weight_bit_plane == 3);
    wire weight_received = weight_rvalid &&
        (state == S_WEIGHT_WAIT || (state == S_WEIGHT_REQ && weight_gnt));

    always_ff @(posedge clk) begin
        if (rst) begin
            fifo_read_ptr <= 0;
            fifo_write_ptr <= 0;
            fifo_count <= 0;
            activation_addr <= 0;
            output_addr <= 0;
            weight_base_addr <= 0;
            k_first <= 0;
            k_end <= 16'(K_DIM);
            program_counter <= 0;
            completed <= 0;
            error <= 0;
            state <= S_IDLE;
            tile_n_base <= 0;
            tile_k_base <= 0;
            fetch_index <= 0;
            program_index <= 0;
            weight_bit_plane <= 0;
            activation_bit_plane <= 0;
            result_local_index <= 0;
        end else if (!error) begin
            case ({command_push, command_pop})
                2'b10: fifo_count <= fifo_count + 1'b1;
                2'b01: fifo_count <= fifo_count - 1'b1;
                default: ;
            endcase
            if (command_push) begin
                command_fifo[fifo_write_ptr] <= host_wdata;
                fifo_write_ptr <= increment_fifo_pointer(fifo_write_ptr);
                completed <= 0;
            end
            if (command_pop)
                fifo_read_ptr <= increment_fifo_pointer(fifo_read_ptr);

            if (host_valid && host_ready && host_write) begin
                case (host_addr)
                    REG_ACT_ADDR: activation_addr <= host_wdata;
                    REG_ACT_DATA: begin
                        if (activation_addr < ACTIVATION_WORDS) begin
                            activation_mem[activation_addr] <= host_wdata;
                            activation_addr <= activation_addr + 1;
                        end else begin
                            error <= 1;
                            state <= S_FAULT;
                        end
                    end
                    REG_OUT_ADDR: output_addr <= host_wdata;
                    REG_WEIGHT_BASE: weight_base_addr <= host_wdata;
                    REG_K_RANGE: {k_end, k_first} <= host_wdata;
                    default: ;
                endcase
            end
            if (host_valid && !host_write && host_addr == REG_OUT_DATA)
                output_addr <= output_addr + 1;

            case (state)
                S_IDLE: if (command_pop) begin
                    case (command_fifo[fifo_read_ptr])
                        32'(OP_LINEAR_I4): begin
                            if (k_first >= k_end || 32'(k_end) > K_DIM ||
                                k_first[1:0] != 0 || k_end[1:0] != 0) begin
                                error <= 1;
                                state <= S_FAULT;
                            end else begin
                                tile_n_base <= 0;
                                tile_k_base <= 32'(k_first);
                                result_local_index <= 0;
                                for (int lane = 0; lane < 32; lane++)
                                    accumulator[lane] <= 0;
                                state <= S_WEIGHT_REQ;
                            end
                        end
                        32'(OP_HALT): begin
                            completed <= 1;
                            program_counter <= program_counter + 1;
                        end
                        default: begin
                            error <= 1;
                            state <= S_FAULT;
                        end
                    endcase
                end

                S_WEIGHT_REQ, S_WEIGHT_WAIT: begin
                    if (weight_received) begin
                        weight_tile[fetch_index] <= weight_rdata;
                        fetch_index <= fetch_index + 1'b1;
                        if (fetch_index == 63) begin
                            weight_bit_plane <= 0;
                            state <= S_PROGRAM;
                        end else begin
                            state <= S_WEIGHT_REQ;
                        end
                    end else if (weight_req && weight_gnt) begin
                        state <= S_WEIGHT_WAIT;
                    end
                end

                S_PROGRAM: begin
                    // Write the same row/column address in all four macros.
                    program_index <= program_index + 1'b1;
                    if (program_index == 31) begin
                        activation_bit_plane <= 0;
                        state <= S_COMPUTE;
                    end
                end

                S_COMPUTE: state <= S_CAPTURE;

                S_CAPTURE: begin
                    for (int lane = 0; lane < 32; lane++) begin
                        logic signed [31:0] contribution;
                        contribution = $signed({26'b0, macro_output[lane]}) <<< plane_shift;
                        accumulator[lane] <= subtract_plane ? accumulator[lane] - contribution
                                                           : accumulator[lane] + contribution;
                    end
                    if (activation_bit_plane != 3) begin
                        activation_bit_plane <= activation_bit_plane + 1'b1;
                        state <= S_COMPUTE;
                    end else if (weight_bit_plane != 3) begin
                        weight_bit_plane <= weight_bit_plane + 1'b1;
                        state <= S_PROGRAM;
                    end else if (tile_k_base + 4 < 32'(k_end)) begin
                        tile_k_base <= tile_k_base + 4;
                        state <= S_WEIGHT_REQ;
                    end else begin
                        state <= S_WRITE_RESULTS;
                    end
                end

                S_WRITE_RESULTS: if (result_ready) begin
                    result_memory[result_index] <= result_data;
                    result_local_index <= result_local_index + 1'b1;
                    if (result_local_index == 31) begin
                        if (tile_n_base + 32 < N_DIM) begin
                            tile_n_base <= tile_n_base + 32;
                            tile_k_base <= 32'(k_first);
                            for (int lane = 0; lane < 32; lane++)
                                accumulator[lane] <= 0;
                            state <= S_WEIGHT_REQ;
                        end else begin
                            program_counter <= program_counter + 1;
                            state <= S_IDLE;
                        end
                    end
                end
                default: begin
                    error <= 1;
                    state <= S_FAULT;
                end
            endcase
        end
    end

    initial begin
        if (K_DIM <= 0 || K_DIM > 65535 || K_DIM % 4 != 0 ||
            N_DIM <= 0 || N_DIM > 65535 || N_DIM % 32 != 0 ||
            FIFO_DEPTH < 1 || FIFO_DEPTH > 255)
            $fatal(1, "Unsupported PIM dimensions or FIFO depth");
    end
endmodule

`default_nettype wire
