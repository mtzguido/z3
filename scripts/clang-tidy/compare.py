#!/usr/bin/env python3
# Copyright (c) 2026 Microsoft Corporation
# SPDX-License-Identifier: MIT
"""Compare warning counts from complete scans of a base and head checkout."""
import argparse
from collections import Counter
from difflib import SequenceMatcher
import json
import pathlib
import re

CHECK = "z3-ast-argument-order"


def read_summary(path):
    summary = json.loads(path.read_text())
    if summary["check"] != CHECK:
        raise ValueError("unexpected clang-tidy check")
    units = summary["translation_units"]
    if not units or any(unit["returncode"] != 0 for unit in units):
        raise ValueError(f"{path}: scan is incomplete; refusing to compare warning counts")
    # Deduplicate header diagnostics shared by several translation units.
    warnings = {(w["file"], w["line"], w["column"], w["message"])
                for w in summary["warnings"]}
    for file, *_ in warnings:
        p = pathlib.PurePosixPath(file)
        if p.is_absolute() or ".." in p.parts or any(ord(c) < 32 for c in file):
            raise ValueError("expected checkout-relative diagnostic paths; use --source-root")
    return summary["clang_tidy_version"], warnings


def warning_diff(base, head, base_source=None, head_source=None):
    """Match diagnostics on unchanged source lines, including lines that moved."""
    line_maps = {}
    if base_source and head_source:
        for file in {w[0] for w in base}:
            before, after = base_source / file, head_source / file
            if not before.is_file() or not after.is_file():
                continue
            old, new = before.read_bytes().splitlines(), after.read_bytes().splitlines()
            if old == new:
                continue
            line_maps[file] = {a + i + 1: b + i + 1
                              for a, b, size in SequenceMatcher(None, old, new, autojunk=False).get_matching_blocks()
                              for i in range(size)}
    added, removed = set(head), []
    for file, line, column, message in sorted(base):
        new_line = line_maps[file].get(line) if file in line_maps else line
        match = (file, new_line, column, message)
        if match in added:
            added.remove(match)
        else:
            removed.append((file, line, column, message))
    def diagnostic(w):
        return dict(zip(("file", "line", "column", "message"), w))
    return {"removed": [diagnostic(w) for w in removed],
            "added": [diagnostic(w) for w in sorted(added)]}


def compare(base_path, head_path, base_sha, head_sha, tested_sha=None, base_source=None, head_source=None):
    tested_sha = tested_sha or head_sha
    head_version, head_warnings = read_summary(head_path)
    head = Counter(w[0] for w in head_warnings)
    if not base_path:
        return {"schema_version": 1, "head_sha": head_sha, "tested_sha": tested_sha, "base_sha": None,
                "head_count": sum(head.values()), "base_count": None, "files": []}
    base_version, base_warnings = read_summary(base_path)
    base = Counter(w[0] for w in base_warnings)
    if base_version != head_version:
        raise ValueError("base and head scans used different clang-tidy versions")
    files = [{"path": path, "base": base[path], "head": head[path]}
             for path in sorted(base.keys() | head.keys()) if base[path] != head[path]]
    return {"schema_version": 1, "base_sha": base_sha, "head_sha": head_sha, "tested_sha": tested_sha,
            "base_count": sum(base.values()), "head_count": sum(head.values()), "files": files,
            "warning_diff": warning_diff(base_warnings, head_warnings, base_source, head_source)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", type=pathlib.Path)
    parser.add_argument("--head", required=True, type=pathlib.Path)
    parser.add_argument("--base-sha")
    parser.add_argument("--head-sha", required=True)
    parser.add_argument("--tested-sha", help="the tested merge commit, if different from the PR head")
    parser.add_argument("--base-source", type=pathlib.Path, help="base checkout, to match shifted source lines")
    parser.add_argument("--head-source", type=pathlib.Path, help="head checkout, to match shifted source lines")
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()
    if bool(args.base_source) != bool(args.head_source):
        parser.error("--base-source and --head-source must be supplied together")
    if args.base_source and (not args.base_source.is_dir() or not args.head_source.is_dir()):
        parser.error("source checkouts must be existing directories")
    for sha in [args.head_sha] + ([args.base_sha] if args.base else []) + ([args.tested_sha] if args.tested_sha else []):
        if not sha or not re.fullmatch(r"[0-9a-f]{40}", sha):
            parser.error("expected full commit SHAs")
    report = compare(args.base, args.head, args.base_sha, args.head_sha, args.tested_sha,
                     args.base_source, args.head_source)
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "comparison.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"AST argument-order warnings: {report['head_count']}")
    if report["base_count"] is not None:
        print(f"Base: {report['base_count']}; change: {report['head_count'] - report['base_count']:+d}")


if __name__ == "__main__":
    main()
