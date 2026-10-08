/*++
Copyright (c) 2006 Microsoft Corporation

Module Name:

    mpq.cpp

Abstract:

    <abstract>

Author:

    Leonardo de Moura (leonardo) 2010-06-21.

Revision History:

--*/

#include "util/mpq.h"
#include "util/rational.h"
#include "util/timeit.h"
#include <iostream>

static void tst0() {
    synch_mpq_manager m;
    mpq a, b;
    m.set(a, 2, 3);
    m.set(b, 4, 3);
    m.div(a, b, b);
    ENSURE(m.eq(b, m.mk_q(1, 2)));
}

static void tst1() {
    synch_mpq_manager m;
    char const * str = "1002034040050606089383838288182";
    mpz v;
    m.set(v, str);
    std::cout << str << "\n" << m.to_string(v) << "\n";
    mpz v2, v3;
    m.mul(v, m.mk_z(-2), v2);
    std::cout << "*-2 = \n" << m.to_string(v2) << "\n";
    m.add(v, v2, v3);
    m.neg(v3);
    ENSURE(m.eq(v, v3));
    ENSURE(m.le(v, v3));
    ENSURE(m.ge(v, v3));
    ENSURE(m.lt(v2, v));
    ENSURE(m.le(v2, v));
    ENSURE(m.gt(v, v2));
    ENSURE(m.ge(v, v2));
    ENSURE(m.neq(v, v2));
    ENSURE(!m.neq(v, v3));
    m.del(v);
    m.del(v2);
    m.del(v3);
}

#if 0
static void mk_random_num_str(unsigned buffer_sz, char * buffer) {
    unsigned div_pos;
    unsigned sz = (rand() % (buffer_sz-2)) + 1;
    if (rand() % 2 == 0) {
        // integer
        div_pos = sz + 1;
    }
    else {
        div_pos = rand() % sz;
        if (div_pos == 0)
            div_pos++;
    }
    ENSURE(sz < buffer_sz);
    for (unsigned i = 0; i < sz-1; ++i) {
        if (i == div_pos && i < sz-2) {
            buffer[i] = '/';
            i++;
            buffer[i] = '1' + (rand() % 9);
        }
        else {
            buffer[i] = '0' + (rand() % 10);
        }
    }
    buffer[sz-1] = 0;
}
#endif

static void bug1() {
    synch_mpq_manager m;
    mpq a;
    mpq b;
    m.set(a, 2);
    m.set(b, 1, 2);
    m.inv(a, a);
    ENSURE(m.eq(a, b));
}

static void bug2() {
    synch_mpq_manager m;
    mpq a;
    mpq b;
    m.set(a, -2);
    m.set(b, -1, 2);
    m.inv(a, a);
    ENSURE(m.eq(a, b));
}

static void tst2() {
    unsynch_mpq_manager m;
    scoped_mpq a(m);
    m.set(a, 1, 3);
    std::cout << "1/3: ";
    m.display_decimal(std::cout, a, 10);
    std::cout << "\n1/4: ";
    m.set(a, 1, 4);
    m.display_decimal(std::cout, a, 10);
    std::cout << "\n";
}

static void set_str_bug() {
    unsynch_mpq_manager m;
    scoped_mpq a(m);
    scoped_mpq b(m);
    m.set(a, "1.0");
    std::cout << a << "\n";
    m.set(b, 1);
    ENSURE(a == b);
    m.set(a, "1.1");
    std::cout << a << "\n";
    m.set(b, 11, 10);
    ENSURE(a == b);
    m.set(a, "1/3");
    m.set(b, 1, 3);
    std::cout << a << "\n";
    ENSURE(a == b);
}

static void tst_prev_power_2(int64_t n, uint64_t d, unsigned expected) {
    unsynch_mpq_manager m;
    scoped_mpq a(m);
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

template<bool SYNCH>
static void tst_add_sub_aliases() {
    mpq_manager<SYNCH> m;
    _scoped_numeral<mpq_manager<SYNCH>> a(m), b(m), result(m), expected(m), large(m);
    m.set(large, "340282366920938463463374607431768211456");
    int64_t numerators[] = {-5, 0, 1, 4294967297LL};
    for (int64_t an : numerators) {
        for (int64_t bn : {-1, 1, 5}) {
            for (uint64_t ad : {4u, 6u, 9u}) {
                for (uint64_t bd : {4u, 6u, 9u}) {
                    for (bool subtract : {false, true}) {
                        int64_t n = an * static_cast<int64_t>(bd);
                        n += (subtract ? -bn : bn) * static_cast<int64_t>(ad);
                        m.set(expected, n, ad * bd);
                        for (bool computed : {false, true}) {
                            for (unsigned alias : {0u, 1u, 2u}) {
                                m.set(a, an, ad);
                                m.set(b, bn, bd);
                                if (computed) {
                                    m.add(a, large, a);
                                    m.sub(a, large, a);
                                    m.add(b, large, b);
                                    m.sub(b, large, b);
                                }
                                mpq& out = alias == 0 ? result.get() : alias == 1 ? a.get() : b.get();
                                if (subtract)
                                    m.sub(a, b, out);
                                else
                                    m.add(a, b, out);
                                ENSURE(m.eq(out, expected));
                            }
                        }
                    }
                }
            }
        }
    }
    m.set(a, 1, 4);
    m.add(a, a, a);
    m.set(expected, 1, 2);
    ENSURE(m.eq(a, expected));
    m.sub(a, a, a);
    ENSURE(m.is_zero(a) && m.is_int(a));
}

void tst_mpq() {
    tst_add_sub_aliases<false>();
    tst_add_sub_aliases<true>();
    tst_prev_power_2();
    set_str_bug();
    bug2();
    bug1();
    tst0();
    tst1();
    tst2();
}


