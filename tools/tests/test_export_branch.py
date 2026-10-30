# SPDX-License-Identifier: GPL-3.0-only
"""Модульные тесты движка чистого экспорта tools/export_branch.py (PRJ-POL-01..06)."""

from __future__ import annotations

import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parents[1]
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import export_branch as eb


def run_git(repo: Path, *args: str) -> None:
    subprocess.run(
        ["git", "-C", str(repo), *args],
        check=True,
        capture_output=True,
        text=True,
    )


class ExportBranchTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.repo = Path(self.tmp.name) / "repo"
        self.repo.mkdir()
        run_git(self.repo, "init", "-b", "main")
        run_git(self.repo, "config", "user.name", "Test User")
        run_git(self.repo, "config", "user.email", "test@example.com")

        (self.repo / ".gitmodules").write_text(
            '[submodule "thirdparty/SDL"]\n\tpath = thirdparty/SDL\n\turl = https://github.com/libsdl-org/SDL.git\n',
            encoding="utf-8",
        )
        (self.repo / "AGENTS.md").write_text("# Upstream AGENTS\n", encoding="utf-8")
        (self.repo / "CLAUDE.md").write_text("# Upstream CLAUDE\n", encoding="utf-8")
        (self.repo / "code").mkdir()
        (self.repo / "code" / "game.cpp").write_text("int main() { return 0; }\n", encoding="utf-8")
        run_git(self.repo, "add", "-A")
        run_git(self.repo, "commit", "-m", "Initial upstream commit")
        run_git(self.repo, "tag", "upstream-base")

        # Подключение HYAI и проектных данных поверх апстрима.
        (self.repo / ".gitmodules").write_text(
            '[submodule "thirdparty/SDL"]\n\tpath = thirdparty/SDL\n\turl = https://github.com/libsdl-org/SDL.git\n'
            '[submodule "HYAI"]\n\tpath = HYAI\n\turl = https://github.com/Multfinite/HYAI.git\n',
            encoding="utf-8",
        )
        (self.repo / "AGENTS.md").write_text("<!-- generated: true -->\n# HYAI\n", encoding="utf-8")
        (self.repo / "CLAUDE.md").write_text("# HYAI CLAUDE\n", encoding="utf-8")
        (self.repo / "meta").mkdir()
        (self.repo / "meta" / "manifest.yaml").write_text(
            textwrap.dedent(
                """\
                apiVersion: hyai/v1
                kind: HyaiManifest
                export:
                  default_base: "upstream-base"
                  exclude_paths:
                    - "HYAI"
                    - "HYAI/**"
                    - "meta/**"
                    - "tools/**"
                    - "AGENTS.md"
                    - "CLAUDE.md"
                    - ".claude/settings.json"
                    - ".codex/hooks.json"
                  strip_submodules:
                    - "HYAI"
                  forbidden_content_patterns:
                    - "\\\\bHYAI\\\\b"
                    - "\\\\b(?:HYAI|PRJ|SW|DOC)-POL-\\\\d{2}\\\\b"
                    - "apiVersion:\\\\s*hyai/"
                    - "(?<!\\\\w)\\\\[(?:А|И|В|Г|\\\\?)\\\\](?!\\\\()"
                  forbidden_commit_patterns:
                    - "\\\\bHYAI\\\\b"
                    - "\\\\bP-\\\\d{4}\\\\b"
                    - "meta/reports/"
                """
            ),
            encoding="utf-8",
        )
        (self.repo / "tools").mkdir()
        (self.repo / "tools" / "dummy.py").write_text("x = 1\n", encoding="utf-8")
        run_git(self.repo, "add", "-A")
        run_git(self.repo, "commit", "-m", "Configure HYAI")

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def test_clean_export_strips_all_hyai_artifacts(self) -> None:
        (self.repo / "code" / "game.cpp").write_text("int main() { return 42; }\n", encoding="utf-8")
        (self.repo / "code" / "helper.h").write_text("#pragma once\nint answer();\n", encoding="utf-8")
        (self.repo / "meta" / "report.md").write_text("Отчёт HYAI [И]\n", encoding="utf-8")
        run_git(self.repo, "add", "-A")
        run_git(self.repo, "commit", "-m", "Implement answer helper")

        rc = eb.main([
            "--root", str(self.repo),
            "create",
            "--branch", "export/answer-helper",
            "--message", "Add answer helper and return 42",
        ])
        self.assertEqual(rc, 0)

        diff_names = subprocess.run(
            ["git", "-C", str(self.repo), "diff", "--name-only", "upstream-base...export/answer-helper"],
            check=True,
            capture_output=True,
            text=True,
        ).stdout.splitlines()
        self.assertEqual(sorted(diff_names), ["code/game.cpp", "code/helper.h"])

        rc_verify = eb.main([
            "--root", str(self.repo),
            "verify",
            "--branch", "export/answer-helper",
        ])
        self.assertEqual(rc_verify, 0)

    def test_export_rejects_hyai_mention_in_product_file(self) -> None:
        (self.repo / "code" / "game.cpp").write_text(
            "// Verified per HYAI rule PRJ-POL-01\nint main() { return 1; }\n",
            encoding="utf-8",
        )
        run_git(self.repo, "add", "-A")
        run_git(self.repo, "commit", "-m", "Update game")

        rc = eb.main([
            "--root", str(self.repo),
            "create",
            "--branch", "export/leaky-code",
            "--message", "Update game return code",
        ])
        self.assertEqual(rc, 1)

    def test_export_rejects_hyai_mention_in_commit_message(self) -> None:
        (self.repo / "code" / "game.cpp").write_text("int main() { return 7; }\n", encoding="utf-8")
        run_git(self.repo, "add", "-A")
        run_git(self.repo, "commit", "-m", "Update game")

        rc = eb.main([
            "--root", str(self.repo),
            "create",
            "--branch", "export/leaky-msg",
            "--message", "Export result per P-0001 and HYAI",
        ])
        self.assertEqual(rc, 1)

    def test_verify_detects_hyai_path_on_manual_branch(self) -> None:
        run_git(self.repo, "branch", "export/dirty", "HEAD")
        rc = eb.main([
            "--root", str(self.repo),
            "verify",
            "--branch", "export/dirty",
        ])
        self.assertEqual(rc, 1)


if __name__ == "__main__":
    unittest.main()
