---
id: tools.tests.readme
title: "Модульные тесты проектных инструментов OpenTS"
type: tool
status: approved
authority: none
visibility: internal
version: "1.0"
updated: "2026-10-30"
---

# `tools/tests/`

Модульные тесты собственных инструментов `OpenTS` (`unittest`). Запускаются в составе `python3 tools/run_unit_tests.py`.

| Файл | Что проверяет |
| --- | --- |
| [`test_export_branch.py`](test_export_branch.py) | Чистый экспорт ветки без `HYAI`, очистка секции `HYAI` в `.gitmodules`, блокировка утечек `HYAI` в коде и сообщениях коммитов (`PRJ-POL-01..06`) |
