---
id: decision.software.SW-0002
title: "Жизненный цикл flecs::entity 1-к-1 с корнем и прямая работа в парадигме flecs"
type: decision
status: approved
authority: contract
visibility: internal
scope: software
affects:
  - "thirdparty/CMakeLists.txt"
  - "cmake/CheckToolkitHeaders.cmake"
  - "code/CMakeLists.txt"
  - "code/ec/fnv1a.h"
  - "code/ec/world.h"
  - "code/ec/world.cpp"
  - "code/ec/entity.h"
  - "code/ec/concepts.h"
  - "code/ec/game_components.h"
  - "code/ec/registry.h"
  - "code/ec/engine_hooks.h"
  - "code/ec/engine_hooks.cpp"
  - "code/ec/components/produce_cash.h"
  - "code/object.h"
  - "code/object.cpp"
  - "code/cell.h"
  - "code/cell.cpp"
  - "code/map.cpp"
  - "code/iloco.h"
  - "code/loco.h"
  - "code/loco.cpp"
  - "code/foot.cpp"
  - "code/drive.cpp"
  - "code/walk.cpp"
  - "code/droppod.cpp"
  - "code/building.h"
  - "code/building.cpp"
  - "code/scenario.cpp"
  - "code/saveload.cpp"
  - "code/startup.cpp"
  - "tests/ec/CMakeLists.txt"
  - "tests/ec/ectest.cpp"
  - "tests/producecash/CMakeLists.txt"
  - "tests/producecash/producecashtest.cpp"
  - "tools/tests/test_ec_flecs.py"
supersedes: "SW-0001"
---

# SW-0002. Жизненный цикл `flecs::entity` 1-к-1 с корнем и прямая работа в парадигме `flecs`

## Решение

1. **Зависимость `thirdparty/flecs` без ECS-систем (`SW-POL-07`, `HYAI-PERM-G03`) `[А]`:** сохраняется вендоризованный подмодуль `flecs` (`v4.1.6`) в режиме `FLECS_CUSTOM_BUILD` + `FLECS_CPP` + `FLECS_OS_API_IMPL` строго как архетипное хранилище сущностей и компонентов без ECS-систем, пайплайнов, таймеров, скриптов и REST.
   - *Что даёт:* плотные типизированные архетипы, кэшируемые запросы `flecs::query<>`, нативные наблюдатели `.on_add`/`.on_remove` и встроенный тег `flecs::Disabled`.
   - *Чем заменить / что будет при удалении:* точки интеграции с главным циклом и сейвами собраны в `code/ec/`; при отказе от `flecs` потребуется замена дескриптора `Entity` в трёх корнях и обходов в `code/ec/registry.h`.
2. **Жизненный цикл 1-к-1 без ленивого создания и чистый дескриптор `flecs::entity` `[А]`:**
   - Вся ленивая материализация и промежуточные обёртки (`TypedEntity`, `EntityView`, `DetachedEntitySlot`, `code/ec/entity_inl.h`) отменяются и удаляются.
   - Три корня (`ObjectClass`, `CellClass`, `LocomotionClass`) хранят исключительно `flecs::entity Entity;` (плюс `virtual flecs::entity Get_Entity() const noexcept = 0;` в `ILocomotion`). Сущность создаётся в конструкторе корня через `EC::Create_Entity(this)` (с привязкой `OwnerLink<Root>{this}`) и уничтожается в деструкторе через `EC::Destroy_Entity(Entity)`.
   - Единый `EC::World()` создаётся при первом обращении (с автоматическим вызовом `Set_World_Init_Hook` вне зависимости от порядка статической инициализации `BlubCell` и `engine_hooks.cpp`), **не сбрасывается** в `Clear_Scenario()` (сценарные сущности удаляются 1-к-1 деструкторами корней, сохраняя валидность глобального `BlubCell.Entity`) и штатно завершается в `Prog_End()` через `World_Shutdown()`, после чего поздние статические деструкторы безопасно превращаются в no-op.
   - В `code/map.cpp` все 8 точек `placement new` над `CellClass` предварительно вызывают `EC::Destroy_Entity(...Entity)`, а сдвиг буфера в `Set_Map_Dimensions(!reset_cells)` переносит `flecs::entity` с перепривязкой `OwnerLink<CellClass>` и удалением вытесненных сущностей.
3. **Прямая работа в парадигме `flecs` и статическая диспетчеризация (`G01..G12`, `E01..E27`) `[А]`:**
   - Код движка и компонентов работает напрямую с `flecs::entity` (`Entity.add<C>()`, `Entity.emplace<C>()`, `Entity.set<C>()`, `Entity.get<C>()`, `Entity.try_get<C>()`, `Entity.has<C>()`, `Entity.remove<C>()`, `Entity.add(flecs::Disabled)` / `Entity.remove(flecs::Disabled)`), а хуки `On_Attach`/`On_Remove` регистрируются как нативные наблюдатели `world.component<C>().on_add`/`.on_remove`.
   - `ComponentRegistry<Ts...>` сохраняет C++20 статическую типизацию всех 39 точек врезки (`G01..G12`, `E01..E27`) без `void*`-реестра и `dynamic_cast`, используя постоянные кэшированные запросы `flecs::query<>` (привязанные к времени жизни `flecs::world`) для глобальных проходов и O(1)-проверку таблицы архетипа `Has_User_Components(Entity)` на точечных событиях.
4. **Хвостовая секция `.SAV` (`EC01`) и 4-уровневый `Compute_CRC` `[А]`:**
   - `Save_Trailer` обходит только архетипы сериализуемых компонентов (игнорируя тысячи пустых клеток/объектов); `Load_Trailer` разрешает `SwizzleID` сразу в существующий `flecs::entity` загруженного корня и выполняет двухфазное чтение (`Subpass 1` замораживает таблицы, `Subpass 2` читает поля и регистрирует `Swizzle`-слоты). 4-уровневый `Compute_CRC` работает напрямую над `flecs::entity`.

## Почему

Директива владельца отменяет ленивую материализацию и собственные обёртки над сущностью (`SW-0001`): хранение чистого `flecs::entity` 1-к-1 с корнем устраняет дублирующий слой абстракций (`TypedEntity`/`EntityView`), рассинхронизацию отложенного `PendingDisabled`, ручную диспетчеризацию `On_Attach`/`On_Remove` и необходимость сбрасывать `flecs::world` между сценариями, позволяя всему последующему коду использовать идиоматичный API `flecs` напрямую.

## Границы

- Затрагивает `code/ec/`, три корневых класса (`ObjectClass`, `CellClass`, `LocomotionClass`/`ILocomotion`), `code/map.cpp`, точки интеграции жизненного цикла/сейвов/локомоторов, `code/building.{h,cpp}` и тесты `tests/{ec,producecash}`.
- Проверяется компиляцией C++20 с `-Wall -Wextra -Werror` в `tools/tests/test_ec_flecs.py` (`ectest`, `producecashtest`), `tools/run_unit_tests.py`, `HYAI/HYAI/tools/run_checks.py` и `tools/export_branch.py verify`.
