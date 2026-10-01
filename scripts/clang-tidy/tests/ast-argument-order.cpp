// Copyright (c) 2026 Microsoft Corporation
// SPDX-License-Identifier: MIT
// Minimal declarations keep the checker tests independent of a Z3 build.
struct expr { unsigned get_id() const; };
struct ast_manager {
    expr* mk_app(unsigned);
    expr* mk_and(expr* a, expr* b) { return mk_app(a->get_id() + b->get_id()); }
    expr* mk_le(expr* a, expr* b) const;
    expr* mk_true() const { return cached; }
    expr* cached;
};
expr* ast_manager::mk_le(expr*, expr*) const {
    return const_cast<ast_manager*>(this)->mk_app(0);
}
expr* combine(expr*, expr*);
template<typename Object, typename Manager> struct obj_ref {};
template<typename Object, typename Manager> struct ref_vector {};
using expr_ref = obj_ref<expr, ast_manager>;
using expr_ref_vector = ref_vector<expr, ast_manager>;
expr* mk_and(ast_manager&, unsigned, expr* const*);
expr_ref mk_and(expr_ref_vector const&);
expr* mk_or(ast_manager&, unsigned, expr* const*);
expr* mk_not(ast_manager&, expr*);
expr_ref mk_not(expr_ref const&);
expr* mk_distinct(ast_manager&, unsigned, expr* const*);
expr* expand_distinct(ast_manager&, unsigned, expr* const*);
expr_ref push_not(expr_ref const&, unsigned = 8);
struct probe {};
probe* mk_not(probe*);
probe* mk_and(probe*, probe*);
probe* mk_or(probe*, probe*);
// These factories allocate even when their definitions are not in this TU.
struct bv_util { expr* mk_numeral(unsigned, unsigned) const; };
struct arith_decl_plugin { expr* mk_numeral(unsigned, bool); };
struct arith_util {
    arith_decl_plugin& plugin() const;
    expr* mk_numeral(unsigned n, bool is_int) const { return plugin().mk_numeral(n, is_int); }
};
struct fpa_decl_plugin { expr* mk_numeral(unsigned); };
struct fpa_util {
    expr* mk_nan(unsigned, unsigned);
    expr* mk_pinf(unsigned, unsigned);
    expr* mk_ninf(unsigned, unsigned);
    expr* mk_pzero(unsigned, unsigned);
    expr* mk_nzero(unsigned, unsigned);
};
namespace seq {
struct skolem {
    expr* mk(unsigned, expr*, expr*);
    expr* mk_eq(expr* a, expr* b) { return mk(0, a, b); }
};
}
struct seq_decl_plugin { expr* mk_string(char const*); };
struct seq_util {
    expr* mk_skolem(unsigned, expr*);
    struct str { expr* mk_string(char const*) const; } str;
};
template<typename Core> struct poly_rewriter {
    expr* mk_add(expr*, expr*);
    expr* mk_mul(expr*, expr*);
    expr* mk_sub(expr*, expr*);
    expr* mk_uminus(expr*);
    expr* cached() const;
};
struct arith_core {};
struct bv_core {};
struct arith_rewriter : poly_rewriter<arith_core> {
    expr* mk_lt_core(expr*, expr*);
    expr* mk_gt_core(expr*, expr*);
    expr* mk_lt(expr* a, expr* b) { return mk_lt_core(a, b); }
};
struct bool_rewriter {
    expr* mk_eq_core(expr*, expr*);
    expr* mk_eq_plain(expr*, expr*);
    expr* mk_eq(expr* a, expr* b) { return mk_eq_plain(a, b); }
};
void ids(unsigned, unsigned);
template<typename A, typename B> void consume(A, B) {}
struct pair {
    pair(expr*, expr*);
};
struct holder {
    holder(expr*);
    void accept(expr*);
};
struct allocating {
    allocating(ast_manager& m) { m.mk_app(1); }
};
expr* forward(ast_manager&);
expr* recursive_a(ast_manager&, int);
expr* recursive_b(ast_manager& m, int n) {
    return n ? recursive_a(m, n - 1) : m.mk_app(1);
}
expr* recursive_a(ast_manager& m, int n) { return recursive_b(m, n); }

