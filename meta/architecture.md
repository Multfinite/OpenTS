---
id: meta.architecture.hub
title: "Архитектура OpenTS: хаб доменов"
type: architecture
status: approved
authority: contract
visibility: internal
version: "1.0"
updated: "2026-10-30"
---

# Архитектура: хаб

Здесь нет норм — только границы доменов и ссылки на их архитектурные карты. Нормы живут в `meta/architecture/<domain>.md` и `meta/policies/*.md`.

## Активные домены `OpenTS`

| Домен | Архитектура домена | Границы в `OpenTS` |
| --- | --- | --- |
| `software` | [`architecture/software.md`](architecture/software.md) | Исходный код движка (`code/`), сборка (`cmake/`, `CMakeLists.txt`), тесты (`tests/`), ресурсы UI (`ui/`) |
| `docs` | [`architecture/docs.md`](architecture/docs.md) | Инженерная документация в `docs/` и связь со справочником `manual/` |

## Справочные шаблоны неактивных доменов каркаса

- [`architecture/kit.md`](architecture/kit.md)
- [`architecture/narrative.md`](architecture/narrative.md)
- [`architecture/picture.md`](architecture/picture.md)
- [`architecture/video.md`](architecture/video.md)

## Правила хаба

1. Хаб не содержит правил: предметные нормы живут в доменных файлах и `meta/policies/`.
2. Связь между доменами и сквозные ограничения (включая чистый экспорт `P-0001`) фиксируются в `meta/decisions/project/`.
