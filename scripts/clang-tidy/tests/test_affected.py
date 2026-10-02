# Copyright (c) 2026 Microsoft Corporation
# SPDX-License-Identifier: MIT
import importlib.util
import json
import pathlib
import shutil
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('affected', ROOT / 'affected.py')
affected = importlib.util.module_from_spec(spec)
spec.loader.exec_module(affected)
SCANNER = shutil.which('clang-scan-deps-21')
COMPILER = shutil.which('clang++-21')


@unittest.skipUnless(SCANNER and COMPILER, 'requires LLVM 21 dependency scanner')
class Dependencies(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = pathlib.Path(self.temp.name)
        self.roots = [(root / side / 'source', root / side / 'build') for side in ['base', 'head']]
        for source, build in self.roots:
            (source / 'src').mkdir(parents=True)
            build.mkdir()
            for name, text in {
                'a.cpp': '#include "middle.h"\n',
                'b.cpp': '#include "leaf.h"\n',
                'unrelated.cpp': 'int unrelated;\n',
                'generated.cpp': '#include "generated.h"\n',
                'middle.h': '#include "leaf.h"\n',
                'leaf.h': 'int leaf;\n',
            }.items():
                (source / 'src' / name).write_text(text)
            (build / 'generated.h').write_text('#define GENERATED 1\n')

    def scan(self):
        results = []
        for source, build in self.roots:
            commands = [dict(directory=str(build), file=str(file), arguments=[COMPILER,
                '-std=c++20', '-I' + str(source / 'src'), '-I' + str(build),
                '-c', str(file), '-o', str(build / (file.name + '.o'))])
                for file in sorted((source / 'src').glob('*.cpp'))]
            (build / 'compile_commands.json').write_text(json.dumps(commands))
            units = affected.compilation_units(source, build)
            deps = affected.scan_dependencies(SCANNER, units, source, build, 2)
            results.append((units, deps))
        return results

    def select(self, changed):
        (base, base_deps), (head, head_deps) = self.scan()
        return affected.select_units(base, head, base_deps, head_deps, changed,
                                     self.roots[0][1], self.roots[1][1])

    def test_transitive_header_includes_and_unchanged_counterpart(self):
        # The head no longer includes the edited header. Its counterpart must
        # still run, or the warning removed from that TU cannot be compared.
        (self.roots[1][0] / 'src/middle.h').write_text('// include removed\n')
        before, after = self.select(['src/leaf.h', 'src/middle.h'])
        self.assertEqual(['source/src/a.cpp', 'source/src/b.cpp'], before)
        self.assertEqual(before, after)

    def test_source_change_does_not_select_unrelated_header_users(self):
        before, after = self.select(['src/a.cpp'])
        self.assertEqual(['source/src/a.cpp'], before)
        self.assertEqual(before, after)

    def test_deleted_header_and_added_and_deleted_sources(self):
        source = self.roots[1][0] / 'src'
        (source / 'leaf.h').unlink()
        (source / 'middle.h').write_text('// header removed\n')
        (source / 'b.cpp').unlink()
        (source / 'new.cpp').write_text('int added;\n')
        before, after = self.select(['src/leaf.h', 'src/middle.h', 'src/b.cpp', 'src/new.cpp'])
        self.assertEqual(['source/src/a.cpp', 'source/src/b.cpp'], before)
        self.assertEqual(['source/src/a.cpp', 'source/src/new.cpp'], after)

    def test_generated_header_changes_are_detected_without_git_paths(self):
        (self.roots[1][1] / 'generated.h').write_text('#define GENERATED 2\n')
        before, after = self.select(['scripts/generate_header.py'])
        self.assertEqual(['source/src/generated.cpp'], before)
        self.assertEqual(before, after)

    def test_documentation_change_has_empty_selection(self):
        self.assertEqual(([], []), self.select(['README.md']))

    def test_compilation_flags_are_compared_without_checkout_paths(self):
        (base, _), (head, _) = self.scan()
        before = affected.command_signatures(base, *self.roots[0])
        after = affected.command_signatures(head, *self.roots[1])
        self.assertEqual(before, after)
        head['source/src/a.cpp'][0]['arguments'].append('-DCHANGED')
        self.assertNotEqual(before, affected.command_signatures(head, *self.roots[1]))


class Configuration(unittest.TestCase):
    def test_build_and_checker_changes_require_full_coverage(self):
        for path in ['CMakeLists.txt', 'src/ast/CMakeLists.txt', 'cmake/flags.cmake',
                     'scripts/clang-tidy/AstArgumentOrderCheck.cpp', '.clang-tidy',
                     '.github/workflows/ast-order-warning-report.yml']:
            self.assertTrue(affected.configuration_changed([path]), path)
        self.assertFalse(affected.configuration_changed(['src/ast/ast.h', 'README.md']))


if __name__ == '__main__':
    unittest.main()
