/*++
Copyright (c) 2012 Microsoft Corporation

Module Name:

    f2n.cpp

Abstract:


Author:

    Leonardo de Moura (leonardo) 2012-08-17.

Revision History:

--*/
#include "util/f2n.h"
#include "util/hwf.h"
#include "util/mpf.h"
#include <iostream>

static void tst1() {
    hwf_manager      hm;
    f2n<hwf_manager> m(hm);
    hwf a, b;
    m.set(a, 11, 3);
    m.floor(a, b);
    std::cout << "floor(11/3): " << m.to_double(b) << "\n";
    m.ceil(a, b);
    std::cout << "ceil(11/3): " << m.to_double(b) << "\n";
    m.set(a, -11, 3);
    m.floor(a, b);
    std::cout << "floor(-11/3): " << m.to_double(b) << "\n";
    m.ceil(a, b);
    std::cout << "ceil(-11/3): " << m.to_double(b) << "\n";
    m.set(a, 11, 1);
    m.floor(a, b);
    std::cout << "floor(11): " << m.to_double(b) << "\n";
    m.ceil(a, b);
    std::cout << "ceil(11): " << m.to_double(b) << "\n";
}

static void tst2() {
    std::cout << "using mpf...\n";
    mpf_manager      fm;
    f2n<mpf_manager> m(fm);
    scoped_mpf a(fm), b(fm);
    m.set(a, 11, 3);
    m.floor(a, b);
    std::cout << "floor(11/3): " << m.to_double(b) << "\n";
    m.ceil(a, b);
    std::cout << "ceil(11/3): " << m.to_double(b) << "\n";
    m.set(a, -11, 3);
    m.floor(a, b);
    std::cout << "floor(-11/3): " << m.to_double(b) << "\n";
    m.ceil(a, b);
    std::cout << "ceil(-11/3): " << m.to_double(b) << "\n";
    m.set(a, 11, 1);
    m.floor(a, b);
    std::cout << "floor(11): " << m.to_double(b) << "\n";
    m.ceil(a, b);
    std::cout << "ceil(11): " << m.to_double(b) << "\n";
}

template<typename M>
static void tst_large_power() {
    M fm;
    f2n<M> m(fm);
    _scoped_numeral<f2n<M>> a(m), result(m), expected(m);
    for (unsigned exponent : {0u, 1u, unsigned(INT_MAX), unsigned(INT_MAX) + 1, UINT_MAX}) {
        for (int base : {-1, 1}) {
            m.set(a, base);
            m.set(expected, exponent == 0 || (base == -1 && exponent % 2 == 0) ? 1 : base);
            m.power(a, exponent, result);
            ENSURE(m.eq(result, expected));
            m.power(a, exponent, a);
            ENSURE(m.eq(a, expected));
        }
    }
}

void tst_f2n() {
    tst_large_power<mpf_manager>();
    tst_large_power<hwf_manager>();
    tst1();
    tst2();
}
