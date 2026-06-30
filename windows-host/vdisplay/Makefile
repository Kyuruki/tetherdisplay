# Offline build for the PORTABLE part of the vdisplay module (mode selection + protocol helpers)
# and its tests, runnable on Linux/WSL with no network and no CMake. The Win32 driver-control code
# (src/parsec_vdd.cpp) and the sandbox are Windows-only and built via the project's CMake on Windows.
#
#   make test   build and run the portable vdisplay unit tests

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

$(BUILD)/vdisplay_test: src/mode_select.cpp tests/vdisplay_test.cpp | $(BUILD)
	$(CXX) $(CXXSTD) $(WARN) $(OPT) $(INC) $(GTEST_INC) \
	    src/mode_select.cpp tests/vdisplay_test.cpp $(GTEST_SRC) -lpthread -o $@

test: $(BUILD)/vdisplay_test
	$(BUILD)/vdisplay_test

clean:
	rm -rf $(BUILD)