void examples(ast_manager& m, expr* n, expr* k, expr* zero) {
    m.mk_and(m.mk_le(n, zero), m.mk_le(k, zero)); // warning
    combine(m.mk_app(1), m.mk_app(2)); // warning
    auto* a = m.mk_le(n, zero);
    auto* b = m.mk_le(k, zero);
    m.mk_and(a, b);
    ids(n->get_id(), k->get_id());
    m.mk_and(m.mk_true(), m.mk_true());
    m.mk_and(m.mk_app(1), zero);
    m.mk_app(m.mk_app(1)->get_id()); // Nesting alone is safe.

    pair p(m.mk_app(1), m.mk_app(2)); // warning
    pair q{m.mk_app(1), m.mk_app(2)};
    expr* array[] = {m.mk_app(1), m.mk_app(2)};
    (void)array;
    consume(allocating(m), m.mk_app(2)); // warning
    holder(m.mk_app(1)).accept(m.mk_app(2)); // Object precedes arguments.

    consume(sizeof(m.mk_app(1)), m.mk_app(2));
    consume(noexcept(m.mk_app(1)), m.mk_app(2));
    using T = decltype(combine(m.mk_app(1), m.mk_app(2)));
    (void)sizeof(T);
    (void)sizeof(combine(m.mk_app(1), m.mk_app(2)));
    (void)noexcept(combine(m.mk_app(1), m.mk_app(2)));
    consume([&] { return m.mk_app(1); }, m.mk_app(2));
    consume([x = m.mk_app(1)] { return x; }, m.mk_app(2)); // warning
    auto build = [&](unsigned i) { return m.mk_app(i); };
    combine(build(1), build(2)); // warning
    auto dispatch = [](expr* x, expr* y) { return combine(x, y); };
    dispatch(m.mk_app(1), m.mk_app(2)); // warning
    consume([&] { return m.mk_app(1); }(), m.mk_app(2)); // warning
    combine(forward(m), m.mk_app(2)); // warning
    combine(recursive_a(m, 2), recursive_b(m, 2)); // warning
    combine(recursive_b(m, 2), recursive_a(m, 2)); // warning
    // NOLINTNEXTLINE(z3-ast-argument-order)
    combine(m.mk_app(1), m.mk_app(2));
    if constexpr (false) {
        combine(m.mk_app(1), m.mk_app(2));
    }
}
expr* forward(ast_manager& m) { return m.mk_app(0); }
extern ast_manager global_manager;
auto global = combine(global_manager.mk_app(1), global_manager.mk_app(2)); // warning
extern expr* redeclared;
expr* redeclared = combine(global_manager.mk_app(1), global_manager.mk_app(2)); // warning
extern expr* redeclared;
expr* defaulted(expr*, expr* = global_manager.mk_app(0));
void defaults() {
    defaulted(global_manager.mk_app(1)); // warning
}
struct initialized {
    pair p;
    initialized(ast_manager& m) : p(m.mk_app(1), m.mk_app(2)) {} // warning
};
template<typename Manager> void instantiate(Manager& m) {
    combine(m.mk_app(1), m.mk_app(2)); // warning
}
void use_template(ast_manager& m) { instantiate(m); }

void numerals(ast_manager& m, bv_util const& bv, arith_util const& arith,
              fpa_decl_plugin& fp_plugin, fpa_util& fp) {
    combine(bv.mk_numeral(0, 1), bv.mk_numeral(1, 23)); // warning
    combine(bv.mk_numeral(23, 24), m.mk_app(1)); // warning
    combine(arith.mk_numeral(1, true), arith.mk_numeral(2, true)); // warning
    combine(fp_plugin.mk_numeral(1), m.mk_app(1)); // warning
    combine(fp.mk_nan(8, 24), m.mk_app(1)); // warning
    combine(fp.mk_pinf(8, 24), m.mk_app(1)); // warning
    combine(fp.mk_ninf(8, 24), m.mk_app(1)); // warning
    combine(fp.mk_pzero(8, 24), m.mk_app(1)); // warning
    combine(fp.mk_nzero(8, 24), m.mk_app(1)); // warning
    auto* zero = bv.mk_numeral(0, 1);
    auto* one = bv.mk_numeral(1, 23);
    combine(zero, one);
    pair sequenced{bv.mk_numeral(0, 1), bv.mk_numeral(1, 23)};
    consume(sizeof(bv.mk_numeral(0, 1)), bv.mk_numeral(1, 23));
}

void skolems(ast_manager& m, seq::skolem& sk, seq_util& seq, expr* a, expr* b) {
    combine(sk.mk(0, a, b), m.mk_app(1)); // warning
    combine(m.mk_app(1), sk.mk_eq(a, b)); // warning
    combine(seq.mk_skolem(0, a), m.mk_app(1)); // warning
    auto* eq = sk.mk_eq(a, b);
    combine(eq, m.mk_app(1));
    pair sequenced{sk.mk_eq(a, b), m.mk_app(1)};
}

