/*++
Copyright (c) 2026 Microsoft Corporation

--*/

#include "ast/array_decl_plugin.h"
#include "ast/bv_decl_plugin.h"
#include "ast/reg_decl_plugins.h"
#include "model/model_evaluator.h"
#include "solver/solver.h"
#include "tactic/fd_solver/smtfd_solver.h"

static void test_array_extensionality(unsigned arity, bool use_function, unsigned num_reads, bool different = false) {
    ast_manager m;
    reg_decl_plugins(m);
    bv_util bv(m);
    array_util arrays(m);
    sort_ref index(bv.mk_sort(1), m), value(bv.mk_sort(2), m);
    ptr_vector<sort> domains(arity, index);
    sort_ref array_sort(arrays.mk_array_sort(arity, domains.data(), value), m);
    expr_ref a(m.mk_const("a", array_sort), m), b(m.mk_const("b", array_sort), m);
    expr_ref_vector assertions(m);
    if (use_function) {
        func_decl_ref f(m.mk_func_decl(symbol("f"), array_sort, value), m);
        expr_ref fa(m.mk_app(f.get(), a.get()), m), fb(m.mk_app(f.get(), b.get()), m);
        assertions.push_back(m.mk_eq(fa, bv.mk_numeral(0, 2)));
        assertions.push_back(m.mk_eq(fb, bv.mk_numeral(1, 2)));
    }
    else {
        expr_ref eq(m.mk_eq(a, b), m);
        assertions.push_back(m.mk_not(eq));
    }

    // Identical observed cells require a distinguishing index elsewhere.
    for (unsigned i = 0; i < num_reads; ++i) {
        expr_ref_vector args(m);
        args.push_back(a);
        for (unsigned j = 0; j < arity; ++j)
            args.push_back(bv.mk_numeral((i >> j) & 1, 1));
        expr_ref va(arrays.mk_select(args), m);
        args[0] = b;
        expr_ref vb(arrays.mk_select(args), m);
        assertions.push_back(m.mk_eq(va, bv.mk_numeral(0, 2)));
        assertions.push_back(m.mk_eq(vb, bv.mk_numeral(different && i == 0 ? 1 : 0, 2)));
    }

    ref<solver> s = mk_smtfd_solver(m, params_ref::get_empty());
    for (expr* f : assertions)
        s->assert_expr(f);
    lbool expected = num_reads == (1u << arity) && !different ? l_false : l_true;
    ENSURE(s->check_sat(0, nullptr) == expected);
    if (expected == l_false)
        return;
    model_ref mdl;
    s->get_model(mdl);
    ENSURE(mdl);
    model_evaluator eval(*mdl);
    for (expr* f : assertions)
        ENSURE(m.is_true(eval(f)));
}

void tst_smtfd() {
    for (unsigned arity : {1u, 2u}) {
        for (bool use_function : {false, true}) {
            test_array_extensionality(arity, use_function, 0);
            test_array_extensionality(arity, use_function, (1u << arity) - 1);
            test_array_extensionality(arity, use_function, 1u << arity);
            test_array_extensionality(arity, use_function, 1u << arity, true);
        }
    }
}
