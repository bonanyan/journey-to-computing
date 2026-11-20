`timescale 1ns/1ns
module tb_dpim;
    bit clk;
    always #5 clk = ~clk;

    bit en, we, cme;
    bit [11:0] addr;
    bit [31:0] d;
    wire [31:0] q;
    bit [1:0] func;
    bit [31:0] operand [0:31];
    wire [31:0] result [0:31];

    DPIM dut (
        .clk(clk), .en(en), .we(we), .cme(cme), .addr(addr), .d(d), .q(q), .func(func),
        .cmIn_00(operand[0]), .cmOut_00(result[0]),
        .cmIn_01(operand[1]), .cmOut_01(result[1]),
        .cmIn_02(operand[2]), .cmOut_02(result[2]),
        .cmIn_03(operand[3]), .cmOut_03(result[3]),
        .cmIn_04(operand[4]), .cmOut_04(result[4]),
        .cmIn_05(operand[5]), .cmOut_05(result[5]),
        .cmIn_06(operand[6]), .cmOut_06(result[6]),
        .cmIn_07(operand[7]), .cmOut_07(result[7]),
        .cmIn_08(operand[8]), .cmOut_08(result[8]),
        .cmIn_09(operand[9]), .cmOut_09(result[9]),
        .cmIn_10(operand[10]), .cmOut_10(result[10]),
        .cmIn_11(operand[11]), .cmOut_11(result[11]),
        .cmIn_12(operand[12]), .cmOut_12(result[12]),
        .cmIn_13(operand[13]), .cmOut_13(result[13]),
        .cmIn_14(operand[14]), .cmOut_14(result[14]),
        .cmIn_15(operand[15]), .cmOut_15(result[15]),
        .cmIn_16(operand[16]), .cmOut_16(result[16]),
        .cmIn_17(operand[17]), .cmOut_17(result[17]),
        .cmIn_18(operand[18]), .cmOut_18(result[18]),
        .cmIn_19(operand[19]), .cmOut_19(result[19]),
        .cmIn_20(operand[20]), .cmOut_20(result[20]),
        .cmIn_21(operand[21]), .cmOut_21(result[21]),
        .cmIn_22(operand[22]), .cmOut_22(result[22]),
        .cmIn_23(operand[23]), .cmOut_23(result[23]),
        .cmIn_24(operand[24]), .cmOut_24(result[24]),
        .cmIn_25(operand[25]), .cmOut_25(result[25]),
        .cmIn_26(operand[26]), .cmOut_26(result[26]),
        .cmIn_27(operand[27]), .cmOut_27(result[27]),
        .cmIn_28(operand[28]), .cmOut_28(result[28]),
        .cmIn_29(operand[29]), .cmOut_29(result[29]),
        .cmIn_30(operand[30]), .cmOut_30(result[30]),
        .cmIn_31(operand[31]), .cmOut_31(result[31])
    );

    task automatic memory_cycle(input bit enable, write_enable,
                                input bit [11:0] address, input bit [31:0] data);
        @(negedge clk);
        en = enable;
        we = write_enable;
        cme = 1;
        addr = address;
        d = data;
        @(posedge clk);
        #1;
    endtask

    task automatic check_q(input logic [31:0] expected);
        if (q !== expected)
            $fatal(1, "addr=%03h en=%b we=%b cme=%b q=%08h expected=%08h",
                   addr, en, we, cme, q, expected);
    endtask

    task automatic run_mem();
        localparam logic [11:0] A = 12'h012, B = 12'h092;
        memory_cycle(1, 1, A, 32'h1234_abcd);
        memory_cycle(1, 1, B, 32'hcafe_0055);
        memory_cycle(1, 0, A, 0);
        check_q(32'h1234_abcd);
        memory_cycle(0, 1, B, 32'hffff_ffff);  // Disabled write
        check_q(32'h1234_abcd);
        memory_cycle(1, 0, B, 0);
        check_q(32'hcafe_0055);
        memory_cycle(1, 1, B, 32'hdead_beef);
        check_q(32'hcafe_0055);                // Write leaves the read port holding its last value
        memory_cycle(1, 0, B, 0);
        check_q(32'hdead_beef);
        memory_cycle(1, 0, A, 0);
        check_q(32'h1234_abcd);
        $display("PASS MEM  scalar reads, overwrite, disabled write and output hold");
    endtask

    task automatic run_dpim();
        localparam logic [6:0] OFFSET = 7'h12;
        logic [31:0] weight [0:31];
        logic [31:0] expected;

        for (int lane = 0; lane < 32; lane++) begin
            weight[lane] = 32'h8000_0010 + lane;
            operand[lane] = 32'h13 + lane;
            memory_cycle(1, 1, {lane[4:0], OFFSET}, weight[lane]);
        end
        memory_cycle(1, 0, {5'd1, OFFSET}, 0);
        check_q(weight[1]);

        for (int op = 0; op < 4; op++) begin
            @(negedge clk);
            cme = 0;
            addr = {5'd31, OFFSET};
            func = op[1:0];
            @(posedge clk);
            #1;
            for (int lane = 0; lane < 32; lane++) begin
                case (func)
                    0: expected = operand[lane] * weight[lane];
                    1: expected = operand[lane] & weight[lane];
                    2: expected = operand[lane] | weight[lane];
                    3: expected = operand[lane] ^ weight[lane];
                endcase
                if (result[lane] !== expected)
                    $fatal(1, "func=%0d lane=%0d got=%08h expected=%08h",
                           op, lane, result[lane], expected);
            end
            check_q(weight[1]);
            $display("DPIM func=%0d lane0=%08h lane1=%08h", op, result[0], result[1]);
        end
        $display("PASS DPIM  32 lanes x 4 operations");
    endtask

    initial begin
        if ($test$plusargs("mem")) $dumpfile("build/mem.vcd");
        else                       $dumpfile("build/dpim.vcd");
        $dumpvars(0, tb_dpim);
        if ($test$plusargs("mem")) run_mem();
        else                       run_dpim();
        $finish;
    end
endmodule
