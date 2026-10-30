---
id: root.claude
title: "CLAUDE.md — адаптер для Claude Code в OpenTS"
type: meta
status: approved
authority: none
visibility: internal
version: "1.0"
updated: "2026-10-30"
---

# CLAUDE.md

Единственная точка входа — [`AGENTS.md`](AGENTS.md). Он собирается из `HYAI/HYAI/core/kernel/agents.md` и фрагментов активных доменов (`meta/domains/software/agents.md`, `HYAI/HYAI/core/domains/docs/agents.md`), проверка расхождения — `python3 HYAI/HYAI/tools/assemble_agents.py --check`.

Порядок чтения для Claude Code:

1. [`AGENTS.md`](AGENTS.md) — проверка среды исполнения (`HYAI-INT-30`), выбор домена и профиля, запреты, ворота и протокол чистого экспорта (`PRJ-POL-01..06`).
2. [`HYAI/HYAI/core/kernel/principles.md`](HYAI/HYAI/core/kernel/principles.md) — `3D`, `HaRD`, `INRA`, `HELPER`.
3. [`meta/project.md`](meta/project.md) — паспорт репозитория `OpenTS`, активные домены и карта путей.
4. [`meta/policies/export.md`](meta/policies/export.md) — обязательное правило создания чистой экспортной ветки без следов `HYAI`.
5. [`HYAI/HYAI/core/domains/`](HYAI/HYAI/core/domains/) — `policy.md`, `access.md`, `profiles/<profile>/README.md` выбранного домена.

Если правило требует уточнения или фиксируется новое решение владельца — оно оформляется в [`meta/decisions/`](meta/decisions/README.md).
