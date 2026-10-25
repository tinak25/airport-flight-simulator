CXX      ?= g++
MPICXX   ?= mpicxx
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -pthread
INCLUDES := -Iinclude
HEADERS  := $(wildcard include/*.hpp)

BIN := bin

.PHONY: all threads mpi test check clean

all: threads mpi

threads: $(BIN)/sequential $(BIN)/concurrent
mpi: $(BIN)/distributed_mpi $(BIN)/hybrid

$(BIN):
	mkdir -p $(BIN)

$(BIN)/sequential: src/sequential.cpp $(HEADERS) | $(BIN)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

$(BIN)/concurrent: src/concurrent.cpp $(HEADERS) | $(BIN)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

$(BIN)/distributed_mpi: src/distributed_mpi.cpp $(HEADERS) | $(BIN)
	$(MPICXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

$(BIN)/hybrid: src/hybrid.cpp $(HEADERS) | $(BIN)
	$(MPICXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

$(BIN)/test_core: tests/test_core.cpp $(HEADERS) | $(BIN)
	$(CXX) $(CXXFLAGS) -UNDEBUG $(INCLUDES) $< -o $@

# Unit tests (no MPI needed)
test: $(BIN)/test_core
	./$(BIN)/test_core

# End-to-end: all four programs must print the same report
check: all test
	./tests/check_same_results.sh

clean:
	rm -rf $(BIN)
