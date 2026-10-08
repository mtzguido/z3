/*++
Copyright (c) 2012 Microsoft Corporation

Module Name:

    mpf.cpp

Abstract:

    mpf repros...

Author:

    Leonardo de Moura (leonardo) 2012-08-21.

Revision History:

--*/
#include "util/mpf.h"
#include "util/f2n.h"

static void bug_set_int() {
    mpf_manager fm;
    scoped_mpf  a(fm);

    fm.set(a, 11, 53, 3);
    ENSURE(fm.to_double(a) == 3.0);

    fm.set(a, 11, 53, 0);
    ENSURE(fm.to_double(a) == 0.0);

    fm.set(a, 11, 53, -1);
    ENSURE(fm.to_double(a) == -1.0);

    fm.set(a, 11, 53, INT_MAX);
    ENSURE(fm.to_double(a) == (double)INT_MAX);

    fm.set(a, 11, 53, INT_MIN);
    ENSURE(fm.to_double(a) == (double)INT_MIN);

    fm.set(a, 8, 24, 3);
    ENSURE(fm.to_float(a) == 3.0);
    ENSURE(fm.to_double(a) == 3.0);

    fm.set(a, 8, 24, 0);
    ENSURE(fm.to_float(a) == 0.0);
    ENSURE(fm.to_double(a) == 0.0);

    fm.set(a, 8, 24, -1);
    ENSURE(fm.to_float(a) == -1.0);
    ENSURE(fm.to_double(a) == -1.0);    

    fm.set(a, 8, 24, INT_MIN);
    ENSURE(fm.to_float(a) == (float)INT_MIN);

    // CMW: This one depends on the rounding mode, but fm.set(..., int) doesn't have one.
    // fm.set(a, 8, 24, INT_MAX);
    // ENSURE(fm.to_float(a) == (float)INT_MAX);
}

static void bug_set_double() {
    mpf_manager fm;
    scoped_mpf  a(fm);

    fm.set(a, 11, 53, 2.5);
    ENSURE(fm.to_double(a) == 2.5);

    fm.set(a, 11, 53, -42.25);
    ENSURE(fm.to_double(a) == -42.25);

    fm.set(a, 8, 24, (double)2.5);
    ENSURE(fm.to_double(a) == 2.5);

    fm.set(a, 8, 24, (double)-42.25);
    ENSURE(fm.to_double(a) == -42.25);

    fm.set(a, 8, 24, (float)2.5);
    ENSURE(fm.to_float(a) == 2.5);

    fm.set(a, 8, 24, (float)-42.25);
    ENSURE(fm.to_float(a) == -42.25);
}

static void test_to_sbv_mpq() {
    mpf_manager fm;
    scoped_mpf a(fm);
    scoped_mpq r(fm.mpq_manager()), expected(fm.mpq_manager());

    fm.set(a, 11, 53, 2.5);
    fm.to_sbv_mpq(MPF_ROUND_NEAREST_TEVEN, a, r);
    fm.mpq_manager().set(expected, 2);
    ENSURE(fm.mpq_manager().eq(r, expected));
    fm.to_sbv_mpq(MPF_ROUND_NEAREST_TAWAY, a, r);
    fm.mpq_manager().set(expected, 3);
    ENSURE(fm.mpq_manager().eq(r, expected));

    fm.set(a, 63, 24, false, fm.mk_min_exp(63), 0);
    fm.to_sbv_mpq(MPF_ROUND_TOWARD_POSITIVE, a, r);
    ENSURE(fm.mpq_manager().is_one(r));
    fm.to_sbv_mpq(MPF_ROUND_TOWARD_ZERO, a, r);
    ENSURE(fm.mpq_manager().is_zero(r));

    fm.set(a, 63, 24, true, fm.mk_min_exp(63), 0);
    fm.to_sbv_mpq(MPF_ROUND_TOWARD_NEGATIVE, a, r);
    ENSURE(fm.mpq_manager().is_minus_one(r));
    fm.to_sbv_mpq(MPF_ROUND_TOWARD_POSITIVE, a, r);
    ENSURE(fm.mpq_manager().is_zero(r));

    fm.mk_pzero(63, 24, a);
    fm.to_sbv_mpq(MPF_ROUND_TOWARD_POSITIVE, a, r);
    ENSURE(fm.mpq_manager().is_zero(r));
}

