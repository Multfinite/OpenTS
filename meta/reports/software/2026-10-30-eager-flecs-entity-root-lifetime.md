---
id: report.software.2026-10-30-eager-flecs-entity-root-lifetime
title: "Переход EC-архитектуры на 1:1 жизненный цикл flecs::entity и прямую парадигму flecs"
type: report
report_kind: task
domain: software
profile: software/code
status: review
authority: none
visibility: internal
date: "2026-10-30"
workflow: task
decisions:
  - "SW-0001"
  - "SW-0002"
---

# Переход EC-архитектуры на 1:1 жизненный цикл `flecs::entity` и прямую парадигму `flecs`

## Задача

Выполнить архитектурный рефакторинг EC-подсистемы по двум директивам владельца:
1. Полностью отменить ленивое создание сущностей и удалить все механизмы ленивой материализации — время жизни сущности `flecs` должно совпадать с временем жизни корневого объекта 1-к-1 (создание в конструкторе, уничтожение в деструкторе).
2. Хранить в корневых объектах (`ObjectClass`, `CellClass`, `LocomotionClass`) исключительно дескриптор `flecs::entity` и перевести весь последующий код на работу непосредственно в парадигме `flecs`.

## Что сделано

- `code/ec/entity_inl.h` (удалён), `cmake/CheckToolkitHeaders.cmake` (возвращён к `main`), `code/ec/{world.h,world.cpp,entity.h,concepts.h,registry.h,engine_hooks.h,engine_hooks.cpp}` — полностью удалены все обёртки и артефакты ленивой материализации (`TypedEntity`, `EntityView`, `DetachedEntitySlot`, `Is_Materialized`, `Ensure_Materialized`, `PendingDisabled`, `Materialize_Loaded_Owner`, `Ensure_Produce_Cash`, `World_Reset`):
  - введены 1-к-1 хелперы `EC::Create_Entity(this)` и `EC::Destroy_Entity(Entity)` с привязкой `OwnerLink<Root>{this}` и безопасным `Set_World_Init_Hook` (независимым от порядка статической инициализации между глобальным `BlubCell` в `code/map.cpp` и `code/ec/engine_hooks.cpp`);
  - отменён сброс `flecs::world` при `Clear_Scenario()`: сценарные сущности уничтожаются 1-к-1 деструкторами корней без превращения `BlubCell.Entity` в висячий дескриптор, а при `Prog_End()` вызывается `World_Shutdown()`, после которого поздние статические деструкторы безопасно превращаются в no-op;
  - хуки `On_Attach` и `On_Remove` переведены на нативные наблюдатели `world.component<C>().on_add` и `.on_remove`, срабатывающие при любых прямых вызовах `Entity.add<C>()`, `Entity.emplace<C>()`, `Entity.set<C>()`, `Entity.remove<C>()` и `entity.destruct()`;
  - глобальные проходы `ComponentRegistry` (`AI_All`, `PostAI_All` и др.) переведены на постоянные кэшированные запросы `flecs::query<>`, хранимые как синглтоны `flecs::world`, а точечные события `E02..E27` используют O(1)-проверку архетипа `Has_User_Components(Entity)`;
  - `Save_Trailer` обходит только архетипы сериализуемых компонентов (`O(активные_компоненты)`, не затрагивая тысячи пустых клеток/объектов), а `Load_Trailer` разрешает `SwizzleID` сразу в существующий `flecs::entity` загруженного корня.
- `code/{object,cell,loco}.{h,cpp}`, `code/iloco.h` — поля `Components`/`CompHolder` заменены на `flecs::entity Entity;` (и `virtual flecs::entity Get_Entity(void) const noexcept = 0;` в `ILocomotion`), создаваемые в конструкторах и уничтожаемые в деструкторах; состояние лимбо переключается напрямую через `Entity.add(flecs::Disabled)` / `Entity.remove(flecs::Disabled)`.
- `code/map.cpp`, `code/{foot,drive,walk,droppod,building}.cpp` — все 8 точек `placement new` над `CellClass` предварительно вызывают `EC::Destroy_Entity(...Entity)`; сдвиг буфера в `Set_Map_Dimensions(!reset_cells)` переносит `flecs::entity` с перепривязкой `OwnerLink<CellClass>` и очисткой вытесненных сущностей; загрузка локомоторов и `BuildingClass` работают напрямую с `Get_Entity().add(flecs::Disabled)` и `Entity.add<EC::ProduceCashComponent>()`.
- `meta/decisions/software/SW-0001-static-ec-flecs-architecture.md`, `meta/decisions/software/SW-0002-eager-flecs-entity-root-lifetime.md`, `meta/decisions/{README.md,software/README.md}`, `meta/architecture/software.md` — оформлено решение `SW-0002` (`supersedes: "SW-0001"`), `SW-0001` переведено в `superseded`, обновлены архитектурные инварианты.
- `tests/ec/{CMakeLists.txt,ectest.cpp}`, `tests/producecash/{CMakeLists.txt,producecashtest.cpp}`, `tools/tests/test_ec_flecs.py` — тесты переписаны под 1:1 `flecs::entity` и прямую парадигму `flecs` (67 проверок в `ectest`, 16 проверок в `producecashtest`, тест пустого реестра и статическая проверка отсутствия любых ленивых обёрток).

## Проверка

| Проверка | Команда | Результат |
| --- | --- | --- |
| Автономные C++20 тесты `ectest` и `producecashtest` (`-Wall -Wextra -Werror`) | `python3 tools/tests/test_ec_flecs.py -v` | `Ran 4 tests ... OK` (`67/67` в `ectest`, `16/16` в `producecashtest`) `[В]` |
| Все модульные тесты инструментов и EC | `python3 tools/run_unit_tests.py` | `OK` `[В]` |
| Полный конвейер проверок HYAI и репозитория | `python3 HYAI/HYAI/tools/run_checks.py --root /home/user/repo` | `0 падений` (`15/15 OK`) `[В]` |
| Проверка чистоты экспортного среза без следов HYAI | `python3 tools/export_branch.py verify` | `OK` `[В]` |

## Открытые вопросы

Открытых вопросов нет.
