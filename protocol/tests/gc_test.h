/* Minimal dependency-free unit test helpers. SPDX-License-Identifier: MIT */
#ifndef GC_TEST_H
#define GC_TEST_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int gct_failed = 0;
static int gct_checks = 0;

#define CHECK(cond) do { gct_checks++; if (!(cond)) { gct_failed++; \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { long long _a = (long long)(a), _b = (long long)(b); gct_checks++; \
    if (_a != _b) { gct_failed++; fprintf(stderr, "%s:%d: CHECK_EQ failed: %s == %s (%lld != %lld)\n", \
    __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)
#define RUN(fn) do { int _f = gct_failed; fn(); printf("%-40s %s\n", #fn, _f == gct_failed ? "ok" : "FAILED"); } while (0)
#define TEST_MAIN_END() do { printf("%d checks, %d failed\n", gct_checks, gct_failed); return gct_failed ? 1 : 0; } while (0)

/* deterministic PRNG for reproducible fuzzing */
static unsigned int gct_seed = 12345u;
#if defined(__GNUC__)
__attribute__((unused))
#endif
static unsigned int gct_rand(void) { gct_seed = gct_seed * 1103515245u + 12345u; return (gct_seed >> 8) & 0xFFFFFF; }
#endif
