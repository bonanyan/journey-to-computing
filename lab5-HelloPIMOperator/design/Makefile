VERILATOR ?= verilator
BUILD_DIR := build
RTL := rtl/mpu.sv third_party/pislib/APIM.v
TB := tb/tb_mpu.sv
SMOKE := $(BUILD_DIR)/smoke/obj_dir/Vtb_mpu
QWEN := $(BUILD_DIR)/qwen3/Vmpu

.PHONY: all test smoke qwen3-q-projection clean
all: test
test: smoke qwen3-q-projection

$(SMOKE): $(RTL) $(TB) Makefile
	mkdir -p $(@D)
	$(VERILATOR) --binary --timing --timescale 1ns/1ps --top-module tb_mpu -GK_DIM=16 -GN_DIM=64 --Mdir $(@D) $(RTL) $(TB)

$(BUILD_DIR)/pim_linear_i4.o: sw/pim_linear_i4.c sw/pim_linear_i4.h Makefile
	mkdir -p $(@D)
	$(CC) -O2 -std=c11 -Wall -Wextra -Werror -c $< -o $@

$(QWEN): $(RTL) tb/operator_test.cpp $(BUILD_DIR)/pim_linear_i4.o Makefile
	mkdir -p $(@D)
	$(VERILATOR) --cc --exe --build --top-module mpu --Mdir $(@D) $(RTL) $(abspath tb/operator_test.cpp) -CFLAGS "-I$(abspath sw)" -LDFLAGS "$(abspath $(BUILD_DIR)/pim_linear_i4.o)"

smoke: $(SMOKE)
	$< $(SMOKE_ARGS)

qwen3-q-projection: $(QWEN)
	$<

clean:
	$(RM) -r $(BUILD_DIR)
