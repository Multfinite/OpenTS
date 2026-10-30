---
id: decision.software.SW-0001
title: "Статически типизированная EC-система на базе flecs (без ECS-систем)"
type: decision
status: superseded
superseded_by: "SW-0002"
authority: contract
visibility: internal
scope: software
affects:
  - ".gitmodules"
  - "thirdparty/CMakeLists.txt"
  - "cmake/CheckToolkitHeaders.cmake"
  - "code/CMakeLists.txt"
  - "code/ec/fnv1a.h"
  - "code/ec/entity.h"
  - "code/ec/world.h"
  - "code/ec/world.cpp"
  - "code/ec/concepts.h"
  - "code/ec/game_components.h"
  - "code/ec/registry.h"
  - "code/ec/engine_hooks.h"
  - "code/ec/engine_hooks.cpp"
  - "code/ec/components/produce_cash.h"
  - "code/builtype.h"
  - "code/building.h"
  - "code/building.cpp"
  - "code/swizzle.h"
  - "code/swizzle.cpp"
  - "code/object.h"
  - "code/object.cpp"
  - "code/techno.cpp"
  - "code/foot.cpp"
  - "code/radio.cpp"
  - "code/cell.h"
  - "code/cell.cpp"
  - "code/map.cpp"
  - "code/iloco.h"
  - "code/loco.h"
  - "code/loco.cpp"
  - "code/ipiggy.h"
  - "code/drive.h"
  - "code/drive.cpp"
  - "code/walk.h"
  - "code/walk.cpp"
  - "code/droppod.h"
  - "code/droppod.cpp"
  - "code/logic.cpp"
  - "code/mainloop.cpp"
  - "code/tactical.cpp"
  - "code/scenario.cpp"
  - "code/saveload.cpp"
  - "code/queue.cpp"
  - "code/startup.cpp"
  - "tests/CMakeLists.txt"
  - "tests/ec/CMakeLists.txt"
  - "tests/ec/ectest.cpp"
  - "tests/producecash/CMakeLists.txt"
  - "tests/producecash/producecashtest.cpp"
supersedes: null
---

# SW-0001. Статически типизированная EC-система на базе `flecs` (без ECS-систем)

## Решение

1. **Зависимость `thirdparty/flecs` (`SW-POL-07`, `HYAI-PERM-G03`):** подключить `flecs` как вендоризованный подмодуль в режиме `FLECS_CUSTOM_BUILD` + `FLECS_CPP` + `FLECS_OS_API_IMPL` строго как архетипное хранилище компонентов и сущностей **без компиляции ECS-систем, пайплайнов, таймеров, скриптов и REST** `[А]`.
   - *Что даёт:* плотное архетипное хранение компонентов, быстрые типизированные табличные обходы `O(активные_компоненты)` и фильтрация по тегу `EcsDisabled`.
   - *Чем заменить / что будет при удалении:* все обращения изолированы за `code/ec/`; при замене `flecs` на собственное sparse-set хранилище публичный шаблонный контракт `TypedEntity<Root>` и `ComponentRegistry<Ts...>` в игровом коде не меняется.
2. **Три корня носителей `Entity` (Вариант A) `[А]`:**
   - `ObjectClass` (`TypedEntity<ObjectClass> Components`), `CellClass` (`TypedEntity<CellClass> Components`), `LocomotionClass` (`TypedEntity<LocomotionClass> Components` с чистым геттером в `ILocomotion`).
   - Идентификатор `ecs_entity_t` инициализируется нулём (`constexpr`) и материализуется лениво при первом добавлении компонента с прикреплением типизированной связи `OwnerLink<Root> { Root* ptr; }`. В `code/map.cpp` точки `placement new` вызывают `Components.Reset()`, а сдвиг буфера клеток переносит владение с обновлением `OwnerLink<CellClass>`.
3. **Статическая типизация и полный контур из 39 точек врезки (`G01..G12`, `E01..E27`) `[А]`:**
   - Рекурсивный шаблонный реестр `ComponentRegistry<Ts...>` на C++20 `concept` и `if constexpr` генерирует вызовы только для тех типов компонентов, в которых объявлен соответствующий метод (`AI` = Pre-AI, `PostAI` и остальные глобальные `G01..G12` и точечные `E01..E27` хуки), поддерживая как общие (`()`, `(EntityView)`), так и корневые (`(ObjectClass&)`, `(CellClass&)`, `(LocomotionClass&)`) сигнатуры без `void*`, `virtual` у компонентов и `dynamic_cast`.
   - Неактивные носители (`Limbo`, `Delete_Me`, вложенный `Piggybacker` в `Begin_Piggyback`/`End_Piggyback`) автоматически получают встроенный тег `flecs::Disabled` и пропускаются табличными тиками на уровне архетипа.
4. **Хвостовая секция `.SAV` (`EC01`, `HYAI-PERM-G02`) и индивидуальный `Compute_CRC` `[А]`:**
   - Состояние сериализуемых компонентов записывается отдельным двухпроходным блоком `EC01` в самом конце `Put_All`/`Get_All` (`code/saveload.cpp`): при `Offset() == Size()` старые сохранения загружаются без изменений; при чтении первый подпроход создаёт все слоты компонентов (замораживая архетипы и адреса памяти в таблицах `flecs`), второй подпроход читает полезную нагрузку через `SaveStreamClass` с поддержкой `SwizzleTarget`, после `Swizzler.Resolve()` выполняется `Post_Load`.
   - `Compute_CRC` генерируется на 4 уровнях: индивидуальный компонент (`Single_Component_CRC<C>`, `Components.Compute_CRC<C>`), вклад всех компонентов носителя (`Components.Compute_CRC`), отдельная покомпонентная и поэлементная таблица в `Print_Heap_CRCs` по стабильному ключу владельца, и вклад в `GameCRC` / `Object_CRCs`.

## Почему

Отказ от динамического реестра `void*`/`ComponentOps` (прототип ветки `ec066f46` отвергнут) даёт проверку типов и допустимого корня на этапе компиляции, полный инлайнинг и нулевую стоимость для пассивных компонентов. Отвергнуты также жадная аллокация в конструкторе (Вариант B — конфликт со статическим `BlubCell` и ~50 000 пустых клеток) и пообъектный скан всех клеток в кадре (Вариант C — `O(N_cells * K_types)`).

## Границы

- Затрагивает модуль `code/ec/`, `thirdparty/flecs`, CMake-конфигурацию и 39 согласованных точек врезки в `code/`.
- Откат возможен удалением хвостового вызова `EC01` (без поломки старых `.SAV`), отключением вызовов диспетчера в `code/ec/engine_hooks.cpp` и удалением подмодуля `thirdparty/flecs`.
- Соблюдение проверяется автономным C++20 тестовым набором (компилируемым с реальным `thirdparty/flecs`), `tools/run_unit_tests.py` и `run_checks.py`.
