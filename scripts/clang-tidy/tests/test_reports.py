# Copyright (c) 2026 Microsoft Corporation
# SPDX-License-Identifier: MIT
import importlib.util
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('comparison', ROOT / 'compare.py')
comparison = importlib.util.module_from_spec(spec)
spec.loader.exec_module(comparison)
BASE, HEAD = 'a' * 40, 'b' * 40


def warning(file, line=1, column=1):
    return dict(file=file, line=line, column=column, message='allocating arguments')


class Reports(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)

    def summary(self, name, warnings, units=None, version='LLVM version 21.1.8'):
        path = self.root / name
        path.write_text(json.dumps(dict(check=comparison.CHECK, clang_tidy_version=version,
            translation_units=units if units is not None else [dict(returncode=0)], warnings=warnings)))
        return path

    def test_header_duplicates_and_line_shifts_do_not_inflate_delta(self):
        base = self.summary('base.json', [warning('src/shared.h')] * 2 + [warning('src/old.cpp')])
        head = self.summary('head.json', [warning('src/shared.h', 50),
            warning('src/new.cpp', 10, 2), warning('src/new.cpp', 10, 12)])
        result = comparison.compare(base, head, BASE, HEAD)
        self.assertEqual((2, 3), (result['base_count'], result['head_count']))
        self.assertEqual([dict(path='src/new.cpp', base=0, head=2),
                          dict(path='src/old.cpp', base=1, head=0)], result['files'])
        out = self.root / 'report'
        p = subprocess.run([sys.executable, str(ROOT / 'compare.py'), '--base', str(base),
            '--head', str(head), '--base-sha', BASE, '--head-sha', HEAD, '--output', str(out)],
            capture_output=True, text=True, check=True)
        self.assertIn('warnings: 3', p.stdout)
        self.assertIn('change: +1', p.stdout)
        self.assertEqual(result, json.loads((out / 'comparison.json').read_text()))

    def test_unchanged_total_can_have_changed_files(self):
        base = self.summary('base.json', [warning('src/old.cpp')])
        head = self.summary('head.json', [warning('src/new.cpp')])
        result = comparison.compare(base, head, BASE, HEAD)
        self.assertEqual(result['base_count'], result['head_count'])
        self.assertEqual(2, len(result['files']))

    def test_reject_incomplete_scan_and_version_mismatch(self):
        head = self.summary('head.json', [])
        for units in [[], [dict(returncode=0), dict(returncode=1)]]:
            base = self.summary('base.json', [], units=units)
            with self.assertRaisesRegex(ValueError, 'incomplete'):
                comparison.compare(base, head, BASE, HEAD)
        base = self.summary('base.json', [], version='LLVM version 20.1.8')
        with self.assertRaisesRegex(ValueError, 'different clang-tidy'):
            comparison.compare(base, head, BASE, HEAD)

    def test_reject_absolute_or_escaping_paths(self):
        for path in ['/checkout/src/test.cpp', '../other.cpp', 'src/file\nname.cpp']:
            head = self.summary('head.json', [warning(path)])
            with self.assertRaises(ValueError):
                comparison.compare(None, head, None, HEAD)

    def test_scheduled_scan_has_no_invented_baseline(self):
        head = self.summary('head.json', [warning('src/test.cpp')])
        result = comparison.compare(None, head, None, HEAD)
        self.assertEqual(1, result['head_count'])
        self.assertIsNone(result['base_count'])
        self.assertIsNone(result['base_sha'])

    def test_runner_counts_deduplicated_headers_and_reports_scan_failure(self):
        source = self.root / 'checkout'
        (source / 'src').mkdir(parents=True)
        database = []
        for name in ['a.cpp', 'b.cpp']:
            file = source / 'src' / name
            file.write_text('// fixture\n')
            database.append(dict(directory=str(source), file=str(file), arguments=['c++', '-c', str(file)]))
        (source / 'compile_commands.json').write_text(json.dumps(database))
        tidy = self.root / 'tidy'
        tidy.write_text('''#!/usr/bin/env python3
import pathlib, sys
if '--version' in sys.argv: print('LLVM version 21.1.8')
elif '--list-checks' in sys.argv: print('z3-ast-argument-order')
else:
    file = pathlib.Path(sys.argv[-1])
    for path in [file, file.parent / 'shared.h', file.parent.parent.parent / 'external.h']:
        print(f'{path}:1:2: warning: allocating arguments [z3-ast-argument-order]')
    if file.name == 'b.cpp' and (file.parent / 'fail').exists(): sys.exit(1)
''')
        tidy.chmod(0o755)
        for fail in [False, True]:
            if fail: (source / 'src/fail').touch()
            out = self.root / ('failed' if fail else 'passed')
            p = subprocess.run([sys.executable, str(ROOT / 'run.py'), '--clang-tidy', str(tidy),
                '--plugin', str(self.root / 'unused.so'), '--build', str(source),
                '--source-root', str(source), '--output', str(out), '--jobs', '2'],
                capture_output=True, text=True)
            self.assertEqual(int(fail), p.returncode, p.stderr)
            summary = json.loads((out / 'summary.json').read_text())
            self.assertEqual(3, summary['warning_count'])
            self.assertEqual(int(fail), summary['failed_translation_units'])
            self.assertIn('AST argument-order warnings: 3', p.stdout)
            self.assertEqual(fail, 'incomplete' in p.stdout)
            self.assertEqual({'src/a.cpp', 'src/b.cpp', 'src/shared.h'},
                             {w['file'] for w in summary['warnings']})


if __name__ == '__main__':
    unittest.main()
