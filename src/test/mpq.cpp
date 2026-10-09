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
#include "util/mpq_inf.h"
#include "util/mpff.h"
#include "util/mpfx.h"
#include "util/f2n.h"
#include "util/hwf.h"
#include "util/rational.h"
#include "util/timeit.h"
#include "util/z3_exception.h"
#include <iostream>
#include <cmath>
#include <cfenv>
#include <limits>

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

template<bool SYNCH>
static void tst_large_double_conversion() {
    mpq_manager<SYNCH> m;
    _scoped_numeral<mpq_manager<SYNCH>> value(m);
    _scoped_numeral<mpz_manager<SYNCH>> n(m), d(m), base(m);
    auto check = [&](double expected) {
        for (bool negative : {false, true}) {
            m.set(value, n, d);
            if (negative)
                m.neg(value);
            double actual = m.get_double(value);
            ENSURE(actual == (negative ? -expected : expected));
            ENSURE(std::signbit(actual) == negative);
        }
    };
    for (unsigned bits : {1024u, 2048u}) {
        m.power(mpz(2), bits, n);
        m.inc(n);
        m.add(n, mpz(2), d);
        check(1.0);
    }
    m.power(mpz(2), 1023, n);
    m.inc(n);
    m.power(mpz(2), 1024, d);
    m.add(d, mpz(3), d);
    check(0.5);
    m.power(mpz(2), 1024, n);
    m.inc(n);
    m.set(d, 3);
    check(0x1.5555555555555p+1022);
    m.set(n, 3);
    m.power(mpz(2), 1024, d);
    m.inc(d);
    check(0x1.8p-1023);

    double tiny = std::numeric_limits<double>::denorm_min();
    m.power(mpz(2), 2048, base);
    m.power(mpz(2), 2048 + 1075, d);
    for (int offset : {-1, 0, 1}) {
        m.add(base, mpz(offset), n);
        check(offset > 0 ? tiny : 0.0);
        m.mul(base, mpz(3), n);
        m.add(n, mpz(offset), n);
        check(offset < 0 ? tiny : 2 * tiny);
    }
    // Values on either side of a rounding midpoint near one. The odd
    // numerators keep these large fractions from reducing to small integers.
    m.set(base, uint64_t(9007199254740995ULL)); // 2^53 + 3
    m.mul2k(base, 2048);
    m.power(mpz(2), 2048 + 53, d);
    for (int offset : {-1, 1}) {
        m.add(base, mpz(offset), n);
        check(offset < 0 ? 0x1.0000000000001p+0 : 0x1.0000000000002p+0);
    }
    // The midpoint between the largest subnormal and the smallest normal.
    m.set(base, uint64_t(9007199254740991ULL)); // 2^53 - 1
    m.mul2k(base, 2048);
    m.power(mpz(2), 2048 + 1075, d);
    for (int offset : {-1, 0, 1}) {
        m.add(base, mpz(offset), n);
        check(offset < 0 ? 0x0.fffffffffffffp-1022 : 0x1p-1022);
    }
    // Values just below and above the rounding threshold for infinity.
    m.set(base, uint64_t(18014398509481983ULL)); // 2^54 - 1
    m.mul2k(base, 2048 + 970);
    m.power(mpz(2), 2048, d);
    for (int offset : {-1, 1}) {
        m.add(base, mpz(offset), n);
        check(offset < 0 ? std::numeric_limits<double>::max() : std::numeric_limits<double>::infinity());
    }
    m.power(mpz(2), 4096, n);
    m.inc(n);
    m.set(d, 3);
    check(std::numeric_limits<double>::infinity());
    m.swap(n, d);
    check(0.0);
}

template<bool SYNCH>
static void tst_scientific_exponent_overflow() {
    mpq_manager<SYNCH> m;
    for (char const* text : {
            "1e4294967296", "1e-4294967296", "1e18446744073709551616",
            "1e18446744073709551617", "-1E+18446744073709551616",
            "1.5e-18446744073709551617", "1e999999999999999999999999999999999999"}) {
        _scoped_numeral<mpq_manager<SYNCH>> value(m);
        bool rejected = false;
        try {
            m.set(value, text);
        }
        catch (default_exception const&) {
            rejected = true;
        }
        ENSURE(rejected);
    }
    for (char const* text : {"1e000000000000000000000000000000000002", "10E+1", "1000e-1"}) {
        _scoped_numeral<mpq_manager<SYNCH>> value(m);
        m.set(value, text);
        ENSURE(m.eq(value, 100));
    }
}

template<bool SYNCH>
static void tst_parse_reused_rational() {
    mpq_manager<SYNCH> m;
    _scoped_numeral<mpq_manager<SYNCH>> value(m), expected(m);
    struct test_case { char const* text; int numerator; unsigned denominator; };
    test_case cases[] = {
        {"1e2", 100, 1}, {"1e0", 1, 1}, {"1e-2", 1, 100},
        {"-2E+2", -200, 1}, {"0e2", 0, 1}, {"1.25e2", 125, 1},
        {"1.25e-2", 1, 80}, {"-1.25", -5, 4}, {"7/9", 7, 9}, {"42", 42, 1}
    };
    for (char const* previous : {"1/3", "-7/11", "1/18446744073709551617", "0", "5"}) {
        for (auto const& test : cases) {
            m.set(value, previous);
            m.set(value, test.text);
            m.set(expected, test.numerator, test.denominator);
            ENSURE(m.eq(value, expected));
        }
    }
    // Also exercise successive assignments whose denominators vary.
    for (auto const& test : cases) {
        m.set(value, test.text);
        m.set(expected, test.numerator, test.denominator);
        ENSURE(m.eq(value, expected));
    }
}

