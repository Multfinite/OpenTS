---
id: meta.domains.software.readme
title: "Оверлей домена software для OpenTS"
type: meta
status: approved
authority: contract
visibility: internal
version: "1.0"
updated: "2026-10-30"
---

# Оверлей домена `software`

Наследует базовый манифест `HYAI/HYAI/core/domains/software/manifest.yaml` через `includes` и переопределяет рабочие корни под дерево `OpenTS`.

| Файл | Назначение |
| --- | --- |
| [`manifest.yaml`](manifest.yaml) | Оверлей `root_paths` (`code`, `cmake`, `tests`, `ui`) и путь к `agents.md` |
| `agents.md` | Сборочный фрагмент для `assemble_agents.py`, включающий правила домена `software` и проектный протокол чистого экспорта |
