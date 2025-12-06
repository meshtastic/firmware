# Makefile for Meshstatic Module - All Components
# BSD-3-Clause License

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2

# Component 1: Batch Manager
TARGET_BATCH = test_batch
SOURCES_BATCH = test_batch.c meshstatic_batch.c
HEADERS_BATCH = meshstatic_batch.h

# Component 2: Storage Manager
TARGET_STORAGE = test_storage
SOURCES_STORAGE = test_storage.c meshstatic_storage.c meshstatic_batch.c
HEADERS_STORAGE = meshstatic_storage.h meshstatic_batch.h

# Component 3: Core 1 Controller + Integration Test
TARGET_INTEGRATION = test_integration
SOURCES_INTEGRATION = test_integration.c meshstatic_core1.c meshstatic_storage.c meshstatic_batch.c
HEADERS_INTEGRATION = meshstatic_core1.h meshstatic_storage.h meshstatic_batch.h

.PHONY: all clean test test-batch test-storage test-integration run help

# Default target - build all components
all: $(TARGET_BATCH) $(TARGET_STORAGE) $(TARGET_INTEGRATION)

# Build Component 1 test program
$(TARGET_BATCH): $(SOURCES_BATCH) $(HEADERS_BATCH)
	$(CC) $(CFLAGS) -o $(TARGET_BATCH) $(SOURCES_BATCH)
	@echo "✓ Build complete: $(TARGET_BATCH) (Component 1)"

# Build Component 2 test program
$(TARGET_STORAGE): $(SOURCES_STORAGE) $(HEADERS_STORAGE)
	$(CC) $(CFLAGS) -o $(TARGET_STORAGE) $(SOURCES_STORAGE)
	@echo "✓ Build complete: $(TARGET_STORAGE) (Component 2)"

# Build Component 3 + Integration test
$(TARGET_INTEGRATION): $(SOURCES_INTEGRATION) $(HEADERS_INTEGRATION)
	$(CC) $(CFLAGS) -o $(TARGET_INTEGRATION) $(SOURCES_INTEGRATION)
	@echo "✓ Build complete: $(TARGET_INTEGRATION) (Component 3 + Integration)"

# Run Component 1 tests
test-batch: $(TARGET_BATCH)
	@echo "Running Component 1 tests..."
	./$(TARGET_BATCH)

# Run Component 2 tests
test-storage: $(TARGET_STORAGE)
	@echo "Running Component 2 tests..."
	./$(TARGET_STORAGE)

# Run Component 3 + Integration tests
test-integration: $(TARGET_INTEGRATION)
	@echo "Running Component 3 + Integration tests..."
	./$(TARGET_INTEGRATION)

# Run all tests
test: test-batch test-storage test-integration
	@echo ""
	@echo "=== All Component Tests Complete ==="

# Quick run (alias for test)
run: test

# Clean build artifacts
clean:
	rm -f $(TARGET_BATCH) $(TARGET_STORAGE) $(TARGET_INTEGRATION)
	rm -rf ./meshstatic
	@echo "✓ Clean complete"

# Help
help:
	@echo "Meshstatic Module - Build System"
	@echo ""
	@echo "Targets:"
	@echo "  make                 - Build all components"
	@echo "  make test            - Build and run all tests"
	@echo "  make test-batch      - Run Component 1 tests only"
	@echo "  make test-storage    - Run Component 2 tests only"
	@echo "  make test-integration- Run Component 3 + Integration tests"
	@echo "  make run             - Alias for 'make test'"
	@echo "  make clean           - Remove build artifacts"
	@echo "  make help            - Show this help message"