static void test_set_bigint_exponent() {
    mpf_manager fm;
    auto& qm = fm.mpq_manager();
    scoped_mpz exponent(qm), large(qm);
    scoped_mpq significand(qm);
    scoped_mpf actual(fm), expected(fm);
    qm.set(large, "340282366920938463463374607431768211456");
    for (unsigned ebits : {8u, 11u, 63u}) {
        for (int64_t e : {int64_t(-17), int64_t(-1), int64_t(0), int64_t(1), int64_t(17),
                          int64_t(INT_MIN) - 1, int64_t(INT_MAX) + 1}) {
            if (e < fm.mk_min_exp(ebits) || e > fm.mk_max_exp(ebits))
                continue;
            for (bool negative : {false, true}) {
                qm.set(significand, negative ? -1 : 1);
                fm.set(expected, ebits, 24, negative, e, uint64_t(0));
                for (bool computed : {false, true}) {
                    qm.set(exponent, e);
                    if (computed) {
                        qm.add(exponent, large, exponent);
                        qm.sub(exponent, large, exponent);
                    }
                    fm.set(actual, ebits, 24, MPF_ROUND_NEAREST_TEVEN, exponent, significand);
                    ENSURE(fm.eq_core(actual, expected));
                }
            }
        }
    }
}

static void test_set_extreme_exponents() {
    mpf_manager fm;
    auto& qm = fm.mpq_manager();
    scoped_mpz exponent(qm), large(qm);
    scoped_mpq significand(qm);
    scoped_mpf actual(fm), expected(fm);
    qm.set(large, "340282366920938463463374607431768211456");
    struct format { unsigned ebits, sbits; };
    for (auto f : {format{2, 2}, {8, 24}, {11, 53}, {63, 24}}) {
        for (auto rm : {MPF_ROUND_NEAREST_TEVEN, MPF_ROUND_NEAREST_TAWAY, MPF_ROUND_TOWARD_POSITIVE,
                        MPF_ROUND_TOWARD_NEGATIVE, MPF_ROUND_TOWARD_ZERO}) {
            for (bool negative : {false, true}) {
                qm.set(significand, negative ? -1 : 1);
                bool away = negative ? rm == MPF_ROUND_TOWARD_NEGATIVE : rm == MPF_ROUND_TOWARD_POSITIVE;
                bool nearest = rm == MPF_ROUND_NEAREST_TEVEN || rm == MPF_ROUND_NEAREST_TAWAY;
                for (bool overflow : {false, true}) {
                    if (overflow) {
                        if (away || nearest)
                            fm.mk_inf(f.ebits, f.sbits, negative, expected);
                        else
                            fm.mk_max_value(f.ebits, f.sbits, negative, expected);
                    }
                    else if (away)
                        fm.set(expected, f.ebits, f.sbits, negative, fm.mk_bot_exp(f.ebits), uint64_t(1));
                    else
                        fm.mk_zero(f.ebits, f.sbits, negative, expected);

                    // The first exponent is just beyond the relevant format boundary;
                    // the others reach or exceed the signed 64-bit conversion bounds.
                    std::string boundary = std::to_string(overflow ? fm.mk_max_exp(f.ebits) + 1 :
                                                          fm.mk_min_exp(f.ebits) - f.sbits - 1);
                    for (char const* e : {boundary.c_str(), overflow ? "9223372036854775807" : "-9223372036854775808",
                                          overflow ? "18446744073709551616" : "-18446744073709551616"}) {
                        for (bool computed : {false, true}) {
                            qm.set(exponent, e);
                            if (computed) {
                                qm.add(exponent, large, exponent);
                                qm.sub(exponent, large, exponent);
                            }
                            fm.set(actual, f.ebits, f.sbits, rm, exponent, significand);
                            ENSURE(fm.eq_core(actual, expected));
                        }
                    }
                }
                // Exercise both sides of, and exactly at, half the least subnormal.
                for (int numerator : {1, 2, 3, 4}) {
                    qm.set(significand, negative ? -numerator : numerator, 2);
                    bool up = numerator == 4 || away ||
                        (numerator == 3 && nearest) || (numerator == 2 && rm == MPF_ROUND_NEAREST_TAWAY);
                    if (up)
                        fm.set(expected, f.ebits, f.sbits, negative, fm.mk_bot_exp(f.ebits), uint64_t(1));
                    else
                        fm.mk_zero(f.ebits, f.sbits, negative, expected);
                    for (bool computed : {false, true}) {
                        qm.set(exponent, fm.mk_min_exp(f.ebits) - f.sbits);
                        if (computed) {
                            qm.add(exponent, large, exponent);
                            qm.sub(exponent, large, exponent);
                        }
                        fm.set(actual, f.ebits, f.sbits, rm, exponent, significand);
                        ENSURE(fm.eq_core(actual, expected));
                    }
                }
            }
        }
    }
    // Zero stays zero regardless of an otherwise overflowing exponent.
    qm.set(significand, 0);
    fm.set(actual, 11, 53, MPF_ROUND_NEAREST_TEVEN, large, significand);
    ENSURE(fm.is_pzero(actual));
}

void tst_mpf() {
    // enable_trace("mpf_mul_bug");
    bug_set_int();
    bug_set_double();
    test_to_sbv_mpq();
    test_set_bigint_exponent();
    test_set_extreme_exponents();
}
