#pragma once

#include <cmath>
#include <iostream>

// Shared assertion helpers. Each suite resets the counter, runs its cases and
// returns how many failed; main sums them.

namespace tests {

inline int g_failures = 0;

inline void Check(bool cond, const char* name) {
    if (cond) {
        std::cout << "  [PASS] " << name << "\n";
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        ++g_failures;
    }
}

inline void CheckNear(float actual, float expected, float tolerance, const char* name) {
    const bool ok = fabsf(actual - expected) <= tolerance;
    if (!ok)
        std::cout << "         expected " << expected << ", got " << actual << "\n";
    Check(ok, name);
}

inline void CheckNear16(const float* actual, const float* expected, float tolerance,
                        const char* name) {
    for (int i = 0; i < 16; ++i) {
        if (fabsf(actual[i] - expected[i]) > tolerance) {
            std::cout << "         element " << i << ": expected " << expected[i] << ", got "
                      << actual[i] << "\n";
            Check(false, name);
            return;
        }
    }
    Check(true, name);
}

inline void Begin(const char* suite) {
    std::cout << "\n" << suite << "\n";
    g_failures = 0;
}

}  // namespace tests
