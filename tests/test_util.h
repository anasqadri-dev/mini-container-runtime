#ifndef MCR_TEST_UTIL_H
#define MCR_TEST_UTIL_H

/* Tiny test helper so we do not need an external framework. */

#include <stdio.h>
#include <string.h>

static int tests_run;
static int tests_failed;

#define CHECK(cond)                                                          \
    do {                                                                     \
        tests_run++;                                                         \
        if (!(cond)) {                                                       \
            tests_failed++;                                                  \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);\
        }                                                                    \
    } while (0)

#define CHECK_STR(actual, expected) CHECK(strcmp((actual), (expected)) == 0)

#define TEST_SUMMARY()                                                       \
    do {                                                                     \
        printf("%d checks, %d failed\n", tests_run, tests_failed);           \
        return tests_failed ? 1 : 0;                                         \
    } while (0)

#endif /* MCR_TEST_UTIL_H */
