CXXFLAGS ?= -O2 -Wall -Wextra -pedantic

.PHONY: all run clean
all: build/cim build/cosim

build/%: simulators/%.cpp simulators/timing.hpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -std=c++17 $< $(LDFLAGS) $(LDLIBS) -o $@

build results:
	mkdir -p $@

# Publish reports only after all three examples finish successfully.
run: all examples/example.trace | results
	@set -eu; \
	reports=$$(mktemp -d results/.run.XXXXXX); \
	trap 'rm -rf "$$reports"' EXIT; \
	build/cim > "$$reports/cim.txt"; \
	build/cosim examples/example.trace > "$$reports/cosim-overlap.txt"; \
	build/cosim examples/example.trace --blocking > "$$reports/cosim-blocking.txt"; \
	mv "$$reports"/*.txt results/
	@for report in results/cim.txt results/cosim-overlap.txt results/cosim-blocking.txt; do \
		printf '\n%s\n' "$$report"; cat "$$report" || exit 1; \
	done

clean:
	$(RM) build/cim build/cosim
