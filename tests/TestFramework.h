#pragma once

// Deliberately not a real test framework (no Catch2/GTest dependency yet --
// not worth pulling in for ~10 checks). Swap for Catch2 once the physics API
// stabilizes across Stage 2+ and the suite grows past this scale.

#include <cmath>
#include <cstdio>
#include <string>

namespace TestFramework {

inline int& FailureCount() {
    static int count = 0;
    return count;
}

inline void ExpectNear(double actual, double expected, double tolerance, const std::string& what) {
    if (std::abs(actual - expected) > tolerance) {
        std::printf("[FAIL] %s: expected %.10g, got %.10g (tolerance %.3g)\n",
                     what.c_str(), expected, actual, tolerance);
        ++FailureCount();
    } else {
        std::printf("[ OK ] %s (%.10g ~= %.10g)\n", what.c_str(), actual, expected);
    }
}

inline void ExpectTrue(bool condition, const std::string& what) {
    if (!condition) {
        std::printf("[FAIL] %s\n", what.c_str());
        ++FailureCount();
    } else {
        std::printf("[ OK ] %s\n", what.c_str());
    }
}

} // namespace TestFramework