template<bool SYNCH>
static void tst_perfect_square_alias() {
    mpq_manager<SYNCH> m;
    _scoped_numeral<mpq_manager<SYNCH>> value(m), expected(m);
    struct test_case { char const* text; char const* root; };
    for (auto const& test : {test_case{"0", "0"}, {"1", "1"}, {"16", "4"},
                             {"4/9", "2/3"}, {"2/9", nullptr}, {"4/3", nullptr}, {"-4/9", nullptr}}) {
        m.set(value, test.text);
        ENSURE(m.is_perfect_square(value, value) == (test.root != nullptr));
        if (test.root) {
            m.set(expected, test.root);
            ENSURE(m.eq(value, expected));
        }
    }
    _scoped_numeral<mpz_manager<SYNCH>> numerator(m), denominator(m);
    m.power(mpz(2), 64, numerator);
    m.add(numerator, mpz(3), numerator);
    m.power(mpz(2), 65, denominator);
    m.inc(denominator);
    m.set(expected, numerator, denominator);
    m.mul(expected, expected, value);
    ENSURE(m.is_perfect_square(value, value));
    ENSURE(m.eq(value, expected));
    m.set(value, 16);
    ENSURE(m.is_int_perfect_square(value, value));
    ENSURE(m.eq(value, 4));
}

template<bool SYNCH>
static void tst_signed_fraction_endpoints() {
    mpq_manager<SYNCH> m;
    _scoped_numeral<mpq_manager<SYNCH>> value(m);
    _scoped_numeral<mpz_manager<SYNCH>> lhs(m), rhs(m), gcd(m);
    for (int numerator : {INT_MIN, INT_MIN + 1, -1, 0, 1, INT_MAX}) {
        for (int denominator : {INT_MIN, INT_MIN + 1, -1, 1, 2, INT_MAX}) {
            m.set(value, numerator, denominator);
            ENSURE(m.is_pos(value.get().denominator()));
            m.mul(value.get().numerator(), mpz(denominator), lhs);
            m.mul(value.get().denominator(), mpz(numerator), rhs);
            ENSURE(m.eq(lhs, rhs));
            m.gcd(value.get().numerator(), value.get().denominator(), gcd);
            ENSURE(m.is_one(gcd));
        }
    }
}

template<bool SYNCH>
static void tst_set_component_aliases() {
    mpq_manager<SYNCH> m;
    _scoped_numeral<mpq_manager<SYNCH>> value(m), expected(m);
    mpz external_numerator(-5), external_denominator(-7);
    for (char const* original : {"-2/3", "0", "2/3", "4294967297/73786976294838206467"}) {
        for (unsigned ni = 0; ni < 3; ++ni) {
            for (unsigned di = 0; di < 3; ++di) {
                m.set(value, original);
                mpz const* numerators[] = {&value.get().numerator(), &value.get().denominator(), &external_numerator};
                mpz const* denominators[] = {&value.get().numerator(), &value.get().denominator(), &external_denominator};
                if (m.is_zero(*denominators[di]))
                    continue;
                m.set(expected, *numerators[ni], *denominators[di]);
                m.set(value, *numerators[ni], *denominators[di]);
                ENSURE(m.eq(value, expected));
                ENSURE(m.is_pos(value.get().denominator()));
            }
        }
    }
}

template<bool SYNCH>
static void tst_infinitesimal_rounding() {
    mpq_manager<SYNCH> m;
    mpq_inf_manager<SYNCH> im;
    _scoped_numeral<mpq_inf_manager<SYNCH>> value(im);
    _scoped_numeral<mpq_manager<SYNCH>> separate(m);
    for (int numerator : {-6, -1, 0, 1, 6}) {
        for (int denominator : {1, 4}) {
            for (int epsilon : {-5, -1, 0, 1, 5}) {
                int quotient = numerator / denominator;
                bool integral = numerator % denominator == 0;
                int expected_floor = quotient - (!integral && numerator < 0) - (integral && epsilon < 0);
                int expected_ceil = quotient + (!integral && numerator > 0) + (integral && epsilon > 0);
                for (unsigned alias = 0; alias < 3; ++alias) {
                    for (bool ceiling : {false, true}) {
                        m.set(value.get().first, numerator, denominator);
                        m.set(value.get().second, epsilon, 3);
                        mpq& output = alias == 0 ? separate.get() : alias == 1 ? value.get().first : value.get().second;
                        if (ceiling)
                            im.ceil(value, output);
                        else
                            im.floor(value, output);
                        ENSURE(m.eq(output, ceiling ? expected_ceil : expected_floor));
                    }
                }
            }
        }
    }
}

