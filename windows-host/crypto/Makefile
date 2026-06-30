# Offline build for the crypto module + its tests against libsodium (Linux/WSL). SODIUM points at a
# libsodium dev tree (headers + a libsodium.so); on a box without libsodium-dev, stage one as the agent
# did: `apt-get download libsodium-dev`, `dpkg-deb -x` it, and symlink the runtime .so. On Windows the
# module builds via CMake against the installed libsodium SDK.
#
#   make test                       build and run the crypto unit tests
#   make test SODIUM=/usr           if libsodium-dev is installed system-wide

CXX      ?= g++
CXXSTD   := -std=c++20
WARN     := -Wall -Wextra -Wpedantic
OPT      := -O2 -g
SODIUM   ?= $(HOME)/.sodium-dev
INC      := -Iinclude -I../core/include -I$(SODIUM)/include
SODIUM_LD := -L$(SODIUM) -lsodium
RUN_ENV  := LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu:$(SODIUM)

GTEST_DIR := /usr/src/googletest/googletest
GTEST_INC := -I$(GTEST_DIR)/include -I$(GTEST_DIR)
GTEST_SRC := $(GTEST_DIR)/src/gtest-all.cc $(GTEST_DIR)/src/gtest_main.cc

BUILD := build
SRCS  := src/secure_channel.cpp src/identity_store.cpp src/secure_byte_channel.cpp
TESTS := tests/secure_channel_test.cpp tests/secure_byte_channel_test.cpp

.PHONY: test clean
$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/crypto_test: $(SRCS) $(TESTS) | $(BUILD)
	$(CXX) $(CXXSTD) $(WARN) $(OPT) $(INC) $(GTEST_INC) \
	    $(SRCS) $(TESTS) $(GTEST_SRC) $(SODIUM_LD) -lpthread -o $@

test: $(BUILD)/crypto_test
	$(RUN_ENV) $(BUILD)/crypto_test

clean:
	rm -rf $(BUILD)
