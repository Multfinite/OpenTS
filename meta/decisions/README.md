---
id: meta.decisions.readme
title: "Решения OpenTS: индекс по областям"
type: meta
status: approved
authority: contract
visibility: internal
version: "1.1"
updated: "2026-10-30"
---

# Решения

Одно решение — одна действующая норма. Нумерация независима внутри каждой области (`HYAI-DEC-05`).

| Область | Каталог | Префикс | Что сюда попадает |
| --- | --- | --- | --- |
| проект | [`project/`](project/README.md) | `P` | нормы, затрагивающие весь репозиторий и несколько доменов |
| каркас | [`kit/`](kit/README.md) | `KIT` | локальные решения по оверлеям каркаса |
| `software` | [`software/`](software/README.md) | `SW` | архитектура движка, контракты модулей, сборка и тесты |
| `docs` | [`docs/`](docs/README.md) | `DOC` | структура спецификаций и документации |
| `narrative` | [`narrative/`](narrative/README.md) | `NR` | зарезервировано каркасом |
| `picture` | [`picture/`](picture/README.md) | `PIC` | зарезервировано каркасом |
| `video` | [`video/`](video/README.md) | `VID` | зарезервировано каркасом |

## Реестр решений

| ID | Область | Название | Статус |
| --- | --- | --- | --- |
| [`P-0001`](project/P-0001-hyai-contracts-and-clean-export.md) | project | Приоритет контрактов HYAI и правило чистого экспорта ветки | approved |
| [`SW-0001`](software/SW-0001-static-ec-flecs-architecture.md) | software | Статически типизированная EC-система на базе `flecs` (без ECS-систем) | superseded (`SW-0002`) |
| [`SW-0002`](software/SW-0002-eager-flecs-entity-root-lifetime.md) | software | Жизненный цикл `flecs::entity` 1-к-1 с корнем и прямая работа в парадигме `flecs` | approved |

## Правила

1. Решение, касающееся двух доменов или всего проекта, живёт в `project/` (`HYAI-DEC-02`).
2. Файл без строки в индексе и строка без файла — ошибки (`HYAI-DEC-13`).
