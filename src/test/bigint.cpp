/*++
Module Name:

    bigint.cpp

Abstract:

    Regression tests for backend-independent bigint behavior.

--*/
#include "ast/reg_decl_plugins.h"
#include "ast/sls/sls_bv_evaluator.h"
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

static void check_bigint_shift(unsigned width, decl_kind op, char const* value,
                               char const* shift, char const* expected) {
    ast_manager m;
    reg_decl_plugins(m);
    bv_util bv(m);
    unsynch_mpz_manager mm;
    powers p(mm);
    scoped_mpz large(mm), result(mm), wanted(mm);
    mm.set(large, "340282366920938463463374607431768211456");
    mm.set(wanted, expected);
    app_ref x(m.mk_const("x", bv.mk_sort(width)), m);
    app_ref s(m.mk_const("s", bv.mk_sort(width)), m);
    app_ref term(m.mk_app(bv.get_family_id(), op, x, s), m);
    sls_tracker tracker(m, bv, mm, p);
    sls_evaluator eval(m, bv, tracker, mm, p);
    tracker.initialize(x);
    tracker.initialize(s);
    // Independently vary the storage history of the value and shift amount.
    for (bool computed_value : {false, true}) {
        auto& stored_value = tracker.get_value(x.get());
        mm.set(stored_value, value);
        if (computed_value) {
            mm.add(large, stored_value, stored_value);
            mm.sub(stored_value, large, stored_value);
        }
        for (bool computed_shift : {false, true}) {
            auto& stored_shift = tracker.get_value(s.get());
            mm.set(stored_shift, shift);
            if (computed_shift) {
                mm.add(large, stored_shift, stored_shift);
                mm.sub(stored_shift, large, stored_shift);
            }
            mm.set(result, large);
            eval(term, result);
            ENSURE(mm.eq(result, wanted));
        }
    }
}

static void tst_bigint_logical_shifts() {
    struct test_case {
        unsigned width;
        char const* value;
        char const* shift;
        char const* left;
        char const* right;
    };
    for (auto const& test : {
             test_case{64, "8", "0", "8", "8"},
             {64, "8", "1", "16", "4"},
             {64, "8", "63", "0", "0"},
             {64, "8", "64", "0", "0"},
             {64, "8", "18446744073709551615", "0", "0"},
             {64, "18446744073709551608", "0", "18446744073709551608", "18446744073709551608"},
             {64, "18446744073709551608", "1", "18446744073709551600", "9223372036854775804"},
             {64, "18446744073709551608", "63", "0", "1"},
             {64, "18446744073709551608", "64", "0", "0"},
             {64, "18446744073709551608", "18446744073709551615", "0", "0"},
             {96, "8", "18446744073709551616", "0", "0"},
             {96, "39614081257132168796771975168", "18446744073709551616", "0", "0"},
             {1, "1", "0", "1", "1"},
             {1, "1", "1", "0", "0"}}) {
        check_bigint_shift(test.width, OP_BSHL, test.value, test.shift, test.left);
        check_bigint_shift(test.width, OP_BLSHR, test.value, test.shift, test.right);
    }
    // Use native arithmetic as an independent oracle across small widths.
    for (unsigned width : {8u, 31u, 32u}) {
        uint64_t mask = (uint64_t(1) << width) - 1;
        for (uint64_t value : {uint64_t(0), uint64_t(1), mask / 2, mask / 2 + 1, mask}) {
            for (unsigned shift : {0u, 1u, width - 1, width, width + 1}) {
                uint64_t left = shift >= width ? 0 : (value << shift) & mask;
                uint64_t right = shift >= width ? 0 : value >> shift;
                check_bigint_shift(width, OP_BSHL, std::to_string(value).c_str(),
                                  std::to_string(shift).c_str(), std::to_string(left).c_str());
                check_bigint_shift(width, OP_BLSHR, std::to_string(value).c_str(),
                                  std::to_string(shift).c_str(), std::to_string(right).c_str());
            }
        }
    }
}

static void tst_bigint_arithmetic_shifts() {
    struct test_case { unsigned width; char const* value; char const* shift; char const* result; };
    for (auto const& test : {
             test_case{64, "8", "0", "8"}, {64, "8", "1", "4"},
             {64, "8", "63", "0"}, {64, "8", "64", "0"},
             {64, "8", "18446744073709551615", "0"},
             {64, "18446744073709551608", "0", "18446744073709551608"},
             {64, "18446744073709551608", "1", "18446744073709551612"},
             {64, "18446744073709551608", "63", "18446744073709551615"},
             {64, "18446744073709551608", "64", "18446744073709551615"},
             {64, "18446744073709551608", "18446744073709551615", "18446744073709551615"},
             {96, "8", "18446744073709551616", "0"},
             {96, "39614081257132168796771975168", "18446744073709551616", "79228162514264337593543950335"},
             {1, "1", "0", "1"}, {1, "1", "1", "1"}}) {
        check_bigint_shift(test.width, OP_BASHR, test.value, test.shift, test.result);
    }
    for (unsigned width : {8u, 31u, 32u}) {
        int64_t modulus = int64_t(1) << width;
        for (int64_t value : {-modulus / 2, int64_t(-3), int64_t(-1), int64_t(0), int64_t(1), modulus / 2 - 1}) {
            for (unsigned shift : {0u, 1u, width - 1, width, width + 1}) {
                // Arithmetic shift rounds signed division toward negative infinity.
                int64_t divisor = int64_t(1) << shift;
                int64_t expected = value / divisor - (value % divisor < 0 ? 1 : 0);
                check_bigint_shift(width, OP_BASHR, std::to_string((value + modulus) % modulus).c_str(),
                                  std::to_string(shift).c_str(), std::to_string((expected + modulus) % modulus).c_str());
            }
        }
    }
}

void tst_bigint() {
    tst_bigint_size();
    tst_bigint_logical_shifts();
    tst_bigint_arithmetic_shifts();
}
