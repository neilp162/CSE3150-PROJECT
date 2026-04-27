CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic
TARGET := bgp_simulator
SRC := src/main.cpp
WASM_NODE ?= /mnt/c/Program\ Files/nodejs/node.exe

.PHONY: all clean test wasm wasm-test

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -Iinclude $(SRC) -o $(TARGET)

test: $(TARGET)
	bash tests/run_tests.sh

wasm:
	bash web/build_wasm.sh

wasm-test:
	$(WASM_NODE) tests/test_wasm.mjs

clean:
	rm -f $(TARGET) ribs.csv
