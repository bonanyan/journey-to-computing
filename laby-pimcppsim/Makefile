CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic

BUILD_DIR = build
RESULTS_DIR = results
SIMULATOR = $(BUILD_DIR)/cpu_pim_sim
TESTS = $(BUILD_DIR)/cpu_pim_tests
COMMON_OBJECTS = $(BUILD_DIR)/cim_simulator.o $(BUILD_DIR)/trace_simulator.o

.PHONY: all run test clean clean-results

all: $(SIMULATOR) $(TESTS)

$(SIMULATOR): $(COMMON_OBJECTS) $(BUILD_DIR)/main.o
	$(CXX) $(CXXFLAGS) -o $@ $^

$(TESTS): $(COMMON_OBJECTS) $(BUILD_DIR)/tests.o
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BUILD_DIR)/cim_simulator.o: simulators/cim_simulator.cpp simulators/cim_simulator.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/trace_simulator.o: simulators/trace_simulator.cpp simulators/trace_simulator.hpp simulators/cim_simulator.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/main.o: simulators/main.cpp simulators/trace_simulator.hpp simulators/cim_simulator.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/tests.o: simulators/tests.cpp simulators/trace_simulator.hpp simulators/cim_simulator.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $@

$(RESULTS_DIR):
	mkdir -p $@

run: $(SIMULATOR) | $(RESULTS_DIR)
	./$(SIMULATOR)

test: $(TESTS)
	./$(TESTS)

clean:
	rm -rf $(BUILD_DIR)

clean-results:
	rm -f $(RESULTS_DIR)/*.json $(RESULTS_DIR)/*.csv
