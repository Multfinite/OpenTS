---
id: hyai.reports.readme
title: "meta/reports/ — отчёты по областям в OpenTS"
type: meta
status: approved
authority: contract
visibility: internal
version: "1.1"
updated: "2026-10-30"
---

# Отчёты

Отчёт — единственный след работы, который читает следующая сессия. Чат не след: он не переживает контекст.

## Структура

| Каталог | Что | Область |
| --- | --- | --- |
| [`kit/`](kit/) | локальные изменения оверлеев каркаса | `kit` |
| [`software/`](software/) | работа с кодом, сборкой и инструментами | `software` |
| [`docs/`](docs/) | работа с документами и схемами | `docs` |
| [`narrative/`](narrative/) | зарезервировано каркасом | `narrative` |
| [`picture/`](picture/) | зарезервировано каркасом | `picture` |
| [`video/`](video/) | зарезервировано каркасом | `video` |
| [`plans/`](plans/README.md) | планы и матрицы, не являющиеся отчётами о работе | — |
| [`archive/`](archive/README.md) | архивные отчёты | — |

Отчёты общие внутри домена (`HYAI-ART-01`).

## Индекс

| Отчёт | Домен | Профиль | Вид |
| --- | --- | --- | --- |
| [`2026-10-30-hyai-initial-setup.md`](software/2026-10-30-hyai-initial-setup.md) | `software` | `software/architect` | `task` |
| [`2026-10-30-analysis-ec-flecs.md`](software/2026-10-30-analysis-ec-flecs.md) | `software` | `software/analysis` | `analysis` |
| [`2026-10-30-static-ec-flecs-implementation.md`](software/2026-10-30-static-ec-flecs-implementation.md) | `software` | `software/code` | `task` |
| [`2026-10-30-demo-produce-cash-component.md`](software/2026-10-30-demo-produce-cash-component.md) | `software` | `software/code` | `task` |

Новый отчёт обязан появиться в этой таблице (`HYAI-ART-13`): `check_report.py` сверяет индекс с файловой системой.
