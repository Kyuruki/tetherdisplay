# Offline build for the PORTABLE part of the encode module (rate-control planning + status) and its
# tests, runnable on Linux/WSL. The NVENC encoder is Windows-only and built via CMake on Windows.
#
#   make test   build and run the portable encode unit tests

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

$(BUILD)/encode_test: src/rate_control.cpp tests/encode_test.cpp | $(BUILD)
	$(CXX) $(CXXSTD) $(WARN) $(OPT) $(INC) $(GTEST_INC) \
	    src/rate_control.cpp tests/encode_test.cpp $(GTEST_SRC) -lpthread -o $@

test: $(BUILD)/encode_test
	$(BUILD)/encode_test

clean:
	rm -rf $(BUILD)
