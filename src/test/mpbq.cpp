/*++
Copyright (c) 2012 Microsoft Corporation

Module Name:

    mpbq.cpp

Abstract:

    mpbq tests...

Author:

    Leonardo de Moura (leonardo) 2012-09-20

Revision History:

--*/
#include "util/mpbq.h"
#include <iostream>

static void tst1() {
    unsynch_mpz_manager zm;
    mpbq_manager m(zm);
    scoped_mpbq  a(m), b(m);
    m.set(a, INT_MAX);
    a = a + 1;
    a = a * a;
    a = a*3 - 1;
    a = a * a - 5;
    a = a * a - 7;
    m.div2k(a, 67);
    std::cout << a << "\n";
    b = a;
    m.approx(b, 32, true);
    std::cout << b << "\n";
    b = a;
    m.approx(b, 32, false);
    std::cout << b << "\n";
    b = a; m.neg(b);
    m.approx(b, 32, true);
    std::cout << b << "\n";
    b = a; m.neg(b);
    m.approx(b, 32, false);
    std::cout << b << "\n";
}

static void tst2() {
   unsynch_mpz_manager zm;
   mpbq_manager m(zm);
   scoped_mpbq  a(m), b(m);
   m.set(a, 5);
   m.set(b, 3);
   m.approx_div(a, b, a, 128);
   std::cout << a << "\n";
 }

static void tst_mul_alias_normalization() {
    unsynch_mpz_manager zm;
    mpbq_manager m(zm);
    scoped_mpbq a(m), b(m), separate(m), expected(m);
    scoped_mpz f(zm), c(zm), large(zm);
    for (int an : {-8, -2, 0, 2, 3, 8}) {
        for (int bn : {-3, -1, 0, 1, 2, 4}) {
            for (unsigned ak : {0u, 1u, 3u}) {
                for (unsigned bk : {0u, 1u, 3u}) {
                    int numerator = an * bn, denominator = 1 << (ak + bk);
                    int quotient = numerator / denominator;
                    bool integral = numerator % denominator == 0;
                    int expected_floor = quotient - (!integral && numerator < 0);
                    int expected_ceil = quotient + (!integral && numerator > 0);
                    m.set(expected, numerator, ak + bk);
                    for (unsigned alias = 0; alias < 3; ++alias) {
                        m.set(a, an, ak);
                        m.set(b, bn, bk);
                        mpbq& output = alias == 0 ? separate.get() : alias == 1 ? a.get() : b.get();
                        m.mul(a, b, output);
                        ENSURE(m.eq(output, expected));
                        ENSURE(m.is_int(output) == integral);
                        m.floor(zm, output, f);
                        m.ceil(zm, output, c);
                        ENSURE(zm.eq(f, mpz(expected_floor)));
                        ENSURE(zm.eq(c, mpz(expected_ceil)));
                    }
                }
            }
        }
    }
    zm.power(mpz(2), 96, large);
    m.set(a, large);
    m.set(b, 3, 97);
    m.set(expected, 3, 1);
    m.mul(a, b, a);
    ENSURE(m.eq(a, expected));
}

void tst_mpbq() {
    tst_mul_alias_normalization();
    tst1();
    tst2();
}
