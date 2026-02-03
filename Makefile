# Makefile for compiling and running easy_memory allocator tests


UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

ASAN_OPTS = allocator_may_return_null=1:detect_stack_use_after_return=1
SAN_FLAGS = -fsanitize=address,undefined

ASAN_OPTS = allocator_may_return_null=1:detect_stack_use_after_return=1
LSAN_RUN_FIX = 

ifeq ($(UNAME_S), Linux)
    ifeq ($(UNAME_M), x86_64)
        SAN_FLAGS += -fsanitize=leak
        ASAN_OPTS := $(ASAN_OPTS):detect_leaks=1
    else
        LSAN_RUN_FIX = ASAN_OPTIONS="$(ASAN_OPTS):detect_leaks=0" LSAN_OPTIONS="detect_leaks=0"
    endif
endif

ifneq (,$(filter MINGW% MSYS%,$(UNAME_S)))
    SAN_FLAGS = 
endif

CC ?= clang
STD_C ?= c99
CFLAGS = -Werror -Wall -Wextra \
	     -Wshadow \
		 -Wconversion -Wsign-conversion \
		 -Wundef \
		 -Wstrict-aliasing=2 \
		 -Wpointer-arith \
		 -Wdouble-promotion \
		 -Wcast-align \
		 -Wcast-qual \
		 -Wmissing-declarations \
		 -Wmissing-prototypes \
		 -Wstrict-prototypes \
		 -Wpadded \
		 -Wint-to-pointer-cast \
		 -Wpointer-to-int-cast \
		 -W -std=$(STD_C) \
		 -g3 \
		 -fno-omit-frame-pointer \
		 -fno-sanitize-recover=all \
		 -I.
DEBUG_FLAGS = -DDEBUG # Debug flag
COV_FLAGS = -O0 -fprofile-arcs -ftest-coverage # Coverage flags
LDFLAGS_COV = -lgcov # Linker flag for coverage

export UBSAN_OPTIONS=halt_on_error=0:exitcode=1:print_stacktrace=1
export ASAN_OPTIONS=$(ASAN_OPTS)
export LSAN_OPTIONS=detect_leaks=0

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
	$(CC) $(CFLAGS) $(SAN_FLAGS) $< -o $@

# Compilation of each test with debug information
$(TEST_DIR)/%_debug: $(TEST_DIR)/%.c easy_memory.h $(TEST_DIR)/test_utils.h
	$(CC) $(CFLAGS) $(DEBUG_FLAGS) $(SAN_FLAGS) $< -o $@
# Fallback test to ensure generic min_exponent_of implementation works
test_fallback:
	$(CC) $(CFLAGS) $(SAN_FLAGS) -DEM_FORCE_GENERIC tests/validation_test.c -o test_fallback
	./test_fallback

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
	@printf "\n--- Running $< (debug mode) ---\n"
	@./$<
	@if [ $$? -ne 0 ]; then \
		printf "\nTest $< FAILED!\n"; \
		exit 1; \
	else \
		printf "\nTest $< PASSED!\n"; \
	fi

# Compilation of all tests without debug
build_silent: $(TEST_SRCS:%.c=%_silent)

# Compilation of all tests with debug information
build_debug: $(TEST_SRCS:%.c=%_debug)

# Compilation of all tests with coverage information (depends on executables)
build_coverage: $(TEST_COV_BINS)

# Memory leak check using valgrind
valgrind: clean
	@printf "Running valgrind memory check on all tests...\n"
	@$(MAKE) build_silent SAN_FLAGS="" CFLAGS="$(CFLAGS) -D__valgrind__"
	@for test in $(TEST_SRCS:%.c=%_silent) ; do \
		printf "\n--- Checking $$test ---\n" ; \
		valgrind --error-exitcode=1 --leak-check=full --show-leak-kinds=all --track-origins=yes ./$$test ; \
	done
	@printf "\nAll memory checks completed.\n"

# Testing: run all tests without debug info
tests: build_silent
	@printf "Running all tests (normal mode)...\n"
	@for test in $(TEST_SRCS:%.c=%_silent) ; do \
		printf "\n--- Running $$test ---\n" ; \
		$(LSAN_RUN_FIX) ./$$test ; \
		if [ $$? -ne 0 ]; then \
			printf "\nTest $$test FAILED with exit code $$?\n"; \
			exit_code=1; \
		fi; \
	done; \
	if [ "$$exit_code" = "1" ]; then \
		printf "\nSome tests FAILED!\n"; \
		exit 1; \
	else \
		printf "\nAll tests PASSED!\n"; \
	fi

# Testing: run all tests with debug info
tests_full: build_debug
	@printf "Running all tests (debug mode)...\n"
	@for test in $(TEST_SRCS:%.c=%_debug) ; do \
		printf "\n--- Running $$test ---\n" ; \
		$(LSAN_RUN_FIX) ./$$test ; \
		if [ $$? -ne 0 ]; then \
			printf "\nTest $$test FAILED with exit code $$?\n"; \
			exit_code=1; \
		fi; \
	done; \
	if [ "$$exit_code" = "1" ]; then \
		printf "\nSome tests FAILED!\n"; \
		exit 1; \
	else \
		printf "\nAll tests PASSED!\n"; \
	fi

# Coverage: build with coverage flags and run tests to generate .gcda/.gcno files
coverage: clean build_coverage
	@printf "Running all tests to generate coverage data...\n"
	@exit_code=0; \
	for test in $(TEST_COV_BINS) ; do \
		printf "\n--- Running $$test (for coverage) ---\n" ; \
		./$$test ; \
		if [ $$? -ne 0 ]; then \
			printf "\nTest $$test FAILED with exit code $$?\n"; \
			exit_code=1; \
		fi; \
	done; \
	if [ "$$exit_code" = "1" ]; then \
		printf "\nSome tests FAILED! Coverage data generation might be incomplete.\n"; \
		exit 1; \
	else \
		printf "\nCoverage data generated successfully from all tests.\n"; \
	fi
	
	@printf "\nGenerating final coverage.info for Codecov...\n"
	lcov --capture --directory . --output-file coverage.info

	@printf "Filtering system files from the report...\n"
	lcov --remove coverage.info '/usr/*' '*/test_utils.h' --output-file coverage.info --ignore-errors unused

	rm -f $(TEST_COV_BINS)
	rm -f $(TEST_DIR)/*.gcda $(TEST_DIR)/*.gcno
	@rm -f coverage_base.info coverage_run.info coverage_total.info

	@printf "Successfully generated final coverage.info for Codecov.\n"


# Cleaning binary files and coverage files
clean:
	rm -f $(TEST_SRCS:%.c=%_silent) $(TEST_SRCS:%.c=%_debug) $(TEST_COV_BINS)
	rm -f $(TEST_DIR)/*.o $(TEST_DIR)/*.cov.o # Clean object files
	rm -f *.gcov # Clean root gcov files if any generated manually
	rm -f $(TEST_DIR)/*.gcda $(TEST_DIR)/*.gcno # Clean coverage data files
	rm -f coverage.info

# Show available tests
list:
	@printf "Available commands:\n"
	@printf "  make tests      - run all tests without debug output \n"
	@printf "  make tests_full - run all tests with debug output\n"
	@printf "  make coverage   - build & run tests to generate coverage data for CodeCov\n"
	@printf "\nAvailable individual tests (always with debug output):\n"
	@for test in $(TEST_SRCS) ; do \
		basename=$$(basename $${test%.c} _test); \
		printf "  make test_$$basename\n" ; \
	done