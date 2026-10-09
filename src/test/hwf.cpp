/*++
Copyright (c) 2012 Microsoft Corporation

Module Name:

    hwf.cpp

Abstract:

    hwf repros...

Author:

    Leonardo de Moura (leonardo) 2012-08-23.

Revision History:

--*/
#include "util/hwf.h"
#include "util/f2n.h"
#include "util/rational.h"
#include <cfloat>
#include <cmath>
#include <limits>
#include <iostream>

static void bug_set_double() {
    hwf_manager m;
    hwf a;

    m.set(a, 0.1);
    ENSURE(m.is_regular(a));

    m.set(a, 1.1);
    ENSURE(m.is_regular(a));

    m.set(a, 11.3);
    ENSURE(m.is_regular(a));

    m.set(a, 0.0);
    ENSURE(m.is_regular(a));
}

static void bug_to_rational() {    
    hwf_manager m;
    hwf a;

    unsynch_mpq_manager mq;
    scoped_mpq r(mq);

    double ad = 0, rd = 0;

    m.set(a, 0.0);
    m.to_rational(a, r);
    ad = m.to_double(a);
    rd = mq.get_double(r);
    VERIFY(ad == rd);

    m.set(a, 1.0);
    m.to_rational(a, r);
    ad = m.to_double(a);
    rd = mq.get_double(r);
    VERIFY(ad == rd);

    m.set(a, 1.5);
    m.to_rational(a, r);
    ad = m.to_double(a);
    rd = mq.get_double(r);
    ENSURE(ad == rd);

    m.set(a, 0.875);
    m.to_rational(a, r);
    ad = m.to_double(a);
    rd = mq.get_double(r);
    ENSURE(ad == rd);

    m.set(a, -1.0);
    m.to_rational(a, r);
    ad = m.to_double(a);
    rd = mq.get_double(r);
    ENSURE(ad == rd);

    m.set(a, -1.5);
    m.to_rational(a, r);
    ad = m.to_double(a);
    rd = mq.get_double(r);
    ENSURE(ad == rd);

    m.set(a, -0.875);
    m.to_rational(a, r);
    ad = m.to_double(a);
    rd = mq.get_double(r);
    ENSURE(ad == rd);

    m.set(a, 0.1);
    m.to_rational(a, r);
    ad = m.to_double(a);
    rd = mq.get_double(r);
#ifdef _WINDOWS    
    // CMW: This one depends on the rounding mode,
    // which is implicit in both hwf::set and in mpq::to_double.
    double diff = (ad-rd);
    ENSURE(diff >= -DBL_EPSILON && diff <= DBL_EPSILON);
#endif
}

static void bug_is_int() {
    unsigned raw_val[2] = { 2147483648u, 1077720461u };
    double   val;
    static_assert(sizeof(raw_val) == sizeof(val));
    memcpy(&val, raw_val, sizeof(val));
    std::cout << val << "\n";
    hwf_manager m;
    hwf a;
    m.set(a, val);
    ENSURE(!m.is_int(a));
} 

static void tst_scaled_rational() {
    hwf_manager m;
    unsynch_mpq_manager qm;
    scoped_mpq significand(qm);
    scoped_mpz exponent(qm);
    hwf value;
    // Unnormalized and negative significands must still represent s * 2^e.
    for (int numerator : {-5, -1, 0, 1, 5}) {
        qm.set(significand, numerator, 2);
        qm.set(exponent, -1);
        m.set(value, MPF_ROUND_NEAREST_TEVEN, significand, exponent);
        ENSURE(m.to_double(value) == numerator * 0.25);
    }
    for (bool negative : {false, true}) {
        qm.set(significand, negative ? -1 : 1);
        qm.set(exponent, -1075);
        double sign = negative ? -1.0 : 1.0;
        for (auto rm : {MPF_ROUND_NEAREST_TEVEN, MPF_ROUND_NEAREST_TAWAY,
                       MPF_ROUND_TOWARD_ZERO, MPF_ROUND_TOWARD_POSITIVE, MPF_ROUND_TOWARD_NEGATIVE}) {
            bool away = rm == MPF_ROUND_NEAREST_TAWAY ||
                rm == (negative ? MPF_ROUND_TOWARD_NEGATIVE : MPF_ROUND_TOWARD_POSITIVE);
            m.set(value, rm, significand, exponent);
            double actual = m.to_double(value);
            ENSURE(actual == (away ? sign * std::numeric_limits<double>::denorm_min() : 0.0));
            ENSURE(std::signbit(actual) == negative);
        }
        qm.set(exponent, 1024);
        m.set(value, MPF_ROUND_TOWARD_ZERO, significand, exponent);
        ENSURE(m.to_double(value) == sign * std::numeric_limits<double>::max());
        m.set(value, MPF_ROUND_NEAREST_TEVEN, significand, exponent);
        ENSURE(m.to_double(value) == sign * std::numeric_limits<double>::infinity());
    }
}

void tst_hwf() {
    tst_scaled_rational();
    bug_is_int();
    bug_set_double();
    bug_to_rational();
}
