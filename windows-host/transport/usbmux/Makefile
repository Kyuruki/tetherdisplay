# Offline build for the PORTABLE usbmux framing + its tests (Linux/WSL, no network/CMake). The plist
# builders and the socket client land here too once the protocol is confirmed; the live transport is
# exercised against a mock daemon (also portable) and, on Windows, the real Apple Mobile Device Service.
#
#   make test   build and run the portable usbmux unit tests

CXX      ?= g++
CXXSTD   := -std=c++20
WARN     := -Wall -Wextra -Wpedantic
OPT      := -O2 -g
INC      := -Iinclude

GTEST_DIR := /usr/src/googletest/googletest
GTEST_INC := -I$(GTEST_DIR)/include -I$(GTEST_DIR)
GTEST_SRC := $(GTEST_DIR)/src/gtest-all.cc $(GTEST_DIR)/src/gtest_main.cc

BUILD := build
SRCS  := src/usbmux_protocol.cpp src/usbmux_messages.cpp src/usbmux_client.cpp
TESTS := tests/usbmux_protocol_test.cpp tests/usbmux_messages_test.cpp tests/usbmux_client_test.cpp

.PHONY: test clean
$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/usbmux_test: $(SRCS) $(TESTS) | $(BUILD)
	$(CXX) $(CXXSTD) $(WARN) $(OPT) $(INC) $(GTEST_INC) \
	    $(SRCS) $(TESTS) $(GTEST_SRC) -lpthread -o $@

test: $(BUILD)/usbmux_test
	$(BUILD)/usbmux_test

clean:
	rm -rf $(BUILD)
