# Offline build for the PORTABLE core logic (control plane) + tests (Linux/WSL). The Win32 streaming
# session is built via CMake on Windows. Pulls in the portable protocol codec for proto::Message.
#
#   make test   build and run the portable core unit tests

CXX      ?= g++
CXXSTD   := -std=c++20
WARN     := -Wall -Wextra -Wpedantic
OPT      := -O2 -g
PROTO    := ../transport/protocol
INC      := -Iinclude -I$(PROTO)/include -I../capture/include -I../encode/include

GTEST_DIR := /usr/src/googletest/googletest
GTEST_INC := -I$(GTEST_DIR)/include -I$(GTEST_DIR)
GTEST_SRC := $(GTEST_DIR)/src/gtest-all.cc $(GTEST_DIR)/src/gtest_main.cc

BUILD := build
SRCS  := src/control_plane.cpp src/streaming_session.cpp $(PROTO)/src/wire.cpp
TESTS := tests/control_plane_test.cpp tests/streaming_session_test.cpp

.PHONY: test clean
$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/core_test: $(SRCS) $(TESTS) | $(BUILD)
	$(CXX) $(CXXSTD) $(WARN) $(OPT) $(INC) $(GTEST_INC) \
	    $(SRCS) $(TESTS) $(GTEST_SRC) -lpthread -o $@

test: $(BUILD)/core_test
	$(BUILD)/core_test

clean:
	rm -rf $(BUILD)
