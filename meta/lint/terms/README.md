---
id: meta.lint.terms.readme
title: "HYAI/meta/lint/terms/ — правила терминов по доменам"
type: meta
status: approved
authority: contract
visibility: internal
version: "2.0"
updated: "2026-10-22"
---

# Правила терминов

Один файл — один домен. `applies_to` обязателен: правило без зоны действия ударит по чужому домену.

| Файл | Домен | Что ловит |
| --- | --- | --- |
| [`kernel.json`](kernel.json) | `kernel` | Лесть и выпрашивание одобрения в отчётах, отказ без ссылки на правило |
| [`software.json`](software.json) | `software` | Магические числа, пустой `except`, `FIXME` без `Q-` |
| [`docs.json`](docs.json) | `docs` | Вода, `TODO` без владельца, схема только картинкой |
| [`narrative.json`](narrative.json) | `narrative` | Канцелярит в описании мира, абсолютные утверждения без маркера |
| [`picture.json`](picture.json) | `picture` | Незаданные оси безопасности, пустой `negative`, штампы качества, смешение весов, брак в хранилище, ранняя стилизация |
| [`video.json`](video.json) | `video` | Незаданный ответ о звуке, storage policy или handoff сегментов |

Формат правила: `id`, `severity`, `pattern`, `applies_to`, `reason`, опционально `suggest`.
