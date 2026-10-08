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

static void tst_bigint_power() {
    unsynch_mpz_manager m;
    scoped_mpz base(m), result(m), large(m), expected(m);
    m.set(large, "4294967296");
    for (int value : {-1, 0, 1}) {
        for (unsigned exponent : {0u, 1u, 2u, 63u, 64u, static_cast<unsigned>(INT_MAX),
                                  static_cast<unsigned>(INT_MAX) + 1, UINT_MAX - 1, UINT_MAX}) {
            if (value == 0 && exponent == 0)
                continue;
            int wanted = exponent == 0 ? 1 : value == -1 && exponent % 2 == 0 ? 1 : value;
            for (bool computed : {false, true}) {
                m.set(base, value);
                if (computed) {
                    m.add(base, large, base);
                    m.sub(base, large, base);
                }
                m.power(base, exponent, result);
                ENSURE(m.eq(result, mpz(wanted)));
                m.power(base, exponent, base);
                ENSURE(m.eq(base, result));
            }
        }
    }
    struct test_case { char const* base; unsigned exponent; char const* result; };
    for (auto const& test : {
             test_case{"-3", 31, "-617673396283947"}, {"3", 32, "1853020188851841"},
             {"-2", 32, "4294967296"}, {"2", 33, "8589934592"},
             {"4294967297", 0, "1"}, {"4294967297", 1, "4294967297"},
             {"4294967297", 2, "18446744082299486209"}}) {
        m.set(base, test.base);
        m.set(expected, test.result);
        m.power(base, test.exponent, result);
        ENSURE(m.eq(result, expected));
        m.power(base, test.exponent, base);
        ENSURE(m.eq(base, expected));
    }
}

static void tst_bigint_decompose() {
    unsynch_mpz_manager m;
    scoped_mpz value(m), restored(m), large(m);
    m.set(large, "340282366920938463463374607431768211456");
    svector<digit_t> words;
    for (int n : {INT_MIN, -1, 0, 1, INT_MAX}) {
        m.set(value, n);
        bool negative = m.decompose(value, words);
        ENSURE(negative == (n < 0));
        ENSURE(words.size() == 1);
        if (n == INT_MIN)
            ENSURE(words[0] == static_cast<unsigned>(INT_MAX) + 1);
        m.set_digits(restored, words.size(), words.data());
        if (negative)
            m.neg(restored);
        ENSURE(m.eq(restored, value));
    }
    for (char const* n : {"-2147483648", "2147483648", "-4294967296", "4294967296",
                          "-18446744073709551616", "18446744073709551617"}) {
        for (bool computed : {false, true}) {
            m.set(value, n);
            if (computed) {
                m.add(value, large, value);
                m.sub(value, large, value);
            }
            // decompose must replace, rather than append to, an existing vector.
            words.push_back(42);
            bool negative = m.decompose(value, words);
            ENSURE(negative == m.is_neg(value));
            m.set_digits(restored, words.size(), words.data());
            if (negative)
                m.neg(restored);
            ENSURE(m.eq(restored, value));
        }
    }
}

static void tst_bigint_division_by_zero() {
    unsynch_mpz_manager m;
    scoped_mpz numerator(m), zero(m), result(m);
    for (char const* value : {"0", "1", "-1", "18446744073709551616", "-18446744073709551616"}) {
        for (bool computed : {false, true}) {
            for (unsigned alias : {0u, 1u, 2u}) {
                m.set(numerator, value);
                m.set(zero, 0);
                if (computed) {
                    m.set(zero, "4294967296");
                    m.sub(zero, zero, zero);
                }
                m.set(result, 42);
                bool rejected = false;
                try {
                    if (alias == 0)
                        m.machine_div(numerator, zero, result);
                    else if (alias == 1)
                        m.machine_div(numerator, zero, numerator);
                    else
                        m.machine_div(numerator, zero, zero);
                }
                catch (default_exception const&) {
                    rejected = true;
                }
                ENSURE(rejected);
                ENSURE(m.is_zero(zero));
                ENSURE(m.eq(result, mpz(42)));
                m.set(result, value);
                ENSURE(m.eq(numerator, result));
            }
        }
    }
}

static void tst_bigint_bitwise_not() {
    unsynch_mpz_manager m;
    scoped_mpz value(m), result(m), expected(m), large(m);
    m.set(large, "340282366920938463463374607431768211456");
    for (char const* input : {"0", "1", "2147483647", "18446744073709551616"}) {
        for (bool computed : {false, true}) {
            m.set(value, input);
            if (computed) {
                m.add(value, large, value);
                m.sub(value, large, value);
            }
            m.set(result, large);
            m.bitwise_not(0, value, result);
            ENSURE(m.is_zero(result));
            m.bitwise_not(0, value, value);
            ENSURE(m.is_zero(value));
        }
    }
    struct test_case { unsigned width; char const* input; char const* result; };
    for (auto const& test : {
             test_case{1, "0", "1"}, {1, "1", "0"},
             {63, "0", "9223372036854775807"}, {64, "0", "18446744073709551615"},
             {65, "0", "36893488147419103231"}, {65, "18446744073709551616", "18446744073709551615"}}) {
        m.set(value, test.input);
        m.set(expected, test.result);
        m.bitwise_not(test.width, value, result);
        ENSURE(m.eq(result, expected));
        m.bitwise_not(test.width, value, value);
        ENSURE(m.eq(value, expected));
    }
}

static void tst_bigint_division_aliases() {
    unsynch_mpz_manager m;
    scoped_mpz a(m), b(m), result(m), expected(m), large(m);
    m.set(large, "340282366920938463463374607431768211456");
    int64_t numerators[] = {0, 1, -1, 5, -5, INT_MIN, INT_MAX, -4294967297LL, 4294967297LL};
    int64_t divisors[] = {1, -1, 3, -3, -4294967296LL, 4294967296LL};
    for (int64_t numerator : numerators) {
        for (int64_t divisor : divisors) {
            int64_t modulus = divisor < 0 ? -divisor : divisor;
            int64_t remainder = numerator % modulus;
            if (remainder < 0)
                remainder += modulus;
            int64_t quotient = (numerator - remainder) / divisor;
            for (bool computed : {false, true}) {
                for (bool modulo : {false, true}) {
                    m.set(expected, modulo ? remainder : quotient);
                    for (unsigned alias : {0u, 1u, 2u}) {
                        m.set(a, numerator);
                        m.set(b, divisor);
                        if (computed) {
                            m.add(a, large, a);
                            m.sub(a, large, a);
                            m.add(b, large, b);
                            m.sub(b, large, b);
                        }
                        mpz& out = alias == 0 ? result.get() : alias == 1 ? a.get() : b.get();
                        if (modulo)
                            m.mod(a, b, out);
                        else
                            m.div(a, b, out);
                        ENSURE(m.eq(out, expected));
                    }
                }
            }
        }
    }
    // Both inputs and the output may refer to the same numeral as well.
    for (int64_t value : divisors) {
        m.set(a, value);
        m.div(a, a, a);
        ENSURE(m.is_one(a));
        m.set(a, value);
        m.mod(a, a, a);
        ENSURE(m.is_zero(a));
    }
}

void tst_bigint() {
    tst_bigint_size();
    tst_bigint_logical_shifts();
    tst_bigint_arithmetic_shifts();
    tst_bigint_power();
    tst_bigint_decompose();
    tst_bigint_division_by_zero();
    tst_bigint_bitwise_not();
    tst_bigint_division_aliases();
}
