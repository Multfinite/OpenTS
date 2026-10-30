---
id: meta.schema.domains.readme
title: "meta/schema/domains/ — оверлеи схем по доменам"
type: meta
status: approved
authority: contract
visibility: internal
version: "2.0"
updated: "2026-10-30"
---

# Оверлеи схем

Каждый файл заменяет часть базовой схемы [`../frontmatter.yaml`](../frontmatter.yaml) для путей своего домена.

| Файл | Домен | Что меняет |
| --- | --- | --- |
| [`software.yaml`](software.yaml) | `software` | Словарь `authority`, запрет литературных значений |
| [`docs.yaml`](docs.yaml) | `docs` | `normative/descriptive/illustrative/informative`, типы документов |
| [`narrative.yaml`](narrative.yaml) | `narrative` | `core/setting/detail/presentation`, шесть контуров, ось `knowledge` |
| [`picture.yaml`](picture.yaml) | `picture` | `visual-canon/brief/prompt/stage`, брифы, промпты, звенья и карточки ассетов |
| [`video.yaml`](video.yaml) | `video` | `video-canon/brief/shot-plan/continuity`, сегменты и delivery records |
| [`kit.yaml`](kit.yaml) | `kit` | Узкий словарь каркаса, запрет `implementation` |

Правила слияния — `MERGE-01..06` в [`merge-rules.md`](../../../HYAI/HYAI/core/kernel/spec/merge-rules.md).
