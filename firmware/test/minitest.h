/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#ifndef MINITEST_H
#define MINITEST_H
#include <stdio.h>
#include <string.h>

static int minitest_fail_count = 0;
static int minitest_run_count = 0;

#define TEST(name) static void name(void)

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            minitest_fail_count++;                                         \
        }                                                                  \
    } while (0)

#define CHECK_EQ_INT(a, b)                                                 \
    do {                                                                   \
        long long minitest_a = (long long)(a);                             \
        long long minitest_b = (long long)(b);                             \
        if (minitest_a != minitest_b) {                                    \
            printf("%s:%d: CHECK_EQ_INT failed: %s (%lld) != %s (%lld)\n", \
                   __FILE__, __LINE__, #a, minitest_a, #b, minitest_b);    \
            minitest_fail_count++;                                         \
        }                                                                  \
    } while (0)

#define CHECK_EQ_STR(a, b)                                                 \
    do {                                                                   \
        const char *minitest_sa = (a);                                     \
        const char *minitest_sb = (b);                                     \
        if (minitest_sa == NULL || minitest_sb == NULL ||                  \
            strcmp(minitest_sa, minitest_sb) != 0) {                       \
            printf("%s:%d: CHECK_EQ_STR failed: %s (\"%s\") != %s (\"%s\")\n", \
                   __FILE__, __LINE__, #a, minitest_sa ? minitest_sa : "(null)", \
                   #b, minitest_sb ? minitest_sb : "(null)");              \
            minitest_fail_count++;                                         \
        }                                                                  \
    } while (0)

#define RUN(name)                                                          \
    do {                                                                   \
        minitest_run_count++;                                              \
        name();                                                            \
    } while (0)

static inline int minitest_summary(void)
{
    if (minitest_fail_count != 0) {
        printf("FAILED: %d check(s) failed in %d test(s)\n", minitest_fail_count, minitest_run_count);
        return 1;
    }
    printf("OK: %d test(s) passed\n", minitest_run_count);
    return 0;
}

#endif
