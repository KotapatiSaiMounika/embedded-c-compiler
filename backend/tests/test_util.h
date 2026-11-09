#ifndef TEST_UTIL_H
#define TEST_UTIL_H

/* Minimal test helpers (no external framework). Include from ONE .c file per test binary. */

#include <stdio.h>
#include <string.h>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        g_checks++;                                                                   \
        if (!(cond)) {                                                                \
            g_failures++;                                                             \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                             \
    } while (0)

#define CHECK_INT(actual, expected)                                                   \
    do {                                                                              \
        long long a_ = (long long)(actual), e_ = (long long)(expected);               \
        g_checks++;                                                                   \
        if (a_ != e_) {                                                               \
            g_failures++;                                                             \
            fprintf(stderr, "  FAIL %s:%d: %s -> got %lld, expected %lld\n", __FILE__, \
                    __LINE__, #actual, a_, e_);                                       \
        }                                                                             \
    } while (0)

#define CHECK_STR(actual, expected)                                                   \
    do {                                                                              \
        const char *a_ = (actual), *e_ = (expected);                                  \
        g_checks++;                                                                   \
        if (strcmp(a_, e_) != 0) {                                                    \
            g_failures++;                                                             \
            fprintf(stderr, "  FAIL %s:%d:\n    got:      %s\n    expected: %s\n",      \
                    __FILE__, __LINE__, a_, e_);                                      \
        }                                                                             \
    } while (0)

#define CHECK_CONTAINS(haystack, needle)                                              \
    do {                                                                              \
        const char *h_ = (haystack), *n_ = (needle);                                  \
        g_checks++;                                                                   \
        if (strstr(h_, n_) == NULL) {                                                 \
            g_failures++;                                                             \
            fprintf(stderr, "  FAIL %s:%d:\n    \"%s\"\n    does not contain \"%s\"\n",  \
                    __FILE__, __LINE__, h_, n_);                                      \
        }                                                                             \
    } while (0)

#define RUN_TEST(fn)                                                                  \
    do {                                                                              \
        int before_ = g_failures;                                                     \
        fn();                                                                         \
        printf("[%s] %s\n", g_failures == before_ ? " OK " : "FAIL", #fn);            \
    } while (0)

#define TEST_SUMMARY()                                                                \
    (printf("\n%d checks, %d failed\n", g_checks, g_failures), g_failures ? 1 : 0)

#endif /* TEST_UTIL_H */
