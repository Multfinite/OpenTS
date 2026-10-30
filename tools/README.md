---
id: tools.readme
title: "Инструменты проекта OpenTS для конвейера HYAI"
type: tool
status: approved
authority: none
visibility: internal
version: "1.0"
updated: "2026-10-30"
---

# `tools/` — проектные движки `OpenTS`

Собственные проверочные и сервисные скрипты проекта, дополняющие встроенные движки `HYAI/HYAI/tools/` (`[И] meta/manifest.yaml`). Все параметры читаются из `meta/manifest.yaml` (`3D`).

| Путь | Назначение |
| --- | --- |
| [`export_branch.py`](export_branch.py) | Создание и верификация чистой экспортной ветки без упоминаний и артефактов `HYAI` (`PRJ-POL-01..06`, `P-0001`) |
| [`check_whitespace.py`](check_whitespace.py) | Запуск `HYAI/HYAI/tools/whitespace.py` по файлам репозитория с учётом нетронутых апстрим-исключений из `export.legacy_whitespace_exempt` |
| [`run_unit_tests.py`](run_unit_tests.py) | Запуск unit-тестов проекта (`tools/tests`) в рамках `validation.project` (флаг `--include-hyai` подключает тесты подмодуля `HYAI`) |
| [`tests/`](tests/README.md) | Модульные тесты проектных инструментов |
