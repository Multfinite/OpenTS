---
id: meta.policy.export
title: "Политика чистого экспорта результата задачи без HYAI"
type: policy
status: approved
authority: invariant
visibility: internal
version: "1.0"
updated: "2026-10-30"
---

# Чистый экспорт результата задачи (`PRJ-POL`)

`HYAI` используется как внешняя среда управления работой ИИ-агента и не должен попадать в главный репозиторий `OpenTS` `[А]`. При запросе пользователя на экспорт результата задачи действуют правила `PRJ-POL-01..06` (решение [`P-0001`](../decisions/project/P-0001-hyai-contracts-and-clean-export.md)).

## Правила

| ID | Норма | Чем проверяется |
| --- | --- | --- |
| `PRJ-POL-01` | По команде пользователя экспортировать результат задачи («надо экспортировать результат» или смысловой эквивалент) агент создаёт новую Git-ветку от чистой базовой ревизии без `HYAI` (`export.default_base` в `meta/manifest.yaml`, по умолчанию `origin/main`), не разрушая рабочую HYAI-ветку `[А]`. | `tools/export_branch.py` |
| `PRJ-POL-02` | В экспортной ветке запрещены любые инфраструктурные пути `HYAI` из `export.exclude_paths` (`HYAI/`, `meta/`, `tools/`, HYAI-версии `AGENTS.md`, `CLAUDE.md`, `.claude/settings.json`, `.codex/hooks.json`) и секция `[submodule "HYAI"]` в `.gitmodules`; эти файлы остаются ровно в состоянии базовой ветки `[А]`. | `python3 tools/export_branch.py verify` |
| `PRJ-POL-03` | В изменённых и новых продуктовых файлах экспортной ветки запрещены упоминания `HYAI`, идентификаторов правил/решений/вопросов (`HYAI-*`, `PRJ-POL-*`, `SW-POL-*`, `DOC-POL-*`, `KIT-POL-*`, `NR-POL-*`, `PIC-POL-*`, `VID-POL-*`, `P-NNNN`, `SW-NNNN`, `DOC-NNNN`, `Q-*`), маркеров провенанса (`[А]`, `[И]`, `[В]`, `[Г]`, `[?]`) и `apiVersion: hyai/` `[А]`. | `python3 tools/export_branch.py verify` |
| `PRJ-POL-04` | Сообщения коммитов экспортной ветки (`<base>..<export-branch>`) описывают только продуктовый результат и не содержат упоминаний `HYAI`, путей `meta/reports/` или `meta/decisions/`, номеров решений/правил каркаса и AI-атрибуции `[А]`. | `python3 tools/export_branch.py verify` |
| `PRJ-POL-05` | Экспорт считается завершённым только после успешного прогона `python3 tools/export_branch.py verify --branch <export-branch> --base <base-ref>` с кодом возврата `rc=0`; при `rc!=0` ветка не сдаётся и не пушится до устранения утечки `[А]`. | код возврата `rc=0` + unit-тесты `tools/tests/test_export_branch.py` |
| `PRJ-POL-06` | В дифф `<base-ref>...<export-branch>` включаются только целевые файлы решения задачи; если в рабочей ветке присутствуют посторонние продуктовые правки, список экспортируемых путей ограничивается флагом `--paths` `[А]`. | `git diff --stat <base-ref>...<export-branch>` |

## Порядок выполнения экспорта

1. Завершить задачу в рабочей HYAI-ветке и убедиться, что `python3 HYAI/HYAI/tools/run_checks.py` проходит с `rc=0`.
2. Выполнить создание чистой ветки:
   ```bash
   python3 tools/export_branch.py create \
     --branch <export-branch> \
     --base origin/main \
     --message "<краткое описание результата на языке целевого репозитория>"
   ```
   При необходимости явно перечислить продуктовые пути через `--paths <path1> <path2>`.
3. Проверить дифф и верификатор:
   ```bash
   python3 tools/export_branch.py verify --branch <export-branch> --base origin/main
   git diff --stat origin/main...<export-branch>
   ```
4. Предъявить пользователю имя ветки, хеш коммита, `git diff --stat` и результат `verify` (`rc=0`). Push выполняется по явному запросу пользователя (`HYAI-PERM-G10`, `HYAI-PERM-V03`).
