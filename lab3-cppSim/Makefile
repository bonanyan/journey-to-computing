CXXFLAGS ?= -O2 -Wall -Wextra -pedantic

FIRMWARE := $(addprefix examples/lab2/,$(addsuffix .hex,row_switch vector_mul bit_masks memory_sweep))

.PHONY: all run-cim run-cpu-cim run clean
all: build/cim build/cosim

build/cim: simulators/cim_array.hpp simulators/timing.hpp
build/cosim: simulators/rv32.hpp simulators/lab2_system.hpp simulators/dpim.hpp

build/%: simulators/%.cpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -std=c++17 $< $(LDFLAGS) $(LDLIBS) -o $@

build results:
	mkdir -p $@

run-cim: build/cim examples/example.cim
run-cpu-cim: build/cosim $(FIRMWARE)
run: all examples/example.cim $(FIRMWARE)

# Publish reports after the selected examples finish successfully.
run-cim run-cpu-cim run: | results
	@set -eu; \
	reports=$$(mktemp -d results/.run.XXXXXX); \
	trap 'rm -rf "$$reports"' EXIT; \
	case "$@" in run|run-cim) \
		build/cim examples/example.cim > "$$reports/cim.txt";; \
	esac; \
	case "$@" in run|run-cpu-cim) \
		for image in $(FIRMWARE); do \
			build/cosim "$$image"; \
		done > "$$reports/cpu-dpim.txt";; \
	esac; \
	for name in cim.txt cpu-dpim.txt; do \
		if test -f "$$reports/$$name"; then \
			mv "$$reports/$$name" "results/$$name"; \
			printf '\n%s\n' "results/$$name"; cat "results/$$name"; \
		fi; \
	done

clean:
	$(RM) build/cim build/cosim
