#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Проектная обёртка whitespace.py для OpenTS.

Проверяет все файлы репозитория встроенным движком `HYAI/HYAI/tools/whitespace.py`,
пропуская подмодуль `HYAI/` и нетронутые апстрим-файлы из `export.legacy_whitespace_exempt`
до тех пор, пока они не изменены относительно `export.default_base`.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

import yaml

SKIP_PARTS = {".git", "__pycache__", "node_modules", ".venv", "venv", "dist", "build"}


def modified_since_base(root: Path, base: str) -> set[str]:
    changed: set[str] = set()
    for extra in ([base], []):
        proc = subprocess.run(
            ["git", "-C", str(root), "diff", "--name-only", *extra],
            capture_output=True,
            text=True,
            encoding="utf-8",
            check=False,
        )
        if proc.returncode == 0:
            changed.update(line.strip() for line in proc.stdout.splitlines() if line.strip())
    return changed


def main() -> int:
    parser = argparse.ArgumentParser(description="Проверка пробелов проекта OpenTS")
    parser.add_argument("--root", default=str(Path.cwd().resolve()))
    args = parser.parse_args()

    root = Path(args.root).resolve()
    manifest_path = root / "meta" / "manifest.yaml"
    if not manifest_path.exists():
        print("meta/manifest.yaml не найден", file=sys.stderr)
        return 2

    man = yaml.safe_load(manifest_path.read_text(encoding="utf-8")) or {}
    ws_rel = ((man.get("validation") or {}).get("data") or {}).get("whitespace") or "meta/lint/whitespace.json"
    ws_cfg = json.loads((root / ws_rel).read_text(encoding="utf-8")) if (root / ws_rel).exists() else {}
    exts = {e.lower() for e in (ws_cfg.get("extensions") or [".md", ".py", ".json", ".yaml", ".yml"])}

    export_cfg = man.get("export") or {}
    base = str(export_cfg.get("default_base") or "origin/main")
    exempt = {str(p).replace("\\", "/").lstrip("./") for p in (export_cfg.get("legacy_whitespace_exempt") or [])}
    touched = modified_since_base(root, base)

    fw_prefix = str(((man.get("framework") or {}).get("prefix")) or "HYAI/HYAI/").strip("/")
    sub_root = str(((man.get("framework") or {}).get("root")) or "HYAI/").strip("/")
    ws_engine = root / fw_prefix / "tools" / "whitespace.py"
    if not ws_engine.exists():
        print(f"движок не найден: {ws_engine}", file=sys.stderr)
        return 2

    targets: list[bytes] = []
    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.suffix.lower() not in exts:
            continue
        rel = path.relative_to(root).as_posix()
        parts = Path(rel).parts
        if any(p in SKIP_PARTS for p in parts):
            continue
        if rel == sub_root or rel.startswith(sub_root + "/"):
            continue
        if rel in exempt and rel not in touched:
            continue
        targets.append(str(path).encode("utf-8"))

    proc = subprocess.run(
        [sys.executable, str(ws_engine), "--root", str(root), "--stdin-paths"],
        input=b"\0".join(targets),
        check=False,
    )
    return proc.returncode


if __name__ == "__main__":
    raise SystemExit(main())
