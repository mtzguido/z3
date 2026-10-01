#!/usr/bin/env python3
# Copyright (c) 2026 Microsoft Corporation
# SPDX-License-Identifier: MIT
"""Compare warning counts from complete scans of a base and head checkout."""
import argparse
from collections import Counter
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
    return summary["clang_tidy_version"], Counter(w[0] for w in warnings)


def compare(base_path, head_path, base_sha, head_sha, tested_sha=None):
    tested_sha = tested_sha or head_sha
    head_version, head = read_summary(head_path)
    if not base_path:
        return {"schema_version": 1, "head_sha": head_sha, "tested_sha": tested_sha, "base_sha": None,
                "head_count": sum(head.values()), "base_count": None, "files": []}
    base_version, base = read_summary(base_path)
    if base_version != head_version:
        raise ValueError("base and head scans used different clang-tidy versions")
    files = [{"path": path, "base": base[path], "head": head[path]}
             for path in sorted(base.keys() | head.keys()) if base[path] != head[path]]
    return {"schema_version": 1, "base_sha": base_sha, "head_sha": head_sha, "tested_sha": tested_sha,
            "base_count": sum(base.values()), "head_count": sum(head.values()), "files": files}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", type=pathlib.Path)
    parser.add_argument("--head", required=True, type=pathlib.Path)
    parser.add_argument("--base-sha")
    parser.add_argument("--head-sha", required=True)
    parser.add_argument("--tested-sha", help="the tested merge commit, if different from the PR head")
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()
    for sha in [args.head_sha] + ([args.base_sha] if args.base else []) + ([args.tested_sha] if args.tested_sha else []):
        if not sha or not re.fullmatch(r"[0-9a-f]{40}", sha):
            parser.error("expected full commit SHAs")
    report = compare(args.base, args.head, args.base_sha, args.head_sha, args.tested_sha)
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "comparison.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"AST argument-order warnings: {report['head_count']}")
    if report["base_count"] is not None:
        print(f"Base: {report['base_count']}; change: {report['head_count'] - report['base_count']:+d}")


if __name__ == "__main__":
    main()
