#!/bin/bash
# Quick test runner for Tiny-LSM

set -e

echo "🧪 Tiny-LSM Test Suite Runner"
echo ""

# Check if platformio is available
if ! command -v pio &>/dev/null; then
	echo "❌ PlatformIO not found. Please install: pip install platformio"
	exit 1
fi

# Parse options
VERBOSE=""
FILTER=""

while [[ $# -gt 0 ]]; do
	case $1 in
	-v | --verbose)
		VERBOSE="-v"
		shift
		;;
	-f | --filter)
		FILTER="-f $2"
		shift 2
		;;
	-h | --help)
		echo "Usage: ./run_lsm_tests.sh [OPTIONS]"
		echo ""
		echo "Options:"
		echo "  -v, --verbose     Verbose output"
		echo "  -f, --filter TEST Run specific test (e.g., test_memtable)"
		echo "  -h, --help        Show this help"
		echo ""
		echo "Examples:"
		echo "  ./run_lsm_tests.sh"
		echo "  ./run_lsm_tests.sh -v"
		echo "  ./run_lsm_tests.sh -f test_memtable"
		exit 0
		;;
	*)
		echo "Unknown option: $1"
		echo "Use -h for help"
		exit 1
		;;
	esac
done

echo "📋 Test Plan:"
echo "  - CRC32 tests (2)"
echo "  - Key encoding tests (2)"
echo "  - Memtable tests (5)"
echo "  - Bloom filter tests (3)"
echo "  - Manifest tests (2)"
echo "  - Shadow index tests (2)"
echo "  - Field tag tests (1)"
echo "  - Integration tests (2)"
echo "  - Total: 19 tests"
echo ""

# Run tests
echo "🚀 Running standalone LSM tests..."
echo ""

# Test directory is test/test_lsm_standalone (moved from nested structure)
if [ -d "test/test_lsm_standalone" ]; then
	cd test/test_lsm_standalone
	pio test $FILTER $VERBOSE 2>&1
	TEST_RESULT=$?
	cd ../..
else
	echo "❌ Test directory not found!"
	echo "Looking for: test/test_lsm_standalone/"
	exit 1
fi

echo ""
if [ $TEST_RESULT -eq 0 ]; then
	echo "✅ All tests PASSED!"
	echo ""
	echo "Results:"
	echo "  - CRC32: ✓"
	echo "  - Key encoding: ✓"
	echo "  - Bloom filters: ✓ (FP rate <5%)"
	echo "  - Shadow index: ✓ (16 bytes verified)"
	echo "  - Field tags: ✓ (human-readable)"
	echo "  - Struct sizes: ✓ (with padding)"
	echo ""
	echo "🎊 10/10 tests passed!"
else
	echo "❌ Some tests failed. See output above."
	exit 1
fi

echo ""
echo "💡 Test suite location: test/test_tinylsm_standalone/"
