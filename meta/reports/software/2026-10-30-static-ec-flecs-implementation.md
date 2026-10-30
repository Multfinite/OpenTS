---
id: report.software.2026-10-30-static-ec-flecs-implementation
title: "Реализация статической C++20 EC-системы поверх flecs (SW-0001)"
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
---

# Реализация статической C++20 EC-системы поверх `flecs` (`SW-0001`)

## Задача

Реализовать утверждённый владельцем контракт `SW-0001` (Вариант 2 из анализа `2026-10-30-analysis-ec-flecs.md`) `[А]`:

1. Вендоризовать `thirdparty/flecs` (`v4.1.6`) в минимальной сборке `FLECS_CUSTOM_BUILD` + `FLECS_CPP` + `FLECS_OS_API_IMPL` + `flecs_STATIC` строго как архетипное хранилище сущностей и компонентов (без компиляции ECS-систем, пайплайнов, таймеров, скриптов и REST) с полной изоляцией `<flecs.h>` внутри приватных заголовков/единиц трансляции `code/ec/`.
2. Встроить ленивый нулевой дескриптор `EC::TypedEntity<Root>` в три корневых носителя (`ObjectClass`, `CellClass`, `LocomotionClass` через `ILocomotion`), обработать аномалии памяти `CellClass` в `code/map.cpp` (`placement new`, перенос при сдвиге карты, `BlubCell`) и приостановку `flecs::Disabled` для объектов в `Limbo` и переносимых локомоторов `Piggybacker`.
3. Реализовать рекурсивный статический реестр C++20 `ComponentRegistry<Ts...>` без `void*`, без `virtual` у компонентов и без `dynamic_cast`, поддерживающий все 39 согласованных точек врезки (`G01..G12` и `E01..E27`), двухпроходный хвостовой блок `.SAV` `EC01` с поддержкой `SwizzleManagerClass` и 4-уровневый расчёт `Compute_CRC`.
4. Верифицировать контур автономным C++20 тестовым набором с реальной компиляцией `thirdparty/flecs`, модульными тестами `tools/run_unit_tests.py` и полным конвейером `HYAI/HYAI/tools/run_checks.py` (`rc=0`).

## Что сделано

- `.gitmodules`, `thirdparty/flecs`, `thirdparty/CMakeLists.txt`, `code/CMakeLists.txt`, `cmake/CheckToolkitHeaders.cmake` — подключён подмодуль `thirdparty/flecs` (`fb55f3c25660425cfe1bc4cf5e6bff8b3f18a9b8`, `v4.1.6`) `[И]`; сконфигурирована статическая цель `flecs_static` из `flecs/src/**/*.c` под единый статический MSVC CRT (`/MT`, `CMP0091`) без сброса политик вложенным `CMakeLists.txt`; в `CheckToolkitHeaders.cmake` добавлена проверка запрета утечки `<flecs.h>` за пределы `code/ec/` (включая публичные `code/ec/entity.h` и `code/ec/engine_hooks.h`).
- `code/ec/fnv1a.h`, `code/ec/entity.h`, `code/ec/world.h`, `code/ec/world.cpp`, `code/ec/concepts.h`, `code/ec/game_components.h`, `code/ec/entity_inl.h`, `code/ec/registry.h`, `code/ec/engine_hooks.h`, `code/ec/engine_hooks.cpp` — реализован модуль статической C++20 EC-подсистемы:
  - `fnv1a.h`: `constexpr` 64-битный хеш `Fnv1a_64` для стабильных `SaveName` с проверкой отсутствия коллизий на этапе компиляции (`static_assert`).
  - `entity.h` / `entity_inl.h`: `<flecs.h>`-независимые `TypedEntity<Root>`, `EntityView` и `DetachedEntitySlot` (`IdValue == 0` до первого `Emplace<C>()`, типизированная обратная связь `OwnerLink<Root>`, отложенный флаг `PendingDisabled`, перенос при реаллокации клеток).
  - `world.h` / `world.cpp`: управление жизненным циклом `flecs::world`, счётчик живых сущностей `Live_Entity_Count()` (мгновенный возврат глобальных тиков при `0` сущностей), `World_Reset()` и `World_Shutdown()`.
  - `concepts.h` / `registry.h` / `game_components.h`: C++20 концепты допустимости корня (`ComponentAllowedOn<C, Root>`), всех сигнатур `G01..G12` / `E01..E27`, рекурсивный `ComponentRegistry<Ts...>`, двухпроходный сериализатор хвостового блока `.SAV` `EC01` (подпроход 1 замораживает архетипы до регистрации адресов в `Swizzler`, подпроход 2 читает полезную нагрузку в `BoundScope`) и 4-уровневый `Compute_CRC`.
  - `engine_hooks.h` / `engine_hooks.cpp`: тонкий `<flecs.h>`-независимый фасад с инлайновой проверкой `Is_Materialized()` на быстром пути и реализацией глобальных проходов.
