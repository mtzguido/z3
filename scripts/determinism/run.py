#!/usr/bin/env python3
# Copyright (c) 2026 Microsoft Corporation
# SPDX-License-Identifier: MIT
"""Build Z3 variants and compare complete solver traces, byte for byte."""

import argparse
import concurrent.futures
import datetime
import hashlib
import itertools
import json
import os
from pathlib import Path
import re
import resource
import shlex
import shutil
import signal
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
PROFILES = ('gcc', 'clang', 'libcxx', 'libcxx-random')
CHANNELS = ('ast.trace', 'stdout', 'stderr')


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n')


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def output(command, **kwargs):
    return subprocess.check_output(command, text=True, **kwargs).strip()


def source_info(source):
    return {
        'revision': output(['git', '-C', str(source), 'rev-parse', 'HEAD']),
        'diff_sha256': hashlib.sha256(subprocess.check_output([
            'git', '-C', str(source), 'diff', 'HEAD', '--binary', '--',
            'src', 'CMakeLists.txt', 'cmake'])).hexdigest(),
    }


def build(args, profile, jobs):
    directory = args.work.resolve() / profile
    directory.mkdir(parents=True, exist_ok=True)
    source = args.source.resolve()
    compiler = shutil.which(args.gcc if profile == 'gcc' else args.clang)
    if not compiler:
        raise ValueError(f'compiler not found for {profile}')
    flags, link_flags = [], []
    if profile == 'clang':
        # Use the same libstdc++ headers as the selected GCC, even if a newer
        # GCC installation is also visible to Clang on this host.
        gcc_lib = output([args.gcc, '-print-libgcc-file-name'])
        flags.append('--gcc-install-dir=' + str(Path(gcc_lib).resolve().parent))
    if profile.startswith('libcxx'):
        if args.libcxx_include:
            flags += ['-nostdinc++', '-isystem', str(args.libcxx_include.resolve())]
            link_flags.append('-stdlib=libc++')
        else:
            flags.append('-stdlib=libc++')
        if args.libcxx_lib:
            lib = str(args.libcxx_lib.resolve())
            link_flags += ['-L' + lib, '-Wl,-rpath,' + lib]
        if profile == 'libcxx-random':
            flags += ['-D_LIBCPP_DEBUG_RANDOMIZE_UNSPECIFIED_STABILITY',
                      '-D_LIBCPP_DEBUG_RANDOMIZE_UNSPECIFIED_STABILITY_SEED=' + str(args.sort_seed)]
    command = ['cmake', '-S', str(source), '-B', str(directory / 'build'), '-G', 'Ninja',
               '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_CXX_STANDARD=20',
               '-DCMAKE_CXX_COMPILER=' + compiler, '-DCMAKE_CXX_FLAGS=' + shlex.join(flags),
               '-DCMAKE_EXE_LINKER_FLAGS=' + shlex.join(link_flags),
               '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
               '-DZ3_INCLUDE_GIT_HASH=OFF', '-DZ3_INCLUDE_GIT_DESCRIBE=OFF',
               '-DZ3_BUILD_TEST_EXECUTABLES=OFF', '-DZ3_ENABLE_EXAMPLE_TARGETS=OFF']
    env = dict(os.environ, LC_ALL='C')
    if shutil.which('ccache'):
        command.append('-DCMAKE_CXX_COMPILER_LAUNCHER=' + shutil.which('ccache'))
        env.setdefault('CCACHE_DIR', str(args.work.resolve() / 'ccache'))
        env.setdefault('CCACHE_BASEDIR', str(source))
        env.setdefault('CCACHE_MAXSIZE', '1G')
    start = time.monotonic()
    metadata = {'profile': profile, 'source': source_info(source),
                'compiler': output([compiler, '--version']), 'configure_command': command,
                'sort_seed': args.sort_seed if profile == 'libcxx-random' else None}
    # A stale success record must not survive a failed incremental rebuild.
    (directory / 'build.json').unlink(missing_ok=True)
    commands = [('configure', command), ('build', ['cmake', '--build', str(directory / 'build'),
                                                 '--target', 'shell', '--parallel', str(jobs)])]
    for stage, cmd in commands:
        print(f'{profile}: {stage} (log: {directory / (stage + ".log")})', flush=True)
        with (directory / (stage + '.log')).open('w') as log:
            result = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, env=env)
        if result.returncode:
            raise RuntimeError(f'{profile}: {stage} failed; see {directory / (stage + ".log")}')
    if metadata['source'] != source_info(source):
        raise RuntimeError('source changed while building; rebuild before comparing')
    metadata['seconds'] = round(time.monotonic() - start, 2)
    metadata['binary_sha256'] = digest(directory / 'build/z3')
    write_json(directory / 'build.json', metadata)
    return f'{profile}={directory / "build/z3"}'


