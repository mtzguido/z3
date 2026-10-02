# AST argument-order warnings

`z3-ast-argument-order` reports calls where separate arguments may allocate AST
nodes. C++ does not specify their relative evaluation order, so compilers can
assign different AST IDs and subsequently make different solver decisions.

```cpp
m.mk_and(a.mk_le(x, zero), a.mk_le(y, zero)); // warning
```

The diagnostic identifies two potentially allocating calls. Constructing and
retaining the arguments in separate statements makes their order explicit.
The checker **never offers or applies automatic fixes**. Its tests verify that
the exported diagnostics contain no replacements. The runner has no fix option.

## Build and run

Use LLVM/Clang **21.1.8**, with clang-tidy and the LLVM/Clang development packages
from the same installation. A plugin must match the clang-tidy binary loading it.
This tooling is independent of the compiler used for normal Z3 builds. The
generation-only commands below require CMake 3.27 or newer and Ninja.

```sh
cmake -G Ninja -S scripts/clang-tidy -B build-tidy \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang-21 -DCMAKE_CXX_COMPILER=clang++-21 \
  -DLLVM_DIR=/usr/lib/llvm-21/lib/cmake/llvm \
  -DCLANG_TIDY=/usr/bin/clang-tidy-21
cmake --build build-tidy
ctest --test-dir build-tidy --output-on-failure

cmake -G Ninja -S . -B build \
  -DCMAKE_CXX_COMPILER=clang++-21 \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DZ3_INCLUDE_GIT_HASH=OFF -DZ3_INCLUDE_GIT_DESCRIBE=OFF \
  -DCMAKE_ADD_CUSTOM_COMMAND_DEPENDS_EXPLICIT_ONLY=ON
cmake --build build --target src/api/api_log_macros.h src/ast/pattern/database.h
python3 scripts/clang-tidy/run.py \
  --plugin build-tidy/Z3TidyModule.so --build build --source-root . --output ast-order-report
```

