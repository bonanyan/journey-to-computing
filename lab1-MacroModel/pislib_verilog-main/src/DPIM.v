/*
 * Copyright	: 2022, Dr. Bonan Yan's research group @Peking Univ.
 * File    	:   DPIM.v
 * Time    	:   2022/12/27 03:44:14
 * Author  	:   Bonan Yan's group (bonanyan@pku.edu.cn) 
 * Description	:   
 */

module DPIM (
  clk,
  en,
  addr,
  d,
  q,
  we,
  cme,
  cmIn_00,	cmIn_01,	cmIn_02,	cmIn_03,	
  cmIn_04,	cmIn_05,	cmIn_06,	cmIn_07,	
  cmIn_08,	cmIn_09,	cmIn_10,	cmIn_11,	
  cmIn_12,	cmIn_13,	cmIn_14,	cmIn_15,	
  cmIn_16,	cmIn_17,	cmIn_18,	cmIn_19,	
  cmIn_20,	cmIn_21,	cmIn_22,	cmIn_23,	
  cmIn_24,	cmIn_25,	cmIn_26,	cmIn_27,	
  cmIn_28,	cmIn_29,	cmIn_30,	cmIn_31,

  cmOut_00,	cmOut_01,	cmOut_02,	cmOut_03,	
  cmOut_04,	cmOut_05,	cmOut_06,	cmOut_07,	
  cmOut_08,	cmOut_09,	cmOut_10,	cmOut_11,	
  cmOut_12,	cmOut_13,	cmOut_14,	cmOut_15,	
  cmOut_16,	cmOut_17,	cmOut_18,	cmOut_19,	
  cmOut_20,	cmOut_21,	cmOut_22,	cmOut_23,	
  cmOut_24,	cmOut_25,	cmOut_26,	cmOut_27,	
  cmOut_28,	cmOut_29,	cmOut_30,	cmOut_31,

  func
);

//Need to define the address map first and 
//change all of the following parameter accordingly
parameter 
	DATA_WIDTH = 32, // unit: bit
	ADDR_WIDTH = 12; // unit: bit

//--------------Generated Parameters----------------------- 
parameter 
	RAM_DEPTH = 1 << ADDR_WIDTH;

//--------------Input Ports----------------------- 
input                  clk; //clk input
input [ADDR_WIDTH-1:0] addr; //address, both effective in memory mode & CIM mode
input                  en; //overall enable, chip select
input                  we; //write enable, high active
input                  cme; //CIM enable, high active
input [DATA_WIDTH-1:0] d; //memory mode input data
input [DATA_WIDTH-1:0]  cmIn_00,	cmIn_01,	cmIn_02,	cmIn_03,	
                        cmIn_04,	cmIn_05,	cmIn_06,	cmIn_07,	
                        cmIn_08,	cmIn_09,	cmIn_10,	cmIn_11,	
                        cmIn_12,	cmIn_13,	cmIn_14,	cmIn_15,	
                        cmIn_16,	cmIn_17,	cmIn_18,	cmIn_19,	
                        cmIn_20,	cmIn_21,	cmIn_22,	cmIn_23,	
                        cmIn_24,	cmIn_25,	cmIn_26,	cmIn_27,	
                        cmIn_28,	cmIn_29,	cmIn_30,	cmIn_31;
input [1:0] func;


//--------------Output Ports----------------------- 
output reg [DATA_WIDTH-1:0] q; //memory mode output data
output reg [DATA_WIDTH-1:0] cmOut_00,	cmOut_01,	cmOut_02,	cmOut_03,
                            cmOut_04,	cmOut_05,	cmOut_06,	cmOut_07,	
                            cmOut_08,	cmOut_09,	cmOut_10,	cmOut_11,	
                            cmOut_12,	cmOut_13,	cmOut_14,	cmOut_15,	
                            cmOut_16,	cmOut_17,	cmOut_18,	cmOut_19,	
                            cmOut_20,	cmOut_21,	cmOut_22,	cmOut_23,	
                            cmOut_24,	cmOut_25,	cmOut_26,	cmOut_27,	
                            cmOut_28,	cmOut_29,	cmOut_30,	cmOut_31;


//--------------Internal variables---------------- 

reg [DATA_WIDTH-1:0] mem [0:RAM_DEPTH-1];

//--------------Code Starts Here------------------ 

