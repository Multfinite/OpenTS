---
id: meta.lint.readme
title: "meta/lint/ — данные проверок OpenTS"
type: meta
status: approved
authority: contract
visibility: internal
version: "1.0"
updated: "2026-10-30"
---

# `meta/lint/`

Правила проверок хранятся как данные. Новое правило добавляется записью JSON, а не правкой Python (`3D`).

| Путь | Чем читается | Что задаёт |
| --- | --- | --- |
| [`links.json`](links.json) | `check_links.py` | Расширения, шаблоны плейсхолдеров и исключения путей |
| [`whitespace.json`](whitespace.json) | `check_whitespace.py` → `whitespace.py` | Проверяемые расширения и правила концовки строк/файлов |
| [`terms/`](terms/README.md) | `lint_terms.py` | Правила терминов и анти-паттернов по доменам (`kernel.json`, `software.json`, `docs.json`, `narrative.json`, `picture.json`, `video.json`) |
