PYTHON ?= python3
VERILATOR ?= verilator
BUILD ?= $(CURDIR)/build
OP := $(abspath $(OPDIR))
D := $(abspath design)
DR := $(D)/third_party/darkriscv/rtl
ZIG_PYTHON ?= $(if $(wildcard ../lab2-CPU+PIM/.venv/bin/python),$(abspath ../lab2-CPU+PIM/.venv/bin/python),$(PYTHON))

.PHONY: hardware
hardware: $(BUILD)/vector.bin $(BUILD)/lane/Vdarkriscv $(BUILD)/pim/Vmpu

$(BUILD)/vector.bin: $(addprefix $(OP)/,kernels.S vector.c firmware.sh kernel.ld elf_image.py)
	ZIG_PYTHON="$(ZIG_PYTHON)" sh "$(OP)/firmware.sh" "$(OP)" "$(BUILD)"

$(BUILD)/driver.o: $(D)/sw/pim_linear_i4.c $(D)/sw/pim_linear_i4.h
	mkdir -p "$(BUILD)"
	$(CC) -O2 -c $< -o $@

$(BUILD)/pim/Vmpu: $(D)/rtl/mpu.sv $(D)/third_party/pislib/APIM.v $(OP)/pim_bus.cpp $(BUILD)/driver.o
	mkdir -p $(@D)
	$(VERILATOR) --cc --exe --build --top-module mpu -GK_DIM=128 -GN_DIM=128 --Mdir $(@D) $(filter %.sv %.v %.cpp,$^) -CFLAGS "-I$(D)/sw" -LDFLAGS "$(BUILD)/driver.o"

$(BUILD)/lane/Vdarkriscv: $(DR)/darkriscv.v $(DR)/config.vh $(OP)/lane_bus.cpp
	mkdir -p $(@D)
	$(VERILATOR) --cc --exe --build -Wno-fatal -I$(DR) --top-module darkriscv --Mdir $(@D) $(DR)/darkriscv.v $(OP)/lane_bus.cpp