void strings(ast_manager& m, seq_util& seq, seq_decl_plugin& plugin, expr* e) {
    combine(seq.str.mk_string("0"), seq.str.mk_string("9")); // warning
    combine(seq.str.mk_string("0"), m.mk_app(1)); // warning
    combine(plugin.mk_string("0"), m.mk_app(1)); // warning
    combine(seq.str.mk_string("0"), e);
    auto* zero = seq.str.mk_string("0");
    combine(zero, seq.str.mk_string("9"));
    pair sequenced{seq.str.mk_string("0"), seq.str.mk_string("9")};
}

void rewriters(ast_manager& m, arith_rewriter& arith, poly_rewriter<bv_core>& bv, bool_rewriter& bool_rw,
               expr* a, expr* b) {
    combine(arith.mk_add(a, b), m.mk_app(1)); // warning
    combine(arith.mk_mul(a, b), arith.mk_sub(a, b)); // warning
    combine(arith.mk_uminus(a), arith.mk_lt(a, b)); // warning
    combine(arith.mk_gt_core(a, b), bv.mk_mul(a, b)); // warning
    combine(bool_rw.mk_eq_core(a, b), arith.mk_mul(a, b)); // warning
    combine(bool_rw.mk_eq(a, b), bool_rw.mk_eq_plain(b, a)); // warning
    combine(arith.cached(), m.mk_app(1));
    auto* product = arith.mk_mul(a, b);
    combine(product, arith.mk_lt(a, b));
    pair sequenced{arith.mk_uminus(a), bv.mk_add(a, b)};
    consume(sizeof(arith.mk_sub(a, b)), arith.mk_mul(a, b));
}

void free_utilities(ast_manager& m, expr* e, expr* const* args,
                    expr_ref const& held, expr_ref_vector const& vec, probe* p) {
    combine(mk_not(m, e), m.mk_app(1)); // warning
    combine(mk_and(m, 2, args), m.mk_app(1)); // warning
    combine(mk_or(m, 2, args), m.mk_app(1)); // warning
    combine(mk_distinct(m, 2, args), m.mk_app(1)); // warning
    combine(expand_distinct(m, 2, args), m.mk_app(1)); // warning
    consume(push_not(held), m.mk_app(1)); // warning
    consume(mk_not(held), m.mk_app(1)); // warning
    consume(mk_and(vec), m.mk_app(1)); // warning
    auto negate = [&](expr* a) { return mk_not(m, a); };
    combine(negate(e), m.mk_app(1)); // warning
    auto* negated = mk_not(m, e);
    combine(negated, m.mk_app(1));
    pair sequenced{mk_not(m, e), m.mk_app(1)};
    consume(sizeof(mk_not(m, e)), m.mk_app(1));
    consume(mk_not(p), m.mk_app(1));
    consume(mk_and(mk_not(p), mk_not(p)), m.mk_app(1));
    consume(mk_or(p, p), m.mk_app(1));
}

// Similar names must not be mistaken for allocation roots.
namespace unrelated {
expr* mk_not(::ast_manager&, expr*);
expr* mk_and(::ast_manager&, unsigned, expr* const*);
void pure(::ast_manager& m, expr* e, expr* const* args) {
    combine(unrelated::mk_not(m, e), m.mk_app(1));
    combine(unrelated::mk_and(m, 2, args), m.mk_app(1));
}
template<typename Core> struct poly_rewriter { expr* mk_mul(expr*, expr*); };
struct arith_rewriter { expr* mk_lt_core(expr*, expr*); };
struct bool_rewriter { expr* mk_eq_plain(expr*, expr*); };
void pure(bool_rewriter& rw, expr* a, expr* b) { combine(rw.mk_eq_plain(a, b), rw.mk_eq_plain(b, a)); }
void pure(poly_rewriter<arith_core>& rw, expr* a, expr* b) { combine(rw.mk_mul(a, b), rw.mk_mul(b, a)); }
void pure(arith_rewriter& rw, expr* a, expr* b) { combine(rw.mk_lt_core(a, b), rw.mk_lt_core(b, a)); }
struct ast_manager { expr* mk_app(unsigned); };
struct bv_util { expr* mk_numeral(unsigned, unsigned); };
struct seq_util { struct str { expr* mk_string(char const*) const; } str; };
namespace seq { struct skolem { expr* mk(unsigned, expr*, expr*); }; }
void pure(ast_manager& m) { combine(m.mk_app(1), m.mk_app(2)); }
void pure(bv_util& bv) { combine(bv.mk_numeral(0, 1), bv.mk_numeral(1, 23)); }
void pure(seq_util& seq) { combine(seq.str.mk_string("0"), seq.str.mk_string("9")); }
void pure(seq::skolem& sk, expr* a, expr* b) { combine(sk.mk(0, a, b), sk.mk(1, a, b)); }
}