def binaries(entries):
    result = {}
    for entry in entries:
        name, path = entry.split('=', 1)
        if not re.fullmatch(r'[A-Za-z0-9_-]+', name) or name in result:
            raise ValueError(f'invalid or duplicate binary name: {name}')
        result[name] = Path(path).resolve(strict=True)
    return result


def run(args, entries):
    variants = binaries(entries)
    manifest = json.loads(args.corpus.read_text())
    cases = manifest['cases']
    if not cases or len({c['file'] for c in cases}) != len(cases):
        raise ValueError('the corpus must be nonempty, with unique input paths')
    inputs = []
    for case in cases:
        name = Path(case['file'])
        if name.is_absolute() or '..' in name.parts:
            raise ValueError(f'input must be relative to the suite: {name}')
        data = (args.suite / name).read_bytes()
        if hashlib.sha256(data).hexdigest() != case['sha256']:
            raise ValueError(f'corpus hash mismatch: {name}')
        inputs.append(data)
    common = ['-smt2', 'input.smt2', 'trace=true', 'trace_file_name=ast.trace',
              '-v:' + str(args.verbosity), 'suppress_platform_verbose=true',
              'parallel.enable=false', 'smt.threads=1', 'sat.threads=1',
              'smt.sls.parallel=false', 'smt.random_seed=0', 'sat.random_seed=0',
              'nlsat.seed=0', 'rlimit=' + str(args.rlimit)]
    env = dict(os.environ, LC_ALL='C', TZ='UTC')
    # Children inherit these limits. Reaching either guard is an incomplete run,
    # never a pass. No preexec_fn is used with the thread pool.
    resource.setrlimit(resource.RLIMIT_FSIZE, (128 * 1024 * 1024, 128 * 1024 * 1024))
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    out = args.out.resolve()
    for name, binary in variants.items():
        directory = out / name
        directory.mkdir(parents=True, exist_ok=False)
        build_file = binary.parent.parent / 'build.json'
        build_metadata = json.loads(build_file.read_text()) if build_file.exists() else None
        binary_hash = digest(binary)
        if build_metadata and build_metadata['binary_sha256'] != binary_hash:
            raise ValueError(f'binary no longer matches its build metadata: {binary}')
        write_json(directory / 'metadata.json', {
            'profile': name, 'binary': str(binary), 'binary_sha256': binary_hash,
            'version': output([str(binary), '-version']), 'build': build_metadata,
            'corpus': manifest, 'arguments': common, 'repeats': args.repeats,
            'watchdog_seconds': args.watchdog, 'max_file_bytes': 128 * 1024 * 1024,
        })

    def execute(task):
        name, index, repeat = task
        relative = Path(f'{index:03d}') / str(repeat)
        directory = out / name / relative
        directory.mkdir(parents=True)
        (directory / 'input.smt2').write_bytes(inputs[index])
        command = [str(variants[name]), *common]
        timed_out = False
        start = time.monotonic()
        with (directory / 'stdout').open('wb') as stdout, (directory / 'stderr').open('wb') as stderr:
            child = subprocess.Popen(command, cwd=directory, env=env, stdout=stdout, stderr=stderr,
                                     start_new_session=True)
            try:
                code = child.wait(timeout=args.watchdog)
            except subprocess.TimeoutExpired:
                timed_out = True
                try:
                    os.killpg(child.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                code = child.wait()
        trace = directory / 'ast.trace'
        complete_trace = False
        if trace.exists() and trace.stat().st_size >= 6:
            with trace.open('rb') as stream:
                header = stream.readline().startswith(b'[tool-version] Z3 ')
                stream.seek(-6, 2)
                complete_trace = header and stream.read() == b'[eof]\n'
        smt_error = bool(re.search(rb'^\(error(?:\s|\))', (directory / 'stdout').read_bytes(), re.M))
        result = {'case_index': index, 'repeat': repeat, 'directory': str(relative),
                  'returncode': code, 'watchdog': timed_out, 'smtlib_error': smt_error,
                  'trace_complete': complete_trace,
                  'complete': code == 0 and not timed_out and complete_trace and not smt_error,
                  'seconds': round(time.monotonic() - start, 4),
                  'channels': {c: {'bytes': (directory / c).stat().st_size,
                                   'sha256': digest(directory / c)}
                               for c in CHANNELS if (directory / c).exists()}}
        write_json(directory / 'run.json', result)
        return name, result

    tasks = list(itertools.product(variants, range(len(cases)), range(args.repeats)))
    collected = {name: [] for name in variants}
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for count, (name, result) in enumerate(pool.map(execute, tasks), 1):
            collected[name].append(result)
            if count % 50 == 0 or count == len(tasks):
                print(f'{count}/{len(tasks)} runs finished', flush=True)
    for name, results in collected.items():
        write_json(out / name / 'runs.json', results)
    return int(any(not r['complete'] for rows in collected.values() for r in rows))


def difference(left, right):
    if not left.is_file() or not right.is_file():
        return {'missing': True}
    # Compare bytes, not just hashes. Read large traces in bounded blocks.
    with left.open('rb') as a, right.open('rb') as b:
        while True:
            x, y = a.read(65536), b.read(65536)
            if x != y:
                break
            if not x:
                return None
    with left.open('rb') as a, right.open('rb') as b:
        for line, (x, y) in enumerate(itertools.zip_longest(a, b, fillvalue=b''), 1):
            if x != y:
                return {'line': line, 'left': x[:500].decode(errors='backslashreplace').rstrip('\n'),
                        'right': y[:500].decode(errors='backslashreplace').rstrip('\n')}
    raise RuntimeError('output changed during comparison')


def compare(inputs, profiles, out):
    roots = {}
    for path in inputs:
        candidates = [path] if (path / 'runs.json').exists() else list(path.glob('*/runs.json'))
        for candidate in candidates:
            root = candidate if candidate.is_dir() else candidate.parent
            metadata = json.loads((root / 'metadata.json').read_text())
            name = metadata['profile']
            if name in roots:
                raise ValueError(f'duplicate report: {name}')
            roots[name] = root
    if set(roots) != set(profiles):
        raise ValueError(f'expected profiles {profiles}, found {sorted(roots)}')
    metadata = {name: json.loads((root / 'metadata.json').read_text()) for name, root in roots.items()}
    first = metadata[profiles[0]]
    cases, repeats = first['corpus']['cases'], first['repeats']
    if not cases or repeats < 2:
        raise ValueError('need a nonempty corpus and at least two repetitions')
    runs = {}
    for name, root in roots.items():
        for key in ('corpus', 'arguments', 'repeats', 'watchdog_seconds', 'max_file_bytes'):
            if metadata[name][key] != first[key]:
                raise ValueError(f'incompatible {key}: {name}')
        if metadata[name]['build'] and first['build']:
            if metadata[name]['build']['source'] != first['build']['source']:
                raise ValueError(f'different source revisions: {name}')
        rows = json.loads((root / 'runs.json').read_text())
        expected = set(itertools.product(range(len(cases)), range(repeats)))
        keys = [(r['case_index'], r['repeat']) for r in rows]
        if len(keys) != len(expected) or set(keys) != expected:
            raise ValueError(f'incomplete or duplicate run inventory: {name}')
        for r in rows:
            runs[name, r['case_index'], r['repeat']] = r
            # Detect truncated/corrupted artifacts even if both sides lost the same data.
            for channel, info in r['channels'].items():
                file = root / r['directory'] / channel
                if not file.is_file() or file.stat().st_size != info['bytes'] or digest(file) != info['sha256']:
                    raise ValueError(f'corrupted output: {file}')
    comparisons = []
    def check(a, b, kind):
        ra, rb = runs[a], runs[b]
        da, db = roots[a[0]] / ra['directory'], roots[b[0]] / rb['directory']
        diffs = {}
        for channel in CHANNELS:
            d = difference(da / channel, db / channel)
            if d:
                diffs[channel] = d
        complete = ra['complete'] and rb['complete']
        comparisons.append({'kind': kind, 'case': cases[a[1]]['file'], 'left': a, 'right': b,
                            'complete': complete, 'differences': diffs,
                            'match': complete and not diffs})
    for name in profiles:
        for index in range(len(cases)):
            for repeat in range(1, repeats):
                check((name, index, 0), (name, index, repeat), 'repeat')
            if name != profiles[0]:
                check((profiles[0], index, 0), (name, index, 0), 'cross')
    summary = []
    for kind in ('repeat', 'cross'):
        for name in profiles:
            rows = [c for c in comparisons if c['kind'] == kind and c['right'][0] == name]
            if rows:
                summary.append({'kind': kind, 'profile': name, 'total': len(rows),
                                'matches': sum(c['match'] for c in rows),
                                'incomplete': sum(not c['complete'] for c in rows),
                                'differences': {ch: sum(ch in c['differences'] for c in rows) for ch in CHANNELS}})
    failures = [c for c in comparisons if not c['match']]
    out.mkdir(parents=True, exist_ok=True)
    write_json(out / 'comparison.json', {'summary': summary, 'comparisons': comparisons})
    lines = ['# Z3 determinism', '', f'{len(cases)} inputs; {len(profiles)} configurations; {repeats} repetitions.',
             f'Baseline: `{profiles[0]}`. All channels compared byte for byte.', '',
             '| Comparison | Configuration | Exact matches | Incomplete | AST differences | stdout | stderr |',
             '|---|---|---:|---:|---:|---:|---:|']
    for row in summary:
        d = row['differences']
        lines.append(f'| {row["kind"]} | {row["profile"]} | {row["matches"]}/{row["total"]} | '
                     f'{row["incomplete"]} | {d["ast.trace"]} | {d["stdout"]} | {d["stderr"]} |')
    if failures:
        lines += ['', '<details><summary>First differences (up to 20 comparisons)</summary>', '']
        for c in failures[:20]:
            lines += [f'`{c["case"]}`: {c["left"][0]} → {c["right"][0]} ({c["kind"]})', '']
            if not c['complete']:
                lines += ['Incomplete run; see the corresponding run.json.', '']
            for channel, d in c['differences'].items():
                lines += [f'{channel}, line {d.get("line", "missing")}:', '', '```diff',
                          '-' + d.get('left', '<missing>'), '+' + d.get('right', '<missing>'), '```', '']
        lines += ['</details>', '']
    (out / 'summary.md').write_text('\n'.join(lines) + '\n')
    print('\n'.join(lines[:len(summary) + 7]))
    print(f'Full report: {out / "summary.md"}')
    return int(bool(failures))


def positive(text):
    value = int(text)
    if value <= 0:
        raise argparse.ArgumentTypeError('must be positive')
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    for name in ('build', 'run', 'compare', 'matrix'):
        p = commands.add_parser(name)
        if name in ('build', 'matrix'):
            p.add_argument('--source', type=Path, default=HERE.parent.parent)
            p.add_argument('--work', type=Path, default=Path('build/determinism'))
            p.add_argument('--gcc', default='g++')
            p.add_argument('--clang', default='clang++')
            p.add_argument('--libcxx-include', type=Path)
            p.add_argument('--libcxx-lib', type=Path)
            p.add_argument('--sort-seed', type=int, default=1)
        if name == 'build':
            p.add_argument('--profile', choices=PROFILES, required=True)
        if name in ('matrix', 'compare'):
            p.add_argument('--profiles', choices=PROFILES if name == 'matrix' else None,
                           nargs='+', default=list(PROFILES))
        if name in ('run', 'matrix'):
            p.add_argument('--suite', type=Path, required=True)
            p.add_argument('--corpus', type=Path, default=HERE / 'corpus.json')
            p.add_argument('--repeats', type=positive, default=2)
            p.add_argument('--rlimit', type=positive, default=1000000)
            p.add_argument('--verbosity', type=int, default=0)
            p.add_argument('--watchdog', type=positive, default=45)
        if name != 'compare':
            p.add_argument('--jobs', type=positive, default=min(8, os.cpu_count() or 1))
        if name == 'run':
            p.add_argument('--binary', action='append', required=True, metavar='NAME=PATH')
        if name == 'compare':
            p.add_argument('--input', action='append', type=Path, required=True)
        if name in ('run', 'compare'):
            p.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    if getattr(args, 'repeats', 2) < 2:
        parser.error('at least two repetitions are required')
    if hasattr(args, 'profiles') and len(set(args.profiles)) != len(args.profiles):
        parser.error('duplicate profile')
    if hasattr(args, 'sort_seed') and not 0 <= args.sort_seed < 2**64:
        parser.error('sorting seed must fit in an unsigned 64-bit integer')
    if args.command == 'build':
        build(args, args.profile, args.jobs)
        return 0
    if args.command == 'run':
        return run(args, args.binary)
    if args.command == 'compare':
        return compare(args.input, args.profiles, args.out)
    workers = min(len(args.profiles), args.jobs)
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        variants = list(pool.map(lambda p: build(args, p, max(1, args.jobs // workers)), args.profiles))
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    args.out = args.work.resolve() / 'reports' / stamp
    run(args, variants)
    return compare([args.out], args.profiles, args.out)


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f'error: {error}', file=sys.stderr)
        sys.exit(2)
