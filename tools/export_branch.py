#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Создание и проверка чистой экспортной ветки без следов HYAI (PRJ-POL-01..06).

Все правила фильтрации и запрещённые паттерны читаются из раздела `export`
файла `meta/manifest.yaml` (принцип 3D). При отсутствии конфигурации возвращает rc=2.
"""

from __future__ import annotations

import argparse
import fnmatch
import json
import os
import re
import shutil
import stat
import subprocess
import sys
import tempfile
from pathlib import Path

import yaml


def load_export_config(root: Path) -> tuple[dict, str | None]:
    manifest_path = root / "meta" / "manifest.yaml"
    if not manifest_path.exists():
        return {}, "meta/manifest.yaml не найден"
    data = yaml.safe_load(manifest_path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        return {}, "meta/manifest.yaml не читается как словарь"
    export_cfg = data.get("export")
    if not isinstance(export_cfg, dict) or not export_cfg.get("exclude_paths"):
        return {}, "meta/manifest.yaml: отсутствует или пуст раздел export"
    return export_cfg, None


def git(root: Path, *args: str, check: bool = False) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", "-C", str(root), *args],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=check,
    )


def glob_to_regex(pattern: str) -> re.Pattern[str]:
    p = pattern.replace("\\", "/").lstrip("./")
    i, out = 0, []
    while i < len(p):
        if p.startswith("**/", i):
            out.append("(?:.*/)?")
            i += 3
        elif p.startswith("**", i):
            out.append(".*")
            i += 2
        elif p[i] == "*":
            out.append("[^/]*")
            i += 1
        elif p[i] == "?":
            out.append("[^/]")
            i += 1
        else:
            out.append(re.escape(p[i]))
            i += 1
    return re.compile("^" + "".join(out) + "$")


def path_matches(rel: str, patterns: list[str]) -> bool:
    clean = rel.replace("\\", "/").lstrip("./")
    return any(glob_to_regex(pat).match(clean) or fnmatch.fnmatch(clean, pat) for pat in patterns)


def strip_submodules_text(text: str, strip_names: list[str]) -> str:
    if not strip_names:
        return text
    targets = {name.strip("/") for name in strip_names}
    lines = text.splitlines(keepends=True)
    out: list[str] = []
    skip = False
    header_re = re.compile(r'^\s*\[submodule\s+"([^"]+)"\]\s*$')
    section_re = re.compile(r"^\s*\[.+\]\s*$")
    for line in lines:
        m = header_re.match(line.strip())
        if m:
            skip = m.group(1).strip("/") in targets
            if not skip:
                out.append(line)
            continue
        if section_re.match(line.strip()):
            skip = False
        if not skip:
            out.append(line)
    return "".join(out)


def ref_exists(root: Path, ref: str) -> bool:
    return git(root, "rev-parse", "--verify", f"{ref}^{{commit}}").returncode == 0


def read_blob_bytes(root: Path, rev: str, rel: str) -> bytes | None:
    proc = subprocess.run(
        ["git", "-C", str(root), "show", f"{rev}:{rel}"],
        capture_output=True,
        check=False,
    )
    return proc.stdout if proc.returncode == 0 else None


def collect_candidate_paths(root: Path, base: str, source: str, from_worktree: bool) -> list[str]:
    paths: set[str] = set()
    diff_args = ["diff", "--name-only", base] if from_worktree else ["diff", "--name-only", base, source]
    proc = git(root, *diff_args)
    if proc.returncode == 0:
        paths.update(line.strip() for line in proc.stdout.splitlines() if line.strip())
    if from_worktree:
        untracked = git(root, "ls-files", "--others", "--exclude-standard")
        if untracked.returncode == 0:
            paths.update(line.strip() for line in untracked.stdout.splitlines() if line.strip())
    return sorted(paths)


def verify_branch(root: Path, cfg: dict, base: str, branch: str) -> list[str]:
    problems: list[str] = []
    if not ref_exists(root, base):
        return [f"базовая ревизия не найдена: {base}"]
    if not ref_exists(root, branch):
        return [f"экспортная ветка не найдена: {branch}"]

    exclude_paths = [str(p) for p in (cfg.get("exclude_paths") or [])]
    strip_subs = [str(s) for s in (cfg.get("strip_submodules") or [])]
    content_regexes = [re.compile(str(p)) for p in (cfg.get("forbidden_content_patterns") or [])]
    commit_regexes = [re.compile(str(p)) for p in (cfg.get("forbidden_commit_patterns") or [])]

    commits = git(root, "rev-list", f"{base}..{branch}")
    commit_shas = [c.strip() for c in commits.stdout.splitlines() if c.strip()]
    if not commit_shas:
        problems.append(f"{branch}: нет коммитов поверх {base}")

    for sha in commit_shas:
        msg = git(root, "show", "-s", "--format=%B", sha).stdout
        for rx in commit_regexes:
            m = rx.search(msg)
            if m:
                problems.append(
                    f"коммит {sha[:8]}: запрещённое упоминание в сообщении коммита: {m.group(0)!r} (PRJ-POL-04)"
                )

    name_status = git(root, "diff", "--name-status", f"{base}...{branch}")
    for line in name_status.stdout.splitlines():
        if not line.strip():
            continue
        parts = line.split("\t")
        changed_rels = parts[1:]
        for rel in changed_rels:
            if path_matches(rel, exclude_paths):
                problems.append(f"{rel}: инфраструктурный путь HYAI попал в экспортную ветку (PRJ-POL-02)")
            if rel == ".gitmodules" and strip_subs:
                blob = read_blob_bytes(root, branch, ".gitmodules")
                text = (blob or b"").decode("utf-8", errors="replace")
                for sub in strip_subs:
                    if f'[submodule "{sub}"]' in text:
                        problems.append(f".gitmodules: осталась секция подмодуля {sub!r} (PRJ-POL-02)")

    patch = git(root, "diff", "-U0", f"{base}...{branch}")
    current_file = "<unknown>"
    for raw_line in patch.stdout.splitlines():
        if raw_line.startswith("+++ b/"):
            current_file = raw_line[6:]
            continue
        if not raw_line.startswith("+") or raw_line.startswith("+++"):
            continue
        added = raw_line[1:]
        for rx in content_regexes:
            m = rx.search(added)
            if m:
                problems.append(
                    f"{current_file}: запрещённый маркер HYAI в добавленной строке: {m.group(0)!r} (PRJ-POL-03)"
                )
    return problems


def create_branch(
    root: Path,
    cfg: dict,
    base: str,
    source: str,
    branch: str,
    message: str,
    only_paths: list[str] | None,
    from_worktree: bool,
    force: bool,
) -> tuple[int, list[str], list[str]]:
    if not ref_exists(root, base):
        return 2, [f"базовая ревизия не найдена: {base}"], []
    if not from_worktree and not ref_exists(root, source):
        return 2, [f"исходная ревизия не найдена: {source}"], []
    if ref_exists(root, f"refs/heads/{branch}") and not force:
        return 2, [f"ветка {branch!r} уже существует (используйте --force для пересоздания)"], []

    exclude_paths = [str(p) for p in (cfg.get("exclude_paths") or [])]
    strip_subs = [str(s) for s in (cfg.get("strip_submodules") or [])]
    candidates = collect_candidate_paths(root, base, source, from_worktree)

    selected: list[str] = []
    for rel in candidates:
        if path_matches(rel, exclude_paths):
            continue
        if only_paths and not path_matches(rel, only_paths):
            continue
        selected.append(rel)

    tmpdir = tempfile.mkdtemp(prefix="opents-clean-export-")
    wt = Path(tmpdir) / "wt"
    exported_files: list[str] = []
    try:
        add_wt = git(root, "worktree", "add", "--detach", str(wt), base)
        if add_wt.returncode != 0:
            return 2, [f"не удалось создать временный worktree: {add_wt.stderr.strip()}"], []

        for rel in selected:
            dst = wt / rel
            if rel == ".gitmodules":
                raw = (root / rel).read_bytes() if from_worktree and (root / rel).exists() else read_blob_bytes(root, source, rel)
                if raw is None:
                    if dst.exists():
                        dst.unlink()
                else:
                    cleaned = strip_submodules_text(raw.decode("utf-8", errors="replace"), strip_subs)
                    base_raw = read_blob_bytes(root, base, ".gitmodules")
                    base_text = (base_raw or b"").decode("utf-8", errors="replace")
                    if cleaned == base_text:
                        continue
                    dst.parent.mkdir(parents=True, exist_ok=True)
                    dst.write_text(cleaned, encoding="utf-8")
                continue

            if from_worktree:
                src_file = root / rel
                if src_file.is_file() or src_file.is_symlink():
                    dst.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(src_file, dst)
                elif dst.exists():
                    dst.unlink()
            else:
                ls_tree = git(root, "ls-tree", source, "--", rel)
                if not ls_tree.stdout.strip():
                    if dst.exists():
                        dst.unlink()
                else:
                    mode_str = ls_tree.stdout.split()[0]
                    if mode_str == "160000":
                        # Продуктовый подмодуль (например thirdparty/*): переносим gitlink.
                        sub_sha = ls_tree.stdout.split()[2]
                        git(wt, "update-index", "--add", "--cacheinfo", f"160000,{sub_sha},{rel}")
                        continue
                    blob = read_blob_bytes(root, source, rel)
                    if blob is not None:
                        dst.parent.mkdir(parents=True, exist_ok=True)
                        dst.write_bytes(blob)
                        if mode_str == "100755":
                            dst.chmod(dst.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

        git(wt, "add", "-A")
        staged = git(wt, "diff", "--cached", "--name-only")
        exported_files = [line.strip() for line in staged.stdout.splitlines() if line.strip()]
        if not exported_files:
            return 1, ["нет продуктовых изменений для экспорта после исключения артефактов HYAI"], []

        commit_proc = git(wt, "commit", "-m", message)
        if commit_proc.returncode != 0:
            return 1, [f"git commit не удался: {commit_proc.stderr.strip() or commit_proc.stdout.strip()}"], []

        new_sha = git(wt, "rev-parse", "HEAD").stdout.strip()
        git(root, "branch", "-f", branch, new_sha)
    finally:
        git(root, "worktree", "remove", "--force", str(wt))
        shutil.rmtree(tmpdir, ignore_errors=True)

    problems = verify_branch(root, cfg, base, branch)
    if problems:
        git(root, "branch", "-D", branch)
        return 1, problems, exported_files
    return 0, [], exported_files


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Чистый экспорт задачи в ветку без HYAI")
    parser.add_argument("--root", default=str(Path.cwd().resolve()))
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_create = sub.add_parser("create", help="создать и проверить чистую экспортную ветку")
    p_create.add_argument("--branch", required=True, help="имя новой экспортной ветки")
    p_create.add_argument("--base", default=None, help="чистая база без HYAI (по умолчанию export.default_base)")
    p_create.add_argument("--source", default="HEAD", help="исходный коммит с решением задачи")
    p_create.add_argument("--from-worktree", action="store_true", help="брать продуктовые правки из рабочего дерева")
    p_create.add_argument("--message", required=True, help="сообщение продуктового коммита")
    p_create.add_argument("--paths", nargs="*", default=None, help="ограничить экспорт указанными путями/глобами")
    p_create.add_argument("--force", action="store_true", help="перезаписать существующую экспортную ветку")
    p_create.add_argument("--json", action="store_true")

    p_verify = sub.add_parser("verify", help="проверить экспортную ветку на отсутствие следов HYAI")
    p_verify.add_argument("--branch", required=True, help="имя проверяемой экспортной ветки")
    p_verify.add_argument("--base", default=None, help="чистая база без HYAI (по умолчанию export.default_base)")
    p_verify.add_argument("--json", action="store_true")

    args = parser.parse_args(argv)
    root = Path(args.root).resolve()
    cfg, err = load_export_config(root)
    if err:
        print(err, file=sys.stderr)
        return 2

    base = args.base or str(cfg.get("default_base") or "origin/main")

    if args.cmd == "verify":
        problems = verify_branch(root, cfg, base, args.branch)
        if args.json:
            print(json.dumps({"branch": args.branch, "base": base, "problems": problems, "count": len(problems)}, ensure_ascii=False, indent=2))
        else:
            for item in problems:
                print(f"  {item}")
            print(f"\nчистый экспорт ({args.branch} относительно {base}): проблем: {len(problems)}")
        return 1 if problems else 0

    rc, problems, exported = create_branch(
        root=root,
        cfg=cfg,
        base=base,
        source=args.source,
        branch=args.branch,
        message=args.message,
        only_paths=args.paths,
        from_worktree=args.from_worktree,
        force=args.force,
    )
    if args.json:
        print(json.dumps({"branch": args.branch, "base": base, "exported": exported, "problems": problems, "rc": rc}, ensure_ascii=False, indent=2))
    else:
        for item in problems:
            print(f"  {item}", file=sys.stderr if rc == 2 else sys.stdout)
        if rc == 0:
            print(f"Создана и проверена чистая ветка {args.branch!r} (база: {base}, файлов: {len(exported)})")
            for rel in exported:
                print(f"  + {rel}")
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
