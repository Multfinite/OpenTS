---
id: meta.decisions.software
title: "Решения домена software"
type: meta
status: approved
authority: contract
visibility: internal
version: "2.1"
updated: "2026-10-30"
---

# Решения домена software

Префикс `SW`. Формат файла: `SW-NNNN-<slug>.md`. Бюджет — 120 строк (`HYAI-DEC-11`).

| ID | Название | Статус |
| --- | --- | --- |
| [`SW-0001`](SW-0001-static-ec-flecs-architecture.md) | Статически типизированная EC-система на базе `flecs` (без ECS-систем) | superseded (`SW-0002`) |
| [`SW-0002`](SW-0002-eager-flecs-entity-root-lifetime.md) | Жизненный цикл `flecs::entity` 1-к-1 с корнем и прямая работа в парадигме `flecs` | approved |
