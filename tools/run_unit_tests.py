#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Запуск модульных тестов проектных инструментов OpenTS (и опционально каркаса HYAI)."""

from __future__ import annotations

import argparse
import sys
import unittest
from pathlib import Path

import yaml


def main() -> int:
    parser = argparse.ArgumentParser(description="Unit-тесты проектных инструментов OpenTS")
    parser.add_argument("--root", default=str(Path.cwd().resolve()))
    parser.add_argument("--include-hyai", action="store_true", help="включить тесты самого подмодуля HYAI")
    args = parser.parse_args()

    root = Path(args.root).resolve()
    manifest_path = root / "meta" / "manifest.yaml"
    if not manifest_path.exists():
        print("meta/manifest.yaml не найден", file=sys.stderr)
        return 2

    man = yaml.safe_load(manifest_path.read_text(encoding="utf-8")) or {}
    fw_prefix = str(((man.get("framework") or {}).get("prefix")) or "HYAI/HYAI/").strip("/")
    paths_cfg = man.get("paths") or {}
    project_tests = root / str(paths_cfg.get("tests") or "tools/tests/")
    hyai_tests = root / fw_prefix / "tools" / "tests"

    dirs = [project_tests]
    if args.include_hyai:
        dirs.insert(0, hyai_tests)

    suite = unittest.TestSuite()
    loader = unittest.TestLoader()
    for test_dir in dirs:
        if test_dir.exists():
            suite.addTests(loader.discover(start_dir=str(test_dir)))

    runner = unittest.TextTestRunner(verbosity=1)
    result = runner.run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