// Memory Write Block 
// Write Operation : When we = 1, cs = 1
always @ (posedge clk)
begin : MEM_WRITE
   if ( en && we ) begin
       mem[addr] = d;
   end
end

// Memory Read Block 
// Read Operation : When we = 0, oe = 1, cs = 1
always @ (posedge clk)
begin : MEM_READ
  if (en && !we) begin
    if(cme) begin
      q = mem[addr];
    end else begin
      // enter cim mode
      cmOut_00  = CimFunc(cmIn_00, mem[{5'd0, addr[6:0]}], func);
      cmOut_01  = CimFunc(cmIn_01, mem[{5'd1, addr[6:0]}], func);
      cmOut_02  = CimFunc(cmIn_02, mem[{5'd2, addr[6:0]}], func);
      cmOut_03  = CimFunc(cmIn_03, mem[{5'd3, addr[6:0]}], func);
      cmOut_04  = CimFunc(cmIn_04, mem[{5'd4, addr[6:0]}], func);
      cmOut_05  = CimFunc(cmIn_05, mem[{5'd5, addr[6:0]}], func);
      cmOut_06  = CimFunc(cmIn_06, mem[{5'd6, addr[6:0]}], func);
      cmOut_07  = CimFunc(cmIn_07, mem[{5'd7, addr[6:0]}], func);
      cmOut_08  = CimFunc(cmIn_08, mem[{5'd8, addr[6:0]}], func);
      cmOut_09  = CimFunc(cmIn_09, mem[{5'd9, addr[6:0]}], func);
      cmOut_10  = CimFunc(cmIn_10, mem[{5'd10, addr[6:0]}], func);
      cmOut_11  = CimFunc(cmIn_11, mem[{5'd11, addr[6:0]}], func);
      cmOut_12  = CimFunc(cmIn_12, mem[{5'd12, addr[6:0]}], func);
      cmOut_13  = CimFunc(cmIn_13, mem[{5'd13, addr[6:0]}], func);
      cmOut_14  = CimFunc(cmIn_14, mem[{5'd14, addr[6:0]}], func);
      cmOut_15  = CimFunc(cmIn_15, mem[{5'd15, addr[6:0]}], func);
      cmOut_16  = CimFunc(cmIn_16, mem[{5'd16, addr[6:0]}], func);
      cmOut_17  = CimFunc(cmIn_17, mem[{5'd17, addr[6:0]}], func);
      cmOut_18  = CimFunc(cmIn_18, mem[{5'd18, addr[6:0]}], func);
      cmOut_19  = CimFunc(cmIn_19, mem[{5'd19, addr[6:0]}], func);
      cmOut_20  = CimFunc(cmIn_20, mem[{5'd20, addr[6:0]}], func);
      cmOut_21  = CimFunc(cmIn_21, mem[{5'd21, addr[6:0]}], func);
      cmOut_22  = CimFunc(cmIn_22, mem[{5'd22, addr[6:0]}], func);
      cmOut_23  = CimFunc(cmIn_23, mem[{5'd23, addr[6:0]}], func);
      cmOut_24  = CimFunc(cmIn_24, mem[{5'd24, addr[6:0]}], func);
      cmOut_25  = CimFunc(cmIn_25, mem[{5'd25, addr[6:0]}], func);
      cmOut_26  = CimFunc(cmIn_26, mem[{5'd26, addr[6:0]}], func);
      cmOut_27  = CimFunc(cmIn_27, mem[{5'd27, addr[6:0]}], func);
      cmOut_28  = CimFunc(cmIn_28, mem[{5'd28, addr[6:0]}], func);
      cmOut_29  = CimFunc(cmIn_29, mem[{5'd29, addr[6:0]}], func);
      cmOut_30  = CimFunc(cmIn_30, mem[{5'd30, addr[6:0]}], func);
      cmOut_31  = CimFunc(cmIn_31, mem[{5'd31, addr[6:0]}], func);
    end
  end
end


function [DATA_WIDTH-1:0] CimFunc;
input [DATA_WIDTH-1:0] a;
input [DATA_WIDTH-1:0] b;
input [1:0] func;
if(func==2'd0)
  CimFunc = a * b;
else if(func==2'd1)
  CimFunc = a & b;
else if(func==2'd2)
  CimFunc = a | b;
else
  CimFunc = a ^ b;
endfunction

endmodule // Basic_GeMM_CIM