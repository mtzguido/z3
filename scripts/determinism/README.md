# Solver determinism

Build several Z3 configurations from the same source and compare their solver
traces and output **byte for byte**. Each input also runs twice with each binary,
so a configuration must agree with itself. No lines are sorted, AST IDs renamed,
addresses replaced, or mismatches ignored.

The structured trace (`trace=true`) includes expression creation with AST IDs,
SMT assignments, conflicts and instantiations. The format manager does not write
to this trace: its nodes represent pretty-printer layouts, use a separate ID
space, and do not participate in solving. User expressions remain traced even
when their function names happen to be `compose`, `string`, or `indent`.

## Local use

Requires Linux, Python 3.11+, CMake, Ninja, GCC, Clang and libc++ development
packages. `ccache` is used when installed. For example, Ubuntu 24.04 provides
`g++-14 clang-18 libc++-18-dev libc++abi-18-dev ninja-build ccache`.

Check out the corpus at the revision recorded in `corpus.json`:

```sh
git clone https://github.com/Z3Prover/z3test ../z3test
revision=$(python3 -c 'import json; print(json.load(open("scripts/determinism/corpus.json"))["revision"])')
git -C ../z3test checkout "$revision"
```

Run all four configurations with a total of eight build jobs:

```sh
python3 scripts/determinism/run.py matrix \
  --suite ../z3test --gcc g++-14 --clang clang++-18 --jobs 8
```

The configurations are:

- `gcc`: GCC with libstdc++.
- `clang`: Clang with the selected GCC's libstdc++ headers.
- `libcxx`: Clang with libc++.
- `libcxx-random`: Clang with libc++'s unspecified-order randomization, seed 1.

Compilers are configurable; `--gcc g++-16 --clang clang++-22` works on hosts
with those versions. For a private libc++ installation, use `--libcxx-include`
for its `include/c++/v1` directory and `--libcxx-lib` for its library directory.

Use `--profiles gcc clang` for a smaller matrix. Build directories and the
compiler cache are reused under `build/determinism`; each run gets a new timestamped
report directory. `--work` changes that location. The total build concurrency is
bounded by `--jobs`, divided among the selected configurations. `CCACHE_DIR` and
`CCACHE_MAXSIZE` can override the local cache defaults. Use a separate `--work`
directory when switching compiler or library installations.

To use existing binaries (which must contain the format-manager trace change):

```sh
python3 scripts/determinism/run.py run --suite ../z3test --out /tmp/z3-runs \
  --binary gcc=/path/to/gcc/z3 --binary clang=/path/to/clang/z3
python3 scripts/determinism/run.py compare --input /tmp/z3-runs \
  --profiles gcc clang --out /tmp/z3-comparison
```

`run` checks successful completion; `compare` checks equality. The combined
`matrix` command does both and exits nonzero for a mismatch or incomplete run.
`build --profile NAME` is also available for separate build jobs.

## What is checked

The committed manifest selects 100 files from a pinned Z3Prover/z3test revision,
with a SHA-256 hash for each input. The sample takes ten files per syntactic
category (arrays, bitvectors, datatypes, floating point, linear arithmetic,
nonlinear arithmetic, optimization, other, quantifiers, strings/sequences),
ordered by a fixed filename hash. It was selected before comparing results.
Files with explicit timeout/seed/thread/trace controls, reset/include, statistics
queries, expected errors, or no check-sat were excluded. `--corpus` accepts another
manifest of the same form; the input hashes must match.

Every run uses a fresh process, a private directory, identical input bytes,
fixed solver seeds, single-threaded solving, and `LC_ALL=C`. Three files are
compared separately: `ast.trace`, `stdout`, and `stderr`. Normal verbosity defaults
to zero; structured tracing is still enabled. `--verbosity 10` enables additional
diagnostics, but some diagnostic paths still print time/memory or iterate unordered
containers. These differences are reported and fail the comparison too.

The default `rlimit=1000000` bounds solver work. An external 45-second watchdog
and a 128 MiB limit per output file guard against runaway jobs. A run must exit
zero, produce no SMT-LIB error, and end its trace with `[eof]`. Timeouts, crashes,
truncated output, missing artifacts, and corpus mismatches cannot pass. A completed
`unknown` answer is compared like any other answer; this is a determinism check,
not an expected-answer regression suite.

The report contains exact-match counts and the first differing lines, and case identifiers
for the preserved inputs and logs. JSON metadata records compiler and
build commands, source revision and tracked source diff hash, binary hashes, corpus,
seeds, exit statuses and output hashes. Artifact hashes are rechecked on comparison.

The trace is not an exhaustive log of every engine, and tracing may affect AST
creation. Internal wall-clock `try_for` tactics can still affect behavior even
with a resource limit; such differences need investigation. Passing a small corpus
with one sorting seed does not prove universal determinism.

## Randomized sorting

The randomized profile defines both:

```text
_LIBCPP_DEBUG_RANDOMIZE_UNSPECIFIED_STABILITY
_LIBCPP_DEBUG_RANDOMIZE_UNSPECIFIED_STABILITY_SEED=1
```

libc++ uses a separate generator for this randomization, consuming neither Z3's
random generator nor C `rand()`. The fixed seed is essential: otherwise the
implementation can seed from an address and change with ASLR. Use `--sort-seed N`
to test another order; this is a compile-time option and requires rebuilding the
randomized configuration. The ordinary libc++ profile gives a direct control.
The feature covers unspecified ordering in `sort`, `partial_sort` and `nth_element`,
not Z3's custom sorting routines. See the [libc++ design document](https://libcxx.llvm.org/DesignDocs/UnspecifiedBehaviorRandomization.html);
the seed macro above follows the installed headers, whose spelling differs from
some versions of that document.

## GitHub Actions

`.github/workflows/determinism.yml` runs on every PR, pushes to master, and manual
dispatch. Four parallel Ubuntu 24.04 jobs use explicitly selected GCC 14/Clang 18
packages and a compiler cache per configuration. The corpus revision and hashes
are fixed; distribution package updates within those compiler versions remain
possible and package versions are printed in the job log.

The comparison job checks that all four complete result sets exist, verifies their
provenance and output hashes, and fails on any raw difference. It writes a job
summary with expandable first differences and retains raw runs and build logs as
artifacts. Fork PRs use the ordinary read-only `pull_request` workflow; no privileged
reporting workflow or PR comment is needed. Pushes to master also warm the caches
that later PRs can restore.
