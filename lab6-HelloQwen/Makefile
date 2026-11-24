.DEFAULT_GOAL := smoke
LAB5 ?= ../lab5-HelloPIMOperator
OPDIR := $(LAB5)/operators
include $(OPDIR)/build.mk

.PHONY: smoke layer
smoke layer: hardware
	PYTHONPATH="$(OP)" $(PYTHON) -B layer.py --build "$(BUILD)" --size $(if $(filter layer,$@),qwen3,small) --tokens $(if $(TOKENS),$(TOKENS),2) $(if $(WEIGHTS),--weights "$(WEIGHTS)",)

# The shared MPU recipe takes its Verilog sources from its prerequisites.
# The lane recipe names its source explicitly, so pass the monitor to it too.
ifeq ($(COUNT_CLOCKS),1)
COUNTER := $(CURDIR)/cycle_counter.sv
$(BUILD)/pim/Vmpu $(BUILD)/lane/Vdarkriscv: $(COUNTER)
$(BUILD)/lane/Vdarkriscv: override VERILATOR += $(COUNTER)
endif

.PHONY: perf
perf:
	$(MAKE) hardware BUILD="$(abspath $(BUILD))/perf" COUNT_CLOCKS=1
	PYTHONPATH="$(OP)" $(PYTHON) -B performance.py --build "$(abspath $(BUILD))/perf" --tokens $(if $(TOKENS),$(TOKENS),1) $(if $(WEIGHTS),--weights "$(WEIGHTS)",)
