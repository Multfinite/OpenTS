---
id: meta.readme
title: "meta/ — проектные данные HYAI для OpenTS"
type: meta
status: approved
authority: invariant
visibility: internal
version: "1.0"
updated: "2026-10-30"
---

# `meta/` — данные проекта `OpenTS`

Проектный слой `HYAI` для репозитория `OpenTS`. Ядро правил и встроенные движки подключены подмодулем в `HYAI/`, а конфигурация, политики, решения и отчёты этого репозитория хранятся здесь (`[А]`, решение [`P-0001`](decisions/project/P-0001-hyai-contracts-and-clean-export.md)).

## Состав

| Путь | Что | Уровень |
| --- | --- | --- |
| [`manifest.yaml`](manifest.yaml) | Активные домены и профили, пути, правила чистого экспорта, проверки | Проект |
| [`project.md`](project.md) | Паспорт `OpenTS`, активные домены, карта репозитория | Проект |
| [`policies/`](policies/README.md) | Проектные политики `PRJ-POL-*` (включая чистый экспорт без `HYAI`) | Проект |
| [`domains/`](domains/README.md) | Оверлеи доменов под дерево каталогов `OpenTS` | Домен |
| [`architecture.md`](architecture.md) | Хаб архитектуры доменов | Проект |
| [`architecture/`](architecture/README.md) | Архитектурные карты и инварианты доменов | Домен |
| [`specs/`](specs/README.md) | Внутренние ТЗ и спецификации профиля `docs/spec-author` | Домен `docs` |
| [`diagrams/`](diagrams/README.md) | Текстовые диаграммы профиля `docs/diagrams` | Домен `docs` |
| [`terminology.md`](terminology.md) | Кросс-доменные термины | Проект |
| [`terminology/`](terminology/README.md) | Словари доменов | Домен |
| [`questions.md`](questions.md) | Открытые проектные вопросы `Q-P-*` | Проект |
| [`questions/`](questions/README.md) | Открытые вопросы доменов | Домен |
| [`decisions/`](decisions/README.md) | Принятые решения по областям | По области |
| [`schema/`](schema/README.md) | Схема frontmatter, доменные оверлеи и реестр `rule-ids.yaml` | Проект + домен |
| [`lint/`](lint/README.md) | Данные проверок ссылок, пробелов и терминов | Проект + домен |
| [`registry/`](registry/README.md) | Репозиторный реестр `documents.yaml` | Проект |
| [`reports/`](reports/README.md) | Отчёты о выполненных задачах, планы и архив | Домен |
| [`intermediate/`](intermediate/README.md) | Временные рабочие артефакты и SPA-опросы (не попадают в коммиты) | Проект |
