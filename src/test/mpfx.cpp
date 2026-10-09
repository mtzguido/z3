/*++
Copyright (c) 2012 Microsoft Corporation

Module Name:

    mpfx.cpp

Abstract:

    Multi precision fixed point numbers.
    
Author:

    Leonardo de Moura (leonardo) 2012-09-19

Revision History:

--*/
#include "util/mpfx.h"
#include <iostream>

static void tst1() {
    mpfx_manager m;
    scoped_mpfx a(m), b(m), c(m);
    m.set(a, 1);
    m.set(b, 2);
    std::cout << a << " + " << b << " == " << (a+b) << "\n";
    m.set(a, 5);
    m.set(c, 3);
    m.display_raw(std::cout, (a*a*b)/c); std::cout << "\n";
    m.display_decimal(std::cout, (a*a*b)/c); std::cout << "\n";
    m.display_decimal(std::cout, (a*a*b)/c, 10); std::cout << "\n";
    m.round_to_plus_inf();
    m.display_decimal(std::cout, (a*a*b)/c); std::cout << "\n";
    m.set(a, -1, 4);
    m.display_decimal(std::cout, a); std::cout << "\n";
}

static void tst_prev_power_2(int64_t n, uint64_t d, unsigned expected) {
    mpfx_manager m;
    scoped_mpfx a(m);
    m.set(a, n, d);
    ENSURE(m.prev_power_of_two(a) == expected);
}

static void tst_prev_power_2() {
    tst_prev_power_2(-10, 1, 0);
    tst_prev_power_2(0, 1, 0);
    tst_prev_power_2(1, 1, 0);
    tst_prev_power_2(2, 1, 1);
    tst_prev_power_2(3, 1, 1);
    tst_prev_power_2(4, 1, 2);
    tst_prev_power_2(5, 1, 2);
    tst_prev_power_2(8, 1, 3);
    tst_prev_power_2(9, 1, 3);
    tst_prev_power_2(9, 2, 2);
    tst_prev_power_2(9, 4, 1);
    tst_prev_power_2(9, 5, 0);
    tst_prev_power_2((1ll << 60) + 1, 1, 60);
    tst_prev_power_2((1ll << 60), 1, 60);
    tst_prev_power_2((1ll << 60) - 1, 1, 59);
    tst_prev_power_2((1ll << 60), 3, 58);
}

static void tst_native_integers() {
    mpfx_manager m;
    scoped_mpfx value(m);
    for (int n : {INT_MIN, INT_MIN + 1, -1, 0, 1, INT_MAX}) {
        m.set(value, n);
        ENSURE(m.get_int64(value) == n);
    }
    for (int64_t n : {INT64_MIN, INT64_MIN + 1, int64_t(-1), int64_t(0), int64_t(1), INT64_MAX}) {
        m.set(value, n);
        ENSURE(m.get_int64(value) == n);
    }
    for (uint64_t n : {uint64_t(0), uint64_t(UINT_MAX), uint64_t(UINT_MAX) + 1,
                       uint64_t(INT64_MAX), uint64_t(INT64_MAX) + 1, UINT64_MAX}) {
        m.set(value, n);
        ENSURE(m.get_uint64(value) == n);
        ENSURE(m.is_int64(value) == (n <= uint64_t(INT64_MAX)));
        m.neg(value);
        ENSURE(m.is_int64(value) == (n <= uint64_t(INT64_MAX) + 1));
    }
    for (unsigned frac_words : {1u, 2u}) {
        mpfx_manager narrow(1, frac_words);
        scoped_mpfx integer(narrow), neighbor(narrow);
        narrow.set(integer, 1);
        narrow.set(neighbor, 1);
        // Fill the adjacent numeral's fractional words with nonzero bits.
        narrow.div2k(neighbor, 32 * frac_words);
        for (int n : {1, -1, INT_MIN, INT_MAX, 0}) {
            narrow.set(integer, n);
            ENSURE(narrow.get_int64(integer) == n);
            if (n >= 0)
                ENSURE(narrow.get_uint64(integer) == static_cast<uint64_t>(n));
        }
        narrow.set(integer, UINT_MAX);
        ENSURE(narrow.get_uint64(integer) == UINT_MAX);
    }
}

static void tst_power() {
    mpfx_manager m;
    scoped_mpfx a(m), result(m);
    for (unsigned exponent : {0u, 1u, 2u, unsigned(INT_MAX), unsigned(INT_MAX) + 1,
                               UINT_MAX - 1, UINT_MAX}) {
        for (int base : {-1, 0, 1}) {
            if (base == 0 && exponent == 0) continue;
            int expected = exponent == 0 || (base == -1 && exponent % 2 == 0) ? 1 : base;
            m.set(a, base);
            m.power(a, exponent, result);
            ENSURE(m.get_int64(result) == expected);
            m.power(a, exponent, a);
            ENSURE(m.eq(a, result));
        }
    }
    // 3^16 fits, but the final unused square 3^32 does not.
    mpfx_manager narrow(1, 1);
    scoped_mpfx base(narrow), result32(narrow);
    narrow.set(base, 3);
    narrow.power(base, 16, result32);
    ENSURE(narrow.get_uint64(result32) == 43046721);
}

void tst_mpfx() {
    tst_power();
    tst_native_integers();
    tst_prev_power_2();
    tst1();
}
