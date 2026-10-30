---
id: meta.architecture.docs
title: "Архитектура домена docs (OpenTS)"
type: architecture
status: approved
authority: contract
visibility: internal
version: "1.0"
updated: "2026-10-30"
---

# Архитектура домена `docs`

## Корпус документов

| Каталог | Что | Уровень по умолчанию |
| --- | --- | --- |
| `docs/BUILDING.md`, `docs/SAVE-FORMAT.md`, `docs/STYLE.md` | Инженерные руководства и описание форматов `[И] docs/` | `descriptive` |
| `docs/DIRECTION.md`, `docs/RATIONALE.md`, `docs/HISTORY.md`, `docs/TS-PATCHES.md` | Архитектурный вектор, обоснования и история реконструкции `[И] docs/` | `descriptive` |
| `manual/` | Публичный справочник INI-ключей, скриптинга и истории изменений (`manual/tools/manage.py`) `[И] manual/README.md` | собственный контракт `manual/` |

## Инварианты

| ID | Инвариант | Чем проверяется |
| --- | --- | --- |
| `DOC-INV-01` | Требование без критерия приёмки не является требованием (`DOC-POL-02`) | ревью + `lint_terms.py --domain docs` |
| `DOC-INV-02` | Документ не выдаёт планируемое поведение за уже реализованное в движке (`DOC-POL-03`) | сверка с `code/` и `Q-DOC-*` |
| `DOC-INV-03` | Схемы в новых спецификациях хранятся текстом (`DOC-POL-10`) | правило `doc.diagram-as-image-only` |
| `DOC-INV-04` | Экспортируемая документация в `docs/` и `manual/` не содержит frontmatter и маркеров `HYAI` (`PRJ-POL-03`, `P-0001`) `[А]` | `python3 tools/export_branch.py verify` |

## Анти-паттерны

| Не делаем | Почему | Что вместо |
| --- | --- | --- |
| Ручная правка генерируемых файлов `manual/data/*.yaml` | Перезаписывается генератором `manual/tools/manage.py` `[И] manual/README.md` | Обновление через штатный инструмент `manual/tools/manage.py` |
| Смешение проектных отчётов `HYAI` и продуктовой документации `docs/` | Загрязняет экспортную ветку (`PRJ-POL-02`) | Проектные артефакты агента хранить строго в `meta/` |
| Диаграмма только растровой картинкой | Не диффится и устаревает | Mermaid в Markdown |

## Решения

Действующие решения домена: [`../decisions/docs/README.md`](../decisions/docs/README.md).
