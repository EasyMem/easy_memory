#ifndef TEST_UTILS_H
#define TEST_UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

/* 
 * Global test counters
*/
static int tests_total = 0;
static int tests_passed = 0;
static int tests_failed = 0;

/* 
 * Colors for output
*/
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_RESET   "\x1b[0m"

/*
 * Functions prototypes for testing
*/
void print_test_summary(void);
bool pointers_overlap(void *p1, size_t size1, void *p2, size_t size2);
void check_pointers_integrity(void **pointers, size_t *sizes, int count);
void fill_memory_pattern(void *ptr, size_t size, int pattern);
bool verify_memory_pattern(void *ptr, size_t size, int pattern);

/* 
 * Macro for checking a condition (full output)
*/
#define ASSERT(condition, message) do { \
    tests_total++; \
    if (condition) { \
        tests_passed++; \
        printf(ANSI_COLOR_GREEN "[PASS] " ANSI_COLOR_RESET "%s\n", message); \
    } else { \
        tests_failed++; \
        printf(ANSI_COLOR_RED "[FAIL] " ANSI_COLOR_RESET "%s\n", message); \
        printf("    at %s:%d\n", __FILE__, __LINE__); \
    } \
} while (0)

/* 
 * Macro for checking a condition (silent mode - messages only on error)
*/
#define ASSERT_QUIET(condition, message) do { \
    tests_total++; \
    if (condition) { \
        tests_passed++; \
    } else { \
        tests_failed++; \
        printf(ANSI_COLOR_RED "[FAIL] " ANSI_COLOR_RESET "%s\n", message); \
        printf("    at %s:%d\n", __FILE__, __LINE__); \
    } \
} while (0)

/* 
 * Macro for starting a new test
*/
#define TEST_CASE(name) do { \
    printf(ANSI_COLOR_BLUE "\n=== TEST CASE: %s ===\n" ANSI_COLOR_RESET, name); \
} while (0)

/* 
 * Macro for starting a new test phase
*/
#define TEST_PHASE(name) do { \
    printf(ANSI_COLOR_YELLOW "\n--- Phase: %s ---\n" ANSI_COLOR_RESET, name); \
} while (0)

/* 
 * Function for printing test results
*/
void print_test_summary(void) {
    printf("\n");
    printf(ANSI_COLOR_BLUE "=== Test Results ===\n" ANSI_COLOR_RESET);
    printf("Total tests: %d\n", tests_total);
    printf(ANSI_COLOR_GREEN "Passed: %d\n" ANSI_COLOR_RESET, tests_passed);
    
    if (tests_failed > 0) {
        printf(ANSI_COLOR_RED "Failed: %d\n" ANSI_COLOR_RESET, tests_failed);
    } else {
        printf("Failed: 0\n");
    }
    
    if (tests_failed == 0) {
        printf(ANSI_COLOR_GREEN "\nAll tests passed!\n" ANSI_COLOR_RESET);
    } else {
        printf(ANSI_COLOR_RED "\nSome tests failed!\n" ANSI_COLOR_RESET);
    }
}

/* 
 * Functions for checking the integrity of the arena
 * Checks that pointers do not overlap
*/
bool pointers_overlap(void *p1, size_t size1, void *p2, size_t size2) {
    char *c1 = (char*)p1;
    char *c2 = (char*)p2;
    return (c1 < (c2 + size2)) && (c2 < (c1 + size1));
}

/* 
 * Checking the integrity of the array of pointers
*/
void check_pointers_integrity(void **pointers, size_t *sizes, int count) {
    for (int i = 0; i < count; i++) {
        if (!pointers[i]) continue;
        
        for (int j = i + 1; j < count; j++) {
            if (!pointers[j]) continue;
            
            ASSERT_QUIET(!pointers_overlap(pointers[i], sizes[i], pointers[j], sizes[j]), 
                   "Allocated memory blocks should not overlap");
        }
    }
}

/* 
 * Filling the allocated block of memory with a test pattern
*/
void fill_memory_pattern(void *ptr, size_t size, int pattern) {
    memset(ptr, pattern & 0xFF, size);
}

/* 
 * Checking the preservation of the pattern in memory
*/
bool verify_memory_pattern(void *ptr, size_t size, int pattern) {
    unsigned char expected = (unsigned char)(pattern & 0xFF);
    unsigned char *bytes = (unsigned char*)ptr;
    
    for (size_t i = 0; i < size; i++) {
        if (bytes[i] != expected) {
            return false;
        }
    }
    
    return true;
}

#endif // TEST_UTILS_H 