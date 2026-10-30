---
id: meta.schema.readme
title: "meta/schema/ — схемы frontmatter и идентификаторов правил"
type: meta
status: approved
authority: contract
visibility: internal
version: "1.0"
updated: "2026-10-30"
---

# `meta/schema/`

Машинные схемы, которые читают [`frontmatter.py`](../../HYAI/HYAI/tools/frontmatter.py) и [`check_rules.py`](../../HYAI/HYAI/tools/check_rules.py).

| Путь | Что |
| --- | --- |
| [`frontmatter.yaml`](frontmatter.yaml) | Базовая схема frontmatter, запрещённые поля-флаги и исключения для апстрим-файлов `OpenTS` |
| [`rule-ids.yaml`](rule-ids.yaml) | Владельцы и классы идентификаторов правил (`HYAI`, `SW`, `DOC`, `PRJ` и др.), пути источников и список `retired` |
| [`domains/`](domains/README.md) | Доменные оверлеи схемы frontmatter (`software.yaml`, `docs.yaml`, `kit.yaml`, `narrative.yaml`, `picture.yaml`, `video.yaml`) |
