`timescale 1ns/1ns
`include "./src/DPIM.v"

module testbench ();

	reg clk, en, cime, we;
	reg [11:0] a;
	reg [31:0] d;
	wire [31:0] q;
	reg [1:0] func;
	reg [31:0] 	cmIn_00,	cmIn_01,	cmIn_02,	cmIn_03,	
				cmIn_04,	cmIn_05,	cmIn_06,	cmIn_07,	
				cmIn_08,	cmIn_09,	cmIn_10,	cmIn_11,	
				cmIn_12,	cmIn_13,	cmIn_14,	cmIn_15,	
				cmIn_16,	cmIn_17,	cmIn_18,	cmIn_19,	
				cmIn_20,	cmIn_21,	cmIn_22,	cmIn_23,	
				cmIn_24,	cmIn_25,	cmIn_26,	cmIn_27,	
				cmIn_28,	cmIn_29,	cmIn_30,	cmIn_31;

  wire [31:0] 	cmOut_00,	cmOut_01,	cmOut_02,	cmOut_03,	
				cmOut_04,	cmOut_05,	cmOut_06,	cmOut_07,	
				cmOut_08,	cmOut_09,	cmOut_10,	cmOut_11,	
				cmOut_12,	cmOut_13,	cmOut_14,	cmOut_15,	
				cmOut_16,	cmOut_17,	cmOut_18,	cmOut_19,	
				cmOut_20,	cmOut_21,	cmOut_22,	cmOut_23,	
				cmOut_24,	cmOut_25,	cmOut_26,	cmOut_27,	
				cmOut_28,	cmOut_29,	cmOut_30,	cmOut_31;


reg [7:0] wr_data, rd_data;
reg err_flag = 0;
reg err_flag_cim = 0;

reg [7:0] data [0:1023];

initial begin
en = 0;
# 10;
en = 1;
end

initial begin
clk = 0;
#1;
forever #5 clk <= ~clk;
end

// initial
// $monitor($realtime,,"web=%x,a=%x,d=%x,q=%x",web,a,d,q);

initial begin
// initialization
#11 
err_flag = 0;
a = -1;
we = 0;
cime = 0;
func = 2'd3; //check XOR ^

// write & read test
repeat(1024) begin
	#10 
	a = a + 1;
	we = 1;
	wr_data = $random;
	data[a] = wr_data;
	d = wr_data;
	
	#10 
	we = 0;

	#10 
	rd_data = q;

	if (wr_data!=rd_data) begin 
		err_flag = 1;
		$display ("ERROR, Addr 0x%0x, Data 0x%0x != 0x%0x",a, wr_data, rd_data);	
		end
end

if (err_flag) 
	$display ("################### ERROR ############");
else  
	$display ("################### Read/Write Mode Passed at %0d ns ######################",$time);

// cim mode test
#10
a = -1;
we = 0;
cime = 0;

repeat(1024) begin
	#10 
	a = a + 1;
	cime = 1;
	cmIn_00 = $random;

	#10 
	cime = 0;

	if ((cmIn_00 ^ data[{5'd0, a[6:0]}])!=cmOut_00) begin 
		err_flag_cim = 1;
		$display ("ERROR, Addr 0x%0x, Data 0x%0x != 0x%0x",a, wr_data, rd_data);	
		end
end

if (err_flag_cim) 
	$display ("################### ERROR ############");
else  
	$display ("################### CIM Mode Passed at %0d ns ######################",$time);

// finish
$finish;
end


// instantiation
DPIM idpim (
.clk (clk)
,.en (en)
,.addr (a)
,.d (d)
,.q (q)
,.we (we)
,.cme (cime)
,.func (func)
,.cmIn_00 (cmOut_00)
,.cmIn_01 (cmOut_01)
,.cmIn_02 (cmOut_02)
,.cmIn_03 (cmOut_03)
,.cmIn_04 (cmOut_04)
,.cmIn_05 (cmOut_05)
,.cmIn_06 (cmOut_06)
,.cmIn_07 (cmOut_07)
,.cmIn_08 (cmOut_08)
,.cmIn_09 (cmOut_09)
,.cmIn_10 (cmOut_10)
,.cmIn_11 (cmOut_11)
,.cmIn_12 (cmOut_12)
,.cmIn_13 (cmOut_13)
,.cmIn_14 (cmOut_14)
,.cmIn_15 (cmOut_15)
,.cmIn_16 (cmOut_16)
,.cmIn_17 (cmOut_17)
,.cmIn_18 (cmOut_18)
,.cmIn_19 (cmOut_19)
,.cmIn_20 (cmOut_20)
,.cmIn_21 (cmOut_21)
,.cmIn_22 (cmOut_22)
,.cmIn_23 (cmOut_23)
,.cmIn_24 (cmOut_24)
,.cmIn_25 (cmOut_25)
,.cmIn_26 (cmOut_26)
,.cmIn_27 (cmOut_27)
,.cmIn_28 (cmOut_28)
,.cmIn_29 (cmOut_29)
,.cmIn_30 (cmOut_30)
,.cmIn_31 (cmOut_31)
,.cmOut_00 (cmOut_00)
,.cmOut_01 (cmOut_01)
,.cmOut_02 (cmOut_02)
,.cmOut_03 (cmOut_03)
,.cmOut_04 (cmOut_04)
,.cmOut_05 (cmOut_05)
,.cmOut_06 (cmOut_06)
,.cmOut_07 (cmOut_07)
,.cmOut_08 (cmOut_08)
,.cmOut_09 (cmOut_09)
,.cmOut_10 (cmOut_10)
,.cmOut_11 (cmOut_11)
,.cmOut_12 (cmOut_12)
,.cmOut_13 (cmOut_13)
,.cmOut_14 (cmOut_14)
,.cmOut_15 (cmOut_15)
,.cmOut_16 (cmOut_16)
,.cmOut_17 (cmOut_17)
,.cmOut_18 (cmOut_18)
,.cmOut_19 (cmOut_19)
,.cmOut_20 (cmOut_20)
,.cmOut_21 (cmOut_21)
,.cmOut_22 (cmOut_22)
,.cmOut_23 (cmOut_23)
,.cmOut_24 (cmOut_24)
,.cmOut_25 (cmOut_25)
,.cmOut_26 (cmOut_26)
,.cmOut_27 (cmOut_27)
,.cmOut_28 (cmOut_28)
,.cmOut_29 (cmOut_29)
,.cmOut_30 (cmOut_30)
,.cmOut_31 (cmOut_31)
);

initial begin
 $dumpfile("wave.vcd");
 $dumpvars(0,testbench);
end

endmodule
