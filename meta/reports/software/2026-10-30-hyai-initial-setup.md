---
id: report.software.2026-10-30-hyai-initial-setup
title: "Первоначальная настройка OpenTS под каркас HYAI и протокол чистого экспорта"
type: report
report_kind: task
domain: software
profile: software/architect
status: review
authority: none
visibility: internal
date: "2026-10-30"
workflow: task
decisions:
  - "P-0001"
cross_scope:
  - path: "AGENTS.md"
    why: "сгенерирован общий вход агента HYAI взамен унаследованных рекомендаций OpenTS по решению P-0001"
  - path: "CLAUDE.md"
    why: "заменён адаптер Claude Code на вход через контракты HYAI (P-0001)"
  - path: ".claude/settings.json"
    why: "отключён унаследованный хук style-rules.py по решению P-0001"
  - path: ".codex/hooks.json"
    why: "отключён унаследованный хук style-rules.py по решению P-0001"
  - path: "meta/"
    why: "развёрнуты и адаптированы проектные метаданные, политики, схемы и реестры HYAI для OpenTS"
---

# Первоначальная настройка OpenTS под каркас HYAI и протокол чистого экспорта

## Задача

Перевести рабочую среду репозитория `OpenTS` на контракты подключённого в корень подмодуля `HYAI` без оглядки на унаследованные агентские рекомендации `OpenTS` `[А]`, а также ввести проверяемый механизм чистого экспорта результата задачи в отдельную Git-ветку без каких-либо упоминаний и артефактов `HYAI` `[А]`.

## Что сделано

- Развёрнут проектный каталог [`meta/`](../../README.md) на базе шаблона `HYAI/HYAI/meta/` (удалены внутренние отчёты и решения самого репозитория `HYAI`, настроены индексы и схемы под `OpenTS`).
- Сконфигурирован [`meta/manifest.yaml`](../../manifest.yaml): активные пары `software/code` (`code`, `cmake`, `tests`, `ui`) и `docs/tech-writer` (`docs`, `meta/specs`, `meta/diagrams`), `framework.root: "HYAI/"`, `framework.prefix: "HYAI/HYAI/"`, а также декларативный раздел `export` (`3D`).
- Создан оверлей домена [`meta/domains/software/manifest.yaml`](../../domains/software/manifest.yaml) и фрагмент `meta/domains/software/agents.md`, благодаря чему `assemble_agents.py` включает протокол чистого экспорта прямо в генерируемый [`AGENTS.md`](../../../AGENTS.md) без модификации подмодуля `HYAI/`.
- Зафиксированы решение владельца [`P-0001`](../../decisions/project/P-0001-hyai-contracts-and-clean-export.md) и проектная политика [`meta/policies/export.md`](../../policies/export.md) (`PRJ-POL-01..06`).
- Реализованы проектные движки в [`tools/`](../../../tools/README.md):
  - [`tools/export_branch.py`](../../../tools/export_branch.py) — создание (`create`) и верификация (`verify`) чистой ветки от `origin/main` через изолированный временный `git worktree` с вырезанием секции `HYAI` из `.gitmodules`, фильтрацией путей `HYAI`/`meta`/`tools`/`AGENTS.md`/`CLAUDE.md` и проверкой диффа и сообщений коммитов на отсутствие любых маркеров `HYAI`;
  - [`tools/check_whitespace.py`](../../../tools/check_whitespace.py) и [`tools/run_unit_tests.py`](../../../tools/run_unit_tests.py) — интеграция проверок пробелов и модульных тестов `tools/tests/test_export_branch.py` в `run_checks.py`.
- Сгенерирован корневой [`AGENTS.md`](../../../AGENTS.md), обновлён [`CLAUDE.md`](../../../CLAUDE.md), отключены унаследованные хуки в `.claude/settings.json` и `.codex/hooks.json`.

## Проверка

| Проверка | Команда | Результат |
| --- | --- | --- |
| Сборка `AGENTS.md` | `python3 HYAI/HYAI/tools/assemble_agents.py --check` | `rc=0` |
| Полный конвейер `HYAI` (15 шагов, включая тесты чистого экспорта) | `python3 HYAI/HYAI/tools/run_checks.py` | `rc=0`, 0 падений `[В]` |
| Чистота подмодуля `HYAI/` | `git -C HYAI status --short` | `rc=0`, пусто `[В]` |

## Открытые вопросы

Открытых вопросов нет. Статус отчёта выставлен в `review` (`HYAI-PERM-N05` — перевод в `approved` выполняет владелец).