Only the checker plugin is compiled. Z3's generated headers and sources are
produced by Python generators; Z3 itself is neither compiled nor linked.
[CMAKE_ADD_CUSTOM_COMMAND_DEPENDS_EXPLICIT_ONLY](https://cmake.org/cmake/help/latest/variable/CMAKE_ADD_CUSTOM_COMMAND_DEPENDS_EXPLICIT_ONLY.html)
prevents CMake from pulling
unneeded component builds into these generators, whose inputs are fully declared.
The runner checks source files in the compilation database, retaining individual
logs and deduplicating header warnings in `warnings.txt` and `summary.json`. Use `--filter`
to select paths and `--jobs` to control parallelism. Warnings do not fail the run;
compiler errors, plugin failures, and checker-test failures do.
The runner prints the final warning count and records `warning_count` and
`failed_translation_units` in `summary.json`. `--source-root` restricts diagnostics
to a checkout and writes relative paths, allowing comparisons across build trees.

## Pull-request reports

`.github/workflows/ast-order-warning-report.yml` runs on every PR, including
documentation-only PRs, pushes to master, and the nightly/manual triggers. It selects
LLVM 21 packages and rejects a version other than 21.1.8. PR runs compare the base
commit and GitHub's PR merge commit using the same checker source and compiler
version. Comparing the merge result avoids attributing intervening upstream fixes
to a PR that is behind its base branch. Pushes to master and nightly/manual runs
scan all translation units and report the revision's count without inventing a baseline.

On PRs, `affected.py` runs `clang-scan-deps-21` on both compilation databases.
It selects changed source files and every translation unit that directly or
transitively includes a changed header on either side. Both versions of those
translation units are checked, including when an include was removed. Added and
deleted translation units and changes to generated headers/sources are included.
The dependency scan preprocesses the code without compiling Z3. Build configuration,
compiler command, or checker changes trigger full scans. Documentation-only PRs
normally select no translation units and explicitly report that nothing needs checking.
The scan builds omit Git metadata from `z3_version.h`, so a new commit alone does
not make files depending on that generated header need analysis.

Base and head checks run in parallel. Each job performs the cheap dependency scans
of both revisions, avoiding another CI job and artifact transfer before analysis.
The runner uses `--selection ast-order-selection.json --revision base` (or `head`)
to apply the resulting plan. It reports all diagnostics in the selected files,
including headers and unchanged lines. PR comments label counts as warnings in
affected files and show the number of translation units checked on each side;
these are not whole-repository totals. Mismatched selections and incomplete scans
cannot produce a warning delta.

Each uncached scan uses all CPUs available to its runner, with the worker count
and elapsed time printed in the log. Completed scans are cached by LLVM version,
runner image/architecture, and a hash of the checker, its scripts, and the workflow.
Full reports use the source commit as their cache identity. PR reports include
both the base and tested merge commits and the scanned side, so a cached full
report or another comparison's affected set cannot be substituted accidentally.
Only exact cache hits skip preparation and analysis; restored reports are validated
before use. Failed scans are never cached. GitHub scopes PR caches to that PR.

The job log and Actions summary show, for example:

```text
Base: 46 → PR: 45; change: -1
```

The summary includes changed per-file counts, the tested revisions, and a collapsed
warning diff with `-` lines for removed warnings and `+` lines for added warnings.
Header diagnostics are deduplicated across translation units. Comparing the source
checkouts matches diagnostics on unchanged lines, so shifted line numbers do not
appear as warning changes. Large diffs are shortened in the comment; the comparison
artifact includes every changed diagnostic. Full logs and warning lists remain in run artifacts for
14 days. An incomplete scan fails the job and cannot produce a warning delta.
Warnings themselves remain advisory, and no automatic fixes are offered or applied.

`.github/workflows/ast-order-warning-comment.yml` updates one bot comment per PR
after the scan. It handles forks using a separate `workflow_run` job with comment
permission. That job checks out only the default branch, reads a size-bounded,
validated JSON artifact, and identifies the PR through GitHub's API. It never
executes PR code with its write token. Results for an outdated PR head/base are
skipped, and an older run cannot overwrite a newer comment. Failed scans report
that the comparison is unavailable.

The commenting workflow must first be merged into the default branch before
GitHub will trigger it. The initial PR introducing the workflows still has its
counts in the scan logs and Actions summary. Fork runs remain subject to the
repository's normal workflow-approval settings. See GitHub's
[workflow_run documentation](https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows#workflow_run).

For a local comparison, run `run.py --source-root ...` on each checkout with the
same plugin, then:

```sh
python3 scripts/clang-tidy/compare.py \
  --base base-report/summary.json --head head-report/summary.json \
  --base-sha "$base_commit" --head-sha "$head_commit" \
  --base-source /path/to/base --head-source /path/to/head --output comparison
node scripts/clang-tidy/comment.js comparison/comparison.json
```

Run the reporting tests without GitHub credentials or API writes:

```sh
python3 -m unittest discover -s scripts/clang-tidy/tests -p 'test_*.py'
node --test scripts/clang-tidy/tests/comment.test.js
```

## What the check knows

The allocation roots are explicitly named `ast_manager` entry points and
factories in `AstArgumentOrderCheck.cpp`. The latter include bit-vector,
arithmetic and floating-point numeral construction, floating-point special
values, sequence Skolem and string factories, polynomial, strict arithmetic
comparison and Boolean equality rewriters, and the free Boolean constructors
and negation helpers from `ast_util.cpp`. Their allocator calls are hidden in
other translation units or uninstantiated template bodies. Calls to
wrappers are classified by following direct calls in definitions visible in the
current translation unit. This includes
inline utilities, local lambdas, constructors, and recursive wrappers. It does
not infer effects from a `mk_` prefix or from C++ `const` qualifiers.
Free utilities must take an AST manager or its reference types; the overloads
that construct tactic probes are not AST allocation roots.

The checker examines ordinary function calls, function-call operators, and
parenthesized constructor calls. It respects unevaluated operands, discarded
`if constexpr` branches, braced constructor initialization, and the sequencing
of the called object before arguments. An uncalled lambda body does not count as
an effect of creating that lambda, but evaluating its capture initializers does.
Bodies of lambdas are independently checked for problematic calls within them.

This is a conservative review aid, not a proof of a platform discrepancy:

- A constructor may find an already interned AST, and allocate nothing on a
  particular call. Function summaries are not sensitive to argument values or
  runtime branch conditions.
- Calls using different AST managers can be reported; manager aliasing is not
  analyzed.
- Non-root functions defined only in another translation unit, indirect calls,
  virtual overrides, and destructor effects may be missed.
- Operator notation other than `operator()` and other unspecified-order
  expressions are outside the initial check's scope.
- This check does not analyze sorting, unordered-container iteration, or RNGs.

Review findings before changing solver code. A justified exception can use
`NOLINT(z3-ast-argument-order)` or `NOLINTNEXTLINE(z3-ast-argument-order)` with an
explanation.
