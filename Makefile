# Makefile for compiling and running easy_memory allocator tests

CC ?= clang
STD_C ?= c99
CFLAGS = -Wall -Wextra -std=$(STD_C) -g -I.
DEBUG_FLAGS = -DDEBUG # Debug flag
COV_FLAGS = -O0 -fprofile-arcs -ftest-coverage # Coverage flags
LDFLAGS_COV = -lgcov # Linker flag for coverage

TEST_DIR = tests
TEST_SRCS = $(wildcard $(TEST_DIR)/*.c)
# Generate names for coverage object files
TEST_COV_OBJS = $(TEST_SRCS:$(TEST_DIR)/%.c=$(TEST_DIR)/%.cov.o)
# Generate names for coverage executables
TEST_COV_BINS = $(TEST_SRCS:$(TEST_DIR)/%.c=$(TEST_DIR)/%_coverage)

# Define the primary source file to check coverage for.
# Adjust if your implementation is in a .c file.
COVERAGE_SRC = easy_memory.h

.PHONY: all clean run tests tests_full list coverage build_coverage

# Default goal: show available commands
all: clean list

# Compilation of each test without debug information
$(TEST_DIR)/%_silent: $(TEST_DIR)/%.c easy_memory.h $(TEST_DIR)/test_utils.h
	$(CC) $(CFLAGS) $< -o $@

# Compilation of each test with debug information
$(TEST_DIR)/%_debug: $(TEST_DIR)/%.c easy_memory.h $(TEST_DIR)/test_utils.h
	$(CC) $(CFLAGS) $(DEBUG_FLAGS) $< -o $@

# --- Coverage Build Steps ---
# 1. Compile source files into object files with coverage flags
#    This generates the .gcno files alongside the object files.
$(TEST_DIR)/%.cov.o: $(TEST_DIR)/%.c easy_memory.h $(TEST_DIR)/test_utils.h
	$(CC) $(CFLAGS) $(COV_FLAGS) -c $< -o $@

# 2. Link object files into executables
#    We still need COV_FLAGS during linking for gcov to work correctly.
$(TEST_DIR)/%_coverage: $(TEST_DIR)/%.cov.o
	$(CC) $(CFLAGS) $(COV_FLAGS) $^ $(LDFLAGS_COV) -o $@
# --- End Coverage Build Steps ---

# Pattern rule for running individual tests (always with debug)
test_%: $(TEST_DIR)/%_test_debug
	@echo "\n--- Running $< (debug mode) ---"
	@./$<
	@if [ $$? -ne 0 ]; then \
		echo "\nTest $< FAILED!"; \
		exit 1; \
	else \
		echo "\nTest $< PASSED!"; \
	fi

# Compilation of all tests without debug
build_silent: $(TEST_SRCS:%.c=%_silent)

# Compilation of all tests with debug information
build_debug: $(TEST_SRCS:%.c=%_debug)

# Compilation of all tests with coverage information (depends on executables)
build_coverage: $(TEST_COV_BINS)

# Memory leak check using valgrind
valgrind: build_silent
	@echo "Running valgrind memory check on all tests..."
	@for test in $(TEST_SRCS:%.c=%_silent) ; do \
		echo "\n--- Checking $$test ---" ; \
		valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$$test ; \
	done
	@echo "\nAll memory checks completed."

# Testing: run all tests without debug info
tests: build_silent
	@echo "Running all tests (normal mode)..."
	@for test in $(TEST_SRCS:%.c=%_silent) ; do \
		echo "\n--- Running $$test ---" ; \
		./$$test ; \
		if [ $$? -ne 0 ]; then \
			echo "\nTest $$test FAILED with exit code $$?"; \
			exit_code=1; \
		fi; \
	done; \
	if [ "$$exit_code" = "1" ]; then \
		echo "\nSome tests FAILED!"; \
		exit 1; \
	else \
		echo "\nAll tests PASSED!"; \
	fi

# Testing: run all tests with debug info
tests_full: build_debug
	@echo "Running all tests (debug mode)..."
	@for test in $(TEST_SRCS:%.c=%_debug) ; do \
		echo "\n--- Running $$test ---" ; \
		./$$test ; \
		if [ $$? -ne 0 ]; then \
			echo "\nTest $$test FAILED with exit code $$?"; \
			exit_code=1; \
		fi; \
	done; \
	if [ "$$exit_code" = "1" ]; then \
		echo "\nSome tests FAILED!"; \
		exit 1; \
	else \
		echo "\nAll tests PASSED!"; \
	fi

# Coverage: build with coverage flags and run tests to generate .gcda/.gcno files
coverage: clean build_coverage
	@echo "Running all tests to generate coverage data..."
	@exit_code=0; \
	for test in $(TEST_COV_BINS) ; do \
		echo "\n--- Running $$test (for coverage) ---" ; \
		./$$test ; \
		if [ $$? -ne 0 ]; then \
			echo "\nTest $$test FAILED with exit code $$?"; \
			exit_code=1; \
		fi; \
	done; \
	if [ "$$exit_code" = "1" ]; then \
		echo "\nSome tests FAILED! Coverage data generation might be incomplete."; \
		exit 1; \
	else \
		echo "\nCoverage data generated successfully from all tests."; \
	fi
	
	@echo "\nGenerating final coverage.info for Codecov..."
	lcov --capture --directory . --output-file coverage.info

	@echo "Filtering system files from the report..."
	lcov --remove coverage.info '/usr/*' '*/test_utils.h' --output-file coverage.info --ignore-errors unused

	rm -f $(TEST_COV_BINS)
	rm -f $(TEST_DIR)/*.gcda $(TEST_DIR)/*.gcno
	@rm -f coverage_base.info coverage_run.info coverage_total.info

	@echo "Successfully generated final coverage.info for Codecov."


# Cleaning binary files and coverage files
clean:
	rm -f $(TEST_SRCS:%.c=%_silent) $(TEST_SRCS:%.c=%_debug) $(TEST_COV_BINS)
	rm -f $(TEST_DIR)/*.o $(TEST_DIR)/*.cov.o # Clean object files
	rm -f *.gcov # Clean root gcov files if any generated manually
	rm -f $(TEST_DIR)/*.gcda $(TEST_DIR)/*.gcno # Clean coverage data files
	rm -f coverage.info

# Show available tests
list:
	@echo "Available commands:"
	@echo "  make tests      - run all tests without debug output"
	@echo "  make tests_full - run all tests with debug output"
	@echo "  make coverage   - build & run tests to generate coverage data for CodeCov"
	@echo "\nAvailable individual tests (always with debug output):"
	@for test in $(TEST_SRCS) ; do \
		basename=$$(basename $${test%.c} _test); \
		echo "  make test_$$basename" ; \
	done