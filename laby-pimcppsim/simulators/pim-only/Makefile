CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic

SIMULATOR = cim_sim
TESTS = cim_tests
COMMON = cim_simulator.o

.PHONY: all run test clean

all: $(SIMULATOR) $(TESTS)

$(SIMULATOR): $(COMMON) main.o
	$(CXX) $(CXXFLAGS) -o $@ $^

$(TESTS): $(COMMON) tests.o
	$(CXX) $(CXXFLAGS) -o $@ $^

cim_simulator.o: cim_simulator.cpp cim_simulator.hpp
	$(CXX) $(CXXFLAGS) -c -o $@ cim_simulator.cpp

main.o: main.cpp cim_simulator.hpp
	$(CXX) $(CXXFLAGS) -c -o $@ main.cpp

tests.o: tests.cpp cim_simulator.hpp
	$(CXX) $(CXXFLAGS) -c -o $@ tests.cpp

run: $(SIMULATOR)
	./$(SIMULATOR)

test: $(TESTS)
	./$(TESTS)

clean:
	rm -f $(SIMULATOR) $(TESTS) *.o
