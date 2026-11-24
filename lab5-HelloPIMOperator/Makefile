.DEFAULT_GOAL := operators
OPDIR := operators
include $(OPDIR)/build.mk

.PHONY: operators
operators: hardware
	$(PYTHON) -B $(OP)/demo.py "$(BUILD)" $(ONLY)
