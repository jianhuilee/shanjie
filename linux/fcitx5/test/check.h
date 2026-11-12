// Minimal test helper. R2: a failure prints the condition's source text only, never values.
#pragma once
#include <cstdio>

inline int g_failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

// Fails the whole test with a message when a required data file is missing.
#define REQUIRE_FILE(path)                                                       \
    do {                                                                         \
        if (FILE *f_ = std::fopen((path), "rb")) {                               \
            std::fclose(f_);                                                     \
        } else {                                                                 \
            std::fprintf(stderr, "missing data file; see CONTRIBUTING.md for the model download commands\n"); \
            return 2;                                                            \
        }                                                                        \
    } while (0)

#define TEST_MAIN_END() return g_failures == 0 ? 0 : 1