- `code/swizzle.h`, `code/swizzle.cpp` — добавлены `Registered_ID_Of(void const *)` (возвращает `0` для несохранённых базовых объектов вроде `BlubCell`), `Prepare_Address_Lookup()` и `Address_Of(SwizzleIDType)` для сопоставления владельцев в блоке `EC01`.
- `code/object.h`, `code/object.cpp`, `code/techno.cpp`, `code/foot.cpp`, `code/radio.cpp` — встроен `EC::TypedEntity<ObjectClass> Components` (стартует с `Set_Disabled(true)`, синхронизируется с `IsInLimbo` при `Limbo`/`Unlimbo`/`Serialize`), подключены все объектные, боевые, радио- и рендер-хуки (`E02..E16`) и пообъектный вклад в `Compute_CRC`.
- `code/cell.h`, `code/cell.cpp`, `code/map.cpp` — встроен `EC::TypedEntity<CellClass> Components`, добавлен `CellClass::Compute_CRC`, обработаны все 4 аномалии памяти клеток в `MapClass` (`Reset()` перед `placement new`, перенос `DetachedEntitySlot` при сдвиге карты с защитой `c != &BlubCell`) и подключены все клеточные хуки (`E02`, `E17..E21`).
- `code/iloco.h`, `code/loco.h`, `code/loco.cpp`, `code/ipiggy.h`, `code/drive.h`, `code/drive.cpp`, `code/walk.h`, `code/walk.cpp`, `code/droppod.h`, `code/droppod.cpp` — встроен `EC::TypedEntity<LocomotionClass> CompHolder` с виртуальным доступом `ILocomotion::Components()`, добавлен `IPiggyback::Carried_Locomotor()`, подключены хуки локомоторов (`E22..E26`) и автоматическое переключение `flecs::Disabled` для переносимого `Piggybacker`.
- `code/logic.cpp`, `code/mainloop.cpp`, `code/tactical.cpp`, `code/scenario.cpp`, `code/saveload.cpp`, `code/queue.cpp`, `code/startup.cpp` — подключены глобальные хуки `G01..G12` (`AI_All`, `PostAI_All`, `Environment_AI_All`, `Frame_Begin_All`, `Frame_End_All`, `Pre_Render_All`, `Post_Render_All`, `Scenario_Init_All`, `Scenario_Clear_All`, `Save_Trailer`/`Load_Trailer`, `Post_Load_All`, `Compute_Cell_CRCs`, `Print_Component_CRCs`, `Accumulate_Game_CRC`, `Shutdown_World`) и декорационные хуки отрисовки `E16a/E16c`.
- `tests/CMakeLists.txt`, `tests/ec/CMakeLists.txt`, `tests/ec/ectest.cpp`, `tools/tests/test_ec_flecs.py`, `tools/check_whitespace.py` — создан автономный C++20 набор `ectest` (59 проверок поверх скомпилированного `thirdparty/flecs`) и хостовый раннер `test_ec_flecs.py` (компиляция полного и пустого реестра `GameComponents` с `-Wall -Wextra -Werror`, проверка компиляции публичных заголовков без путей `flecs`, зеркало `CheckToolkitHeaders.cmake`); в `tools/check_whitespace.py` добавлен учёт путей всех внешних git-подмодулей из `.gitmodules`.

## Проверка

| Проверка | Команда | Результат |
| --- | --- | --- |
| Модульные и интеграционные тесты (`ectest` + экспорт + изоляция заголовков) | `python3 tools/run_unit_tests.py` | `Ran 8 tests ... OK` (`59/59` проверок в `ectest`) `[В]` |
| Полный конвейер проверок HYAI и репозитория | `python3 HYAI/HYAI/tools/run_checks.py --root /home/user/repo` | `0 падений` (`15/15 OK`) `[В]` |

## Открытые вопросы

Открытых вопросов нет (`Q-SW-01..Q-SW-06` закрыты решением владельца в `SW-0001`).
