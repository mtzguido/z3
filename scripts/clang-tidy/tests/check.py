#!/usr/bin/env python3
# Copyright (c) 2026 Microsoft Corporation
# SPDX-License-Identifier: MIT
"""Check warning locations, negative cases, suppression, and absence of fixes."""
import pathlib
import re
import subprocess
import sys
import tempfile


def main():
    tidy, plugin, source = sys.argv[1:]
    source = pathlib.Path(source).resolve()
    original = source.read_bytes()
    expected = {i for i, line in enumerate(original.decode().splitlines(), 1)
                if "// warning" in line}
    for standard in ("c++17", "c++20"):
        with tempfile.TemporaryDirectory() as temp:
            diagnostics = pathlib.Path(temp) / "diagnostics.yaml"
            result = subprocess.run([
                tidy, "--load=" + plugin, "--config={Checks: '-*,z3-ast-argument-order', WarningsAsErrors: ''}",
                "--export-fixes=" + str(diagnostics), str(source), "--", "-std=" + standard,
            ], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            actual = {int(line) for line in re.findall(
                re.escape(str(source)) + r":(\d+):\d+: warning: .*\[z3-ast-argument-order\]", result.stdout)}
            if result.returncode or actual != expected:
                print(result.stdout)
                raise AssertionError(f"{standard}: missing {expected - actual}, unexpected {actual - expected}")
            assert source.read_bytes() == original, "checker changed the source"
            assert diagnostics.exists(), "no diagnostic export produced"
            assert "ReplacementText:" not in diagnostics.read_text(), "checker offered an automatic fix"
            print(f"{standard}: {len(expected)} expected warning locations; negative cases and no-fix check passed")


if __name__ == "__main__":
    main()