template<typename M, typename ToRational>
static void tst_directed_powers(M& m, ToRational to_rational) {
    _scoped_numeral<M> a(m), saved(m), result(m);
    unsynch_mpq_manager qm;
    scoped_mpq input(qm), exact(qm), actual(qm);
    for (bool upward : {false, true}) {
        m.set_rounding(upward);
        for (int numerator : {-15, -3, -1, 1, 3, 15}) {
            for (unsigned denominator : {3u, 7u, 17u}) {
                for (unsigned exponent : {2u, 3u, 4u, 5u, 9u, 12u}) {
                    m.set(saved, numerator, denominator);
                    to_rational(saved, input);
                    qm.power(input, exponent, exact);
                    for (bool alias : {false, true}) {
                        m.set(a, saved);
                        auto& output = alias ? a.get() : result.get();
                        m.power(a, exponent, output);
                        to_rational(output, actual);
                        ENSURE(upward ? qm.ge(actual, exact) : qm.le(actual, exact));
                    }
                }
            }
        }
    }
}

static void tst_directed_powers() {
    int rounding = std::fegetround();
    mpff_manager ff;
    tst_directed_powers(ff, [&](mpff const& n, scoped_mpq& q) { ff.to_mpq(n, q.m(), q); });
    mpfx_manager fx;
    tst_directed_powers(fx, [&](mpfx const& n, scoped_mpq& q) { fx.to_mpq(n, q.m(), q); });
    mpf_manager fm;
    f2n<mpf_manager> f(fm);
    tst_directed_powers(f, [&](mpf const& n, scoped_mpq& q) { fm.to_rational(n, q); });
    hwf_manager hm;
    f2n<hwf_manager> h(hm);
    tst_directed_powers(h, [&](hwf const& n, scoped_mpq& q) { hm.to_rational(n, q); });
    std::fesetround(rounding);
}

template<typename Convert>
static void tst_directed_conversion(Convert convert) {
    unsynch_mpq_manager m;
    scoped_mpz large(m), numerator(m), denominator(m);
    scoped_mpq input(m), actual(m);
    for (unsigned bits : {53u, 64u, 65u, 127u, 128u, 200u, 1024u, 1030u}) {
        m.set(large, 1);
        m.mul2k(large, bits);
        for (int n : {1, 3, 5, 7}) {
            for (int d : {1, 3, 9, 17}) {
                m.add(large, mpz(n), numerator);
                m.add(large, mpz(d), denominator);
                for (bool negative : {false, true}) {
                    m.set(input, numerator, denominator);
                    if (negative)
                        m.neg(input);
                    for (bool upward : {false, true}) {
                        convert(m, input, upward, actual);
                        ENSURE(upward ? m.ge(actual, input) : m.le(actual, input));
                    }
                }
            }
        }
    }
}

static void tst_directed_conversions() {
    mpff_manager ff;
    scoped_mpff fv(ff);
    tst_directed_conversion([&](unsynch_mpq_manager& m, mpq const& q, bool up, mpq& actual) {
        ff.set_rounding(up);
        ff.set(fv, m, q);
        ff.to_mpq(fv, m, actual);
    });
    mpfx_manager fx;
    scoped_mpfx xv(fx);
    tst_directed_conversion([&](unsynch_mpq_manager& m, mpq const& q, bool up, mpq& actual) {
        fx.set_rounding(up);
        fx.set(xv, m, q);
        fx.to_mpq(xv, m, actual);
    });
    hwf_manager hm;
    hwf hv;
    tst_directed_conversion([&](unsynch_mpq_manager& m, mpq const& q, bool up, mpq& actual) {
        hm.set(hv, up ? MPF_ROUND_TOWARD_POSITIVE : MPF_ROUND_TOWARD_NEGATIVE, q);
        hm.to_rational(hv, m, actual);
    });

    unsynch_mpq_manager m;
    scoped_mpz denominator(m);
    scoped_mpq tiny(m);
    m.set(denominator, 1);
    m.mul2k(denominator, 200);
    for (int sign : {-1, 1}) {
        m.set(tiny, mpz(sign), denominator);
        fx.set_rounding(sign < 0);
        for (int previous : {-1, 0, 1}) {
            fx.set(xv, previous);
            fx.set(xv, m, tiny);
            ENSURE(fx.is_zero(xv));
            ENSURE(!fx.is_neg(xv));
        }
    }
}

void tst_mpq() {
    tst_directed_conversions();
    tst_directed_powers();
    tst_infinitesimal_rounding<false>();
    tst_infinitesimal_rounding<true>();
    tst_set_component_aliases<false>();
    tst_set_component_aliases<true>();
    tst_signed_fraction_endpoints<false>();
    tst_signed_fraction_endpoints<true>();
    tst_perfect_square_alias<false>();
    tst_perfect_square_alias<true>();
    tst_parse_reused_rational<false>();
    tst_parse_reused_rational<true>();
    tst_scientific_exponent_overflow<false>();
    tst_scientific_exponent_overflow<true>();
    tst_large_double_conversion<false>();
    tst_large_double_conversion<true>();
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


