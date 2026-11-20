VERILATOR ?= verilator
GTKWAVE ?= gtkwave
RTL := pislib_verilog-main/src
VFLAGS := --binary --timing --trace --timescale 1ns/1ns

.PHONY: all mem dpim apim wave-mem wave-dpim wave-apim clean
all: mem dpim apim

build/dpim/Vtb_dpim: tb/tb_dpim.sv $(RTL)/DPIM.v Makefile
	mkdir -p build/dpim
	$(VERILATOR) $(VFLAGS) --top-module tb_dpim --Mdir build/dpim tb/tb_dpim.sv $(RTL)/DPIM.v

build/apim/Vtb_apim: tb/tb_apim.sv $(RTL)/APIM.v Makefile
	mkdir -p build/apim
	$(VERILATOR) $(VFLAGS) --top-module tb_apim --Mdir build/apim tb/tb_apim.sv $(RTL)/APIM.v

mem: build/dpim/Vtb_dpim
	$< +mem

dpim: build/dpim/Vtb_dpim
	$<

apim: build/apim/Vtb_apim
	$<

wave-mem wave-dpim wave-apim: wave-%: %
	@# Some Verilator releases add an outer TOP scope to VCD signal names.
	@awk 'NR == FNR { if (!root && $$1 == "$$scope") root = $$3; next } \
	      /^tb_/ && root == "TOP" { $$0 = "TOP." $$0 } { print }' \
	      build/$*.vcd views/$*.gtkw > build/$*.gtkw
	$(GTKWAVE) build/$*.vcd build/$*.gtkw

clean:
	$(RM) -r build
