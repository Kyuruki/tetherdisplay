# Offline build for the PORTABLE part of the capture module (adapter selection + cross-adapter policy)
# and its tests, runnable on Linux/WSL. The WGC/D3D11 capture and cross-adapter bridge are Windows-only
# and built via the project's CMake on Windows.
#
#   make test   build and run the portable capture unit tests

CXX      ?= g++
CXXSTD   := -std=c++20
WARN     := -Wall -Wextra -Wpedantic
OPT      := -O2 -g
INC      := -Iinclude

GTEST_DIR := /usr/src/googletest/googletest
GTEST_INC := -I$(GTEST_DIR)/include -I$(GTEST_DIR)
GTEST_SRC := $(GTEST_DIR)/src/gtest-all.cc $(GTEST_DIR)/src/gtest_main.cc

BUILD := build

.PHONY: test clean
$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/capture_test: src/adapter_select.cpp tests/capture_test.cpp | $(BUILD)
	$(CXX) $(CXXSTD) $(WARN) $(OPT) $(INC) $(GTEST_INC) \
	    src/adapter_select.cpp tests/capture_test.cpp $(GTEST_SRC) -lpthread -o $@

test: $(BUILD)/capture_test
	$(BUILD)/capture_test

clean:
	rm -rf $(BUILD)
