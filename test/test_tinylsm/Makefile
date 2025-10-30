# Makefile for running Tiny-LSM tests on host

CC = g++
STUBS_DIR = stubs
# Define macros needed for configuration.h (required when real config is included)
DEFINES = -DAPP_VERSION=\"test-1.0.0\" -DHW_VERSION=\"1.0\" -DHW_VENDOR=\"test\"
CFLAGS = -std=c++11 -Wall -Wextra -g $(DEFINES) -I$(STUBS_DIR) -I../../src -I.
# On Linux, clock_gettime requires -lrt; on macOS it's in the system library
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    LDFLAGS = -lrt
else
    LDFLAGS =
endif

# Unity test framework
UNITY_DIR = ../../.pio/libdeps/native/Unity/src
UNITY_SRC = $(UNITY_DIR)/unity.c
UNITY_INC = -I$(UNITY_DIR)

# LSM source files (needed for linking)
LSM_DIR = ../../src/libtinylsm
LSM_SRCS = \
	$(LSM_DIR)/tinylsm_utils.cpp \
	$(LSM_DIR)/tinylsm_memtable.cpp \
	$(LSM_DIR)/tinylsm_filter.cpp \
	$(LSM_DIR)/tinylsm_manifest.cpp \
	stubs/tinylsm_fs_stub.cpp

# Test source
TEST_SRC = test_main.cpp

# Output
TARGET = test_tinylsm

all: $(TARGET)
	@echo "✅ Tests compiled successfully!"
	@echo "Run with: ./$(TARGET)"

$(TARGET): $(TEST_SRC) $(LSM_SRCS) $(UNITY_SRC)
	$(CC) $(CFLAGS) $(UNITY_INC) -o $@ $^ $(LDFLAGS)

run: $(TARGET)
	@echo "🧪 Running Tiny-LSM tests..."
	@./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all run clean

help:
	@echo "Tiny-LSM Test Suite"
	@echo ""
	@echo "Usage:"
	@echo "  make          - Compile tests"
	@echo "  make run      - Compile and run tests"
	@echo "  make clean    - Remove compiled binary"
	@echo ""
	@echo "Alternative (PlatformIO):"
	@echo "  pio test -e native"

