/*++
Module Name:

    bigint.cpp

Abstract:

    Regression tests for backend-independent bigint behavior.

--*/
#include "util/rational.h"

static void tst_bigint_size() {
    struct test_case { char const* value; unsigned size; };
    rational large("340282366920938463463374607431768211456");
    for (auto const& test : {
             test_case{"0", 1}, {"1", 1}, {"-1", 1},
             {"2147483647", 1}, {"-2147483648", 1},
             {"2147483648", 2}, {"-2147483649", 2}, {"4294967295", 2},
             {"4294967296", 3}, {"-4294967296", 3},
             {"18446744073709551615", 3}, {"-18446744073709551615", 3},
             {"18446744073709551616", 4}, {"-18446744073709551616", 4},
             {"79228162514264337593543950335", 4},
             {"79228162514264337593543950336", 5}}) {
        rational value(test.value);
        // Include the denominator's one unit, and exercise GMP's retained storage.
        ENSURE(value.storage_size() == test.size + 1);
        rational computed = (large + value) - large;
        ENSURE(computed == value);
        ENSURE(computed.storage_size() == value.storage_size());
        if (!value.is_zero()) {
            rational reciprocal = rational(1) / value;
            // A negative rational keeps its sign in the numerator.
            unsigned denominator_size = value == rational(INT_MIN) ? 2 : test.size;
            ENSURE(reciprocal.storage_size() == denominator_size + 1);
            ENSURE((rational(1) / computed).storage_size() == reciprocal.storage_size());
        }
    }
    rational third = (large / rational(3)) / large;
    ENSURE(third == rational(1, 3));
    ENSURE(third.storage_size() == 2);
}

void tst_bigint() {
    tst_bigint_size();
}
