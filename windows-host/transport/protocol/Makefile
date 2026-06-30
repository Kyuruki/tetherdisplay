# Offline build for the portable protocol module + its tests, usable on Linux/WSL with no
# network and no CMake. The canonical Windows build is CMakeLists.txt; this exists so the
# pure-C++ M0 logic can be compiled and unit-tested anywhere during development.
#
# Targets:
#   make test       build and run the GoogleTest suite
#   make loopback   build and run the in-process M0 loopback gate
#   make all        both
#   make clean

CXX      ?= g++
CXXSTD   := -std=c++20
WARN     := -Wall -Wextra -Wpedantic
OPT      := -O2 -g
INC      := -Iinclude
REPO     := ../../..
VECTORS  := $(REPO)/protocol/test-vectors

GTEST_DIR := /usr/src/googletest/googletest
GTEST_INC := -I$(GTEST_DIR)/include -I$(GTEST_DIR)
GTEST_SRC := $(GTEST_DIR)/src/gtest-all.cc $(GTEST_DIR)/src/gtest_main.cc

BUILD := build

.PHONY: all test loopback clean
all: test loopback

$(BUILD):
	mkdir -p $(BUILD)

# --- unit + golden-vector tests ---
$(BUILD)/wire_test: src/wire.cpp tests/wire_test.cpp | $(BUILD)
	$(CXX) $(CXXSTD) $(WARN) $(OPT) $(INC) $(GTEST_INC) \
	    -DTD_VECTORS_DIR='"$(abspath $(VECTORS))"' \
	    src/wire.cpp tests/wire_test.cpp $(GTEST_SRC) -lpthread -o $@

test: $(BUILD)/wire_test
	$(BUILD)/wire_test

# --- in-process loopback gate ---
$(BUILD)/loopback: src/wire.cpp $(REPO)/tools/loopback/main.cpp | $(BUILD)
	$(CXX) $(CXXSTD) $(WARN) $(OPT) $(INC) \
	    src/wire.cpp $(REPO)/tools/loopback/main.cpp -o $@

loopback: $(BUILD)/loopback
	$(BUILD)/loopback

clean:
	rm -rf $(BUILD)
