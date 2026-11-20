`timescale 1ns/1ns
module tb_apim;
    bit clk;
    always #5 clk = ~clk;

    bit cs, web, cimeb;
    bit [9:0] a;
    bit [7:0] d;
    wire [7:0] q;
    bit [3:0] x [0:3];
    wire [5:0] code [0:7];

    Basic_GeMM_CIM dut (
        .clk(clk), .cs(cs), .web(web), .cimeb(cimeb), .a(a), .d(d), .q(q),
        .cim_in0(x[0]), .cim_in1(x[1]), .cim_in2(x[2]), .cim_in3(x[3]),
        .cim_out0(code[0]), .cim_out1(code[1]), .cim_out2(code[2]), .cim_out3(code[3]),
        .cim_out4(code[4]), .cim_out5(code[5]), .cim_out6(code[6]), .cim_out7(code[7])
    );

    localparam bit [2:0] GROUP = 3'd2;
    localparam bit [1:0] SLOT = 2'd1;
    bit [7:0] weight [0:3][0:7];

    task automatic program_weight(input int row, col, input bit [7:0] value);
        @(negedge clk);
        cs = 1;
        web = 0;
        cimeb = 1;
        a = {row[1:0], GROUP, col[2:0], SLOT};
        d = value;
        @(posedge clk);
        #1;
        weight[row][col] = value;
    endtask

    task automatic compute(input bit [3:0] x0, x1, x2, x3);
        int unsigned sum;
        @(negedge clk);
        web = 1;
        cimeb = 0;
        a = {2'd0, GROUP, 3'd0, SLOT};
        x[0] = x0;
        x[1] = x1;
        x[2] = x2;
        x[3] = x3;
        @(posedge clk);
        #1;
        for (int col = 0; col < 8; col++) begin
            sum = 0;
            for (int row = 0; row < 4; row++)
                sum += x[row] * weight[row][col];
            if (code[col] !== 6'(sum / 256))
                $fatal(1, "column=%0d sum=%0d got=%0d expected=%0d",
                       col, sum, code[col], sum / 256);
        end
        $display("APIM x=[%0d %0d %0d %0d] codes=[%0d %0d %0d %0d %0d %0d %0d %0d]",
                 x0, x1, x2, x3, code[0], code[1], code[2], code[3],
                 code[4], code[5], code[6], code[7]);
    endtask

    initial begin
        $dumpfile("build/apim.vcd");
        $dumpvars(0, tb_apim);
        for (int row = 0; row < 4; row++)
            for (int col = 0; col < 8; col++)
                program_weight(row, col, 8'(16 * (row + 1) + 24 * col));

        @(negedge clk);
        web = 1;
        cimeb = 1;
        a = {2'd3, GROUP, 3'd7, SLOT};
        @(posedge clk);
        #1;
        if (q !== weight[3][7])
            $fatal(1, "APIM scalar read addr=%03h got=%02h expected=%02h", a, q, weight[3][7]);
        compute(1, 2, 3, 4);

        program_weight(0, 0, 255);
        program_weight(1, 0, 1);
        compute(1, 0, 0, 0);  // Column 0 sum = 255
        compute(1, 1, 0, 0);  // Column 0 sum = 256

        for (int row = 0; row < 4; row++)
            program_weight(row, 0, 255);
        compute(15, 15, 15, 15);
        $display("PASS APIM  all 8 columns, including the ADC boundary and maximum sum");
        $finish;
    end
endmodule
