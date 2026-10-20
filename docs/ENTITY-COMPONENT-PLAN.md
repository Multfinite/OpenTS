# План перехода на модель Entity-Component и жизненный цикл компонентов (Flecs без систем)

**Статус:** рабочий сводный документ ветки разработки (`arena/ec066f46-opents` поверх `infrastructure/entity-component`). В ветке `main` игра ещё не использует Flecs и хранилище компонентов; функциональность в целом ещё **не завершена** и находится в процессе сборки по этапам. Документ объединяет в одном месте архитектурный план внедрения сущностей для всех игровых классов, спецификацию жизненного цикла компонентов (`Ready`, `Process`, вход и выход из мира, отцепление ссылок, рендер, сохранения, контрольные суммы CRC и разрушение), разбор точек интеграции с циклом движка и текущий статус реализации.

Связанные документы репозитория: [Project direction](DIRECTION.md) фиксирует долгосрочный курс на переход к композиции, [Building OpenTS](BUILDING.md) задаёт поддерживаемые тулчейны и сборку зависимостей, [Style](STYLE.md) — правила оформления C++20-кода, [The saved game format](SAVE-FORMAT.md) — бинарную разметку файлов `.SAV` и работу `SwizzleManager`, [Contributing](../CONTRIBUTING.md) — правила проверки и оформления изменений.

---

## Состояние готовности на текущей ветке

- **Готово в коде и проверено автономными тестами:**
  - Инвентаризация и автоматическая проверка `cmake/CheckEntityInventory.cmake` (`entityinventory`) по всем 65 включённым, 5 исключённым и 1 отложенному классу.
  - Вендоринг подмодуля `thirdparty/flecs` (`v4.1.6`, коммит `fb55f3c25660425cfe1bc4cf5e6bff8b3f18a9b8`) в статическую библиотеку `flecs_static` только с ядром C99 и C++ API (`FLECS_CUSTOM_BUILD`, `FLECS_CPP`, `FLECS_OS_API_IMPL`).
  - Привязка сущностей в конструкторах всех 65 включённых листовых классов (81 тело конструктора), безопасное зануление `Entity` при копировании (`AbstractClass`, `LocomotionClass`), удаление сущностей в `~AbstractClass()` и `~LocomotionClass()`, а также привязка дочерних сущностей локомоторов через `FlecsWorld.Set_Entity_Parent` в `LocomotionClass::Link_To_Object` и `LocomotionClass::Post_Load`.
  - Ядро моста и диспетчера жизненного цикла **Варианта B (`ComponentOps`)** в `code/flecs/` (`id.hh`, `lifecycle.hh`, `components.hh`, `world.h`, `world.cpp`, `register.cpp`) и `constinit`-объект `FlecsWorld` в `code/_flecs.cpp`.
  - Автономный C++20-тестовый набор `tests/flecs` (с реальным `code/crc.cpp`), скомпилированный и пройденный под Linux (`g++` 12.2, `-std=c++20 -Wall -Wextra -Werror`) в режимах Debug с санитайзерами (`ASan + UBSan + LSan`, 0 ошибок и 0 утечек) и Release (`-O2 -DNDEBUG`), включая compile-time тесты `static_assert` на некорректные сигнатуры хуков.
- **Ещё не готово (следующие этапы):**
  - Вшивка точек вызова #2–#9 в основные файлы цикла движка (`logic.cpp`, `scenario.cpp`, `object.cpp`, `tracker.cpp`, `saveload.cpp`, `tactical.cpp`, `startup.cpp` и хвосты листовых `Unlimbo()`).
  - Выбор и сквозная реализация первой реальной геймплейной фичи на компонентах (решение D4).
  - Сборка всех четырёх целевых конфигураций MSVC (`Win32` / `x64` × `Debug` / `Release`), прогон `ctest` на Windows (`tests/save`, `tests/desync`, `tests/syncrec`), замеры памяти и времени кадра на 262 144 клетках `CellClass` и живые игровые проверки (кампания, скирмиш, цикл сохранения/загрузки, сетевая синхронизация).

---

## 1. Базовые правила и границы использования Flecs

Архитектура опирается на три жёстких правила:

1. **Никаких систем Flecs.** Ни в одной фазе не регистрируются `ecs_system`, конвейеры (`pipeline`), встроенные фазы расписания или вызовы `world.progress()`. Ничто не продвигает симуляцию неявно.
2. **Сущность создаётся для каждого экземпляра финального игрового класса из утверждённого списка (§2).** «Финальный» означает листовой класс в иерархиях `AbstractClass` и `LocomotionClass`, от которого никто больше не наследуется, за вычетом пяти осознанно исключённых классов.
3. **Компоненты несут только новую функциональность.** Ни одно существующее поле старых C++-классов не переносится в компоненты в рамках базовой миграции: существующий C++-объект остаётся единственным владельцем своих старых полей и виртуального поведения, а `EntityId` служит точкой расширения для новых механик.

Flecs разделяет хранилище архетипов и планировщик систем. Движок использует только хранилище и явные запросы под полным контролем `FlecsWorldClass`:

| Механизм Flecs | Статус и правила использования в OpenTS |
| --- | --- |
| `flecs::world`, `flecs::entity` | **Да.** Единственный мир владеет всеми сущностями; заголовок `<flecs.h>` включён строго в один файл `code/flecs/world.cpp`. |
| Компоненты и теги (`ecs_component_init`, `add/set/get/remove`) | **Да**, только для новых фич и служебных связок моста (`LegacyObjectLink`, `LegacyLocomotionLink`, `EntityBridgeMeta`). |
| Отношения (`ChildOf`, пары) | **Да**, для дочерних сущностей локомоторов (`ChildOf`), связки с типом (`TypeOf`) и явных связей новых механик. |
| Префабы (`IsA`) | **Отложено.** Автоматическое наследование компонентов через `IsA` требует отдельного решения по семантике копирования; по умолчанию используется явное копирование данных типа при создании экземпляра. |
| Запросы (`ecs_query_t`, `world.query`) | **Да**, но вызываются только явно из конкретных точек движка внутри `FlecsWorldClass` с обязательной канонической сортировкой для симуляции и CRC. |
| Отложенный буфер (`defer_begin` / `defer_end`) | **Да**, как защитная область вокруг явных циклов обхода, чтобы добавление/удаление компонентов и уничтожение сущностей не инвалидировало активные таблицы архетипов. |
| Глобальные `world.observer` (`OnAdd` / `OnSet`) | **Нет.** Скрытые синхронные коллбэки при записи поля ломают предсказуемость стека вызовов посреди `AI()`. Низкоуровневые storage-хуки `ecs_type_hooks_t` (`ctor`, `dtor`, `move`, `copy`, `on_remove`) разрешены исключительно внутри `world.cpp` для управления временем жизни C++-объектов и точного учёта `LiveCount`. |
| Системы (`world.system`, `ecs_system`) | **Никогда.** |
| Конвейеры, фазы, `world.progress()`, таймеры, `flecs::app` | **Никогда.** Симуляцией управляет только `Main_Loop()` → `LogicClass::AI()`. |
| Аддоны Flecs: script, JSON, meta/reflection, REST, HTTP, stats, units | **Выключены на этапе сборки** (`FLECS_CUSTOM_BUILD`). Не добавляют парсеров, рефлексии и сетевых сокетов в процесс игры. |
| Многопоточность (`world.set_threads`) | **Нет.** Детерминированная lockstep-симуляция выполняется строго в одном потоке. |

---

## 2. Охват классов и два дерева иерархии

### 2.1 Аудит классов кодовой базы

Инвентаризация всех объявлений классов в `code/*.h` и `code/*.hh` (без `code/language/`) даёт следующую картину:

| Категория классов | Количество |
| --- | --- |
| Всего классов в `code/*.h`, `code/*.hh` | 598 |
| Листовых классов по всему дереву (включая UI, файлы, сеть, рендер, контейнеры) | 541 |
| Внутренних (нелистовых) базовых классов в двух игровых деревьях | 11 |
| Листовых классов-кандидатов в двух игровых деревьях (`AbstractClass` + `LocomotionClass`) | 71 (61 + 10) |
| **Исключены осознанным решением** | **5** |
| **Отложено решение (`Tactical`)** | **1** |
| **Классы, получающие сущность Flecs** | **65** (66 в случае включения `Tactical`) |

Пять исключённых листовых классов: `AnimClass`, `IsometricTileClass`, `LogicClass`, `TActionClass`, `TEventClass`. Одиннадцать промежуточных классов (`AbstractClass`, `ObjectClass`, `MissionClass`, `RadioClass`, `TechnoClass`, `FootClass`, `LayerClass`, `AbstractTypeClass`, `ObjectTypeClass`, `TechnoTypeClass`, `LocomotionClass`) собственных сущностей не создают: экземпляр `UnitClass` представлен ровно **одной** сущностью с тегом `"UnitClass"`.

### 2.2 Два дерева игровых классов

```text
AbstractClass : IPersistent                          code/abstract.h
├─ AbstractTypeClass                                 code/abstype.h
│  ├─ 13 прямых листьев (CampaignClass, HouseTypeClass, ScriptTypeClass,
│  │   SideClass, SuperWeaponTypeClass, TagTypeClass, TaskForceClass,
│  │   TeamTypeClass, TiberiumClass, TriggerTypeClass, WarheadTypeClass,
│  │   WeaponTypeClass, AITriggerTypeClass)
│  └─ ObjectTypeClass
│     ├─ 9 прямых листьев (AnimType, BulletType, IsometricTileType,
│     │   OverlayType, ParticleSystemType, ParticleType, SmudgeType,
│     │   TerrainType, VoxelAnimType)
│     └─ TechnoTypeClass → AircraftType, BuildingType, InfantryType, UnitType
├─ ObjectClass                                       code/object.h
│  ├─ 12 прямых листьев (Anim ✗, BuildingLight, Bullet, IsometricTile ✗,
│  │   Overlay, Particle, ParticleSystem, Smudge, Terrain,
│  │   VeinholeMonster, VoxelAnim, Wave)
│  └─ MissionClass → RadioClass → TechnoClass
│        ├─ BuildingClass
│        └─ FootClass → AircraftClass, InfantryClass, UnitClass
├─ LayerClass → LogicClass ✗ (глобальный драйвер кадра)
└─ 18 прямых листьев (AlphaShape, Cell, EMPulse, Factory, FoggedObject,
    House, LightSource, Neuron, Script, Super, TAction ✗, TEvent ✗,
    Tactical ?, Tag, Team, Trigger, Tube, WaypointPath)

LocomotionClass : IPersistent, ILocomotion           code/loco.h
└─ Drive, DropPod, Fly, Hover, Jumpjet, Levitate, Mech, Teleport,
   Tunnel, Walk (10 листьев; часть реализует IPiggyback)
```

Проверка суммы: `26` листьев типов + `12` не-боевых `ObjectClass` + `4` класса `TechnoClass` + `19` прочих листьев `AbstractClass` = `61` кандидат; минус `5` исключённых (`AnimClass`, `IsometricTileClass`, `LogicClass`, `TActionClass`, `TEventClass`) = `56` (из них `1` под вопросом — `Tactical`, итого `55` активных листьев `AbstractClass`) + `10` локомоторов = **65 классов с сущностями**.

### 2.3 Разбор особых случаев и архитектурные решения (D1–D5)

| Особый случай | Факты из кода | Принятое решение |
| --- | --- | --- |
| `CellClass` — 262 144 слота (`MAP_CELL_W × MAP_CELL_H` = 512×512, `code/sun.h`) в `MapClass::Array`, реконструируются placement `new` в `MapClass::Init_Cells()` без вызова `~CellClass()`, плюс глобальный `BlubCell` до `main` | 262k сущностей на порядок больше всех остальных объектов вместе взятых. | **Включён полностью** (включая `BlubCell`). Placement reconstruction закрывается через `Replace_Entity(Entity, this, "CellClass")`, уничтожающий старую сущность перед выдачей новой. Закрытие волны C требует замера памяти, времени сброса сценария и цены кадра. |
| `IsometricTileClass` (`IsometricTiles`) | Не зарегистрирован в `RegisterClasses()` для независимой фабричной загрузки. | **Исключён (`✗`).** Доступ к данным тайлов идёт через карту и сущности типов `IsometricTileTypeClass`. |
| `LogicClass` (глобальный `Logic`) | Единственный драйвер симуляции; не игровой объект, не удаляется и сохраняет только состав слоя. | **Исключён (`✗`).** |
| `Tactical` (глобальный `TacticalMap`) | Зарегистрирован и сохраняется, существует в единственном экземпляре. | **Решение D5 открыто (`?`).** В список пока не включён; ничего не зависит от наличия у него сущности до закрытия волны D. |
| Локомоторы (10 листьев `LocomotionClass`) | Владелец — `std::unique_ptr<ILocomotion> FootClass::Locomotion` + временная подмена стеком `IPiggyback` (`code/foot.cpp:3319,3598`, `aircraft.cpp:1362`, `building.cpp:6369`, `infantry.cpp:1199,4157`, `unit.cpp:5428`). | **Включены как дочерние сущности (`ChildOf`).** Родительская связь устанавливается в `LocomotionClass::Link_To_Object` и восстанавливается после swizzle в `LocomotionClass::Post_Load`; также должна обновляться при стековых переключениях `IPiggyback`. Обычные запросы по игровым объектам не задевают дочерние локомоторы без явного запроса. |
| `AnimClass` (`Anims`, 109 точек `new`) | Самый высокочастотный визуальный мусор в игре. | **Исключён (`✗`).** Если эффекту нужны данные, компонент вешается на владельца (`ObjectClass`) или на `AnimTypeClass`. |
| `TActionClass`, `TEventClass` | Двойное владение (глобальные кучи `Actions`/`Events` + связные списки `TriggerTypeClass`). | **Исключены (`✗`).** Логика триггеров расширяется через `TriggerClass` и `TriggerTypeClass`. |
| Высокочастотные боевые и физические листья (`BulletClass`, `ParticleClass`, `ParticleSystemClass`, `VoxelAnimClass`, `WaveClass`) | `BulletClass` несёт боевой урон и обратную связь, в отличие от `AnimClass`. | **Включены** с обязательным бенчмарком спавна/удаления 10k объектов за кадр перед закрытием волны C. |
| Классы типов (26 листьев `AbstractTypeClass`) | Прототипы из INI, живут на протяжении сценария (кроме `IsometricTileTypeClass`, переживающих `Clear_Scenario` при сохранении театра!). | **Включены (волна B).** Естественное место хранения новых INI-параметров фич. |

Сводка решений:

- **D1 (Охват):** утверждена таблица §8 (65 включено, 5 исключено, `Tactical` открыт). Скрипт `cmake/CheckEntityInventory.cmake` автоматически валидирует соответствие конструкторов и `code/flecs/register.cpp`.
- **D2 (Запрет gameplay-обзерверов):** глобальные `world.observer` запрещены; вся игровая диспетчеризация явная.
- **D3 (Хранение `EntityId`):** поле `EntityId Entity` (8 байт) лежит прямо в `AbstractClass` и `LocomotionClass`, обеспечивая $O(1)$ доступ без хеш-таблиц; никогда не сериализуется.
- **D4 (Первая фича — открыто):** вертикальный срез для вшивки хуков в цикл симуляции выбирается мейнтейнером.
- **D5 (`Tactical` — открыто):** решить до закрытия волны D.

---

## 3. Архитектура моста `code/flecs/`, идентификаторы и целостность на ветке

### 3.1 Пять существующих идентификаторов и шестой (`EntityId`)

| Идентификатор | Тип и владелец | Время жизни | Назначение |
| --- | --- | --- | --- |
| Индекс в куче (`Heap ID`) | `int`, позиция в `DynamicVectorClass<T*>` | Один прогон сценария | Итерация по кучам, `OBJECT_HEAP_LIST` (`code/objheaps.hh`) |
| Абстрактный ID | `int AbstractClass::ID` (`Scen->Get_Unique_ID()`) | Переживает сохранение/загрузку | Межобъектные ссылки, расчёт CRC |
| Тип RTTI | `RTTIType` (`code/rtti.hh`, 61 значение) | Константа времени компиляции | `What_Am_I()`, `Fetch_RTTI()`, каноническая сортировка |
| Идентификатор класса | 16-байтный `ClassID` (`code/classid.h`) | Сейвы и ключи `Locomotor=` в INI | Фабрика `Create_Object` (`code/classfactory.*`) |
| Swizzle ID | `SwizzleIDType` (`uint32_t`, `code/swizzle.h`) | Одна операция сохранения или загрузки | Перешивка сырых указателей после чтения `.SAV` |
| **Сущность Flecs (`EntityId`)** | `uint64_t` (`code/flecs/id.hh`), хранится в `AbstractClass::Entity` / `LocomotionClass::Entity` | **Только текущий процесс** | Ключ доступа к компонентам и отношениям в `FlecsWorld` |

`EntityId` содержит поколенческие биты Flecs и отличается не только между запусками, но и между двумя сетевыми клиентами в одной партии (например, если один игрок открывал UI-предпросмотр или создал локальный эффект). Поэтому **`EntityId` категорически запрещено записывать в `.SAV`, отправлять по сети или подмешивать в `CRCEngine`**.

### 3.2 Исправленные дефекты базовой привязки на ветке

Аудит дерева `code/` выявил пять критических мест, три из которых блокировали сборку или приводили к порче сущностей и уже исправлены на текущей ветке:

1. **Объявление `AbstractClass::Entity` и защита при копировании (`code/abstract.h`, `code/loco.h`):** в `AbstractClass` отсутствовало само поле `EntityId Entity;`, а неявный конструктор копирования копировал чужой `Entity` в новый экземпляр (что при вызове `Replace_Entity(Entity, this, ...)` в copy-конструкторе наследника вроде `TaskForceClass` уничтожало живую сущность исходного объекта!). Добавлены `EntityId Entity;` и явные конструкторы копирования с `Entity(0)` в `AbstractClass` и `LocomotionClass`.
2. **Заголовок в `code/abstract.cpp`:** добавлен `#include "flecs/world.h"` для `FlecsWorld.Destroy_Entity(Entity)` в `~AbstractClass()`.
3. **Родительская связь локомоторов (`code/loco.cpp`):** `FlecsWorld.Set_Entity_Parent(Entity, LinkedTo != nullptr ? LinkedTo->Entity : 0)` подключён как в `LocomotionClass::Link_To_Object`, так и в `LocomotionClass::Post_Load` (поскольку при загрузке сейва `LinkedTo` восстанавливается через `SwizzleManager` напрямую в память без вызова `Link_To_Object`).
4. **Мёртвые функции логики (`code/logic.cpp`):** `LogicClass_AI_Logic_507470()` (`code/logic.cpp:490`) и `LogicClass::Environment_AI()` (`code/logic.cpp:421`) не вызываются нигде в движке — единственной живой точкой симуляции является `LogicClass::AI()` (`code/logic.cpp:231`), вызываемая из `Main_Loop()` (`code/mainloop.cpp:325`).
5. **Неполнота `OBJECT_HEAP_LIST` для CRC (`code/saveload.cpp:327`):** макрос `OBJECT_HEAP_LIST` (`code/objheaps.hh:15`) обходит лишь 45 из 65 классов с сущностями и полностью пропускает 20 классов: `AlphaShapeClass`, `CampaignClass`, `CellClass`, `FoggedObjectClass`, `IsometricTileTypeClass`, `NeuronClass`, `OverlayClass`, `SmudgeClass`, `VeinholeMonsterClass`, `WaveClass` и все 10 подклассов `LocomotionClass` (`FootClass::Compute_CRC` не заглядывает в `Locomotion`). Следовательно, расчёт CRC для компонентов обязан идти централизованно через `FlecsWorld.Fold_CRCs(crc)` по каноническому ключу, а не полагаться на `OBJECT_HEAP_LIST`.

### 3.3 Файловая структура `code/flecs/`

```text
code/flecs/
  id.hh         — только тип EntityId (uint64_t); безопасно включается в abstract.h
                    и loco.h без единой зависимости и без статической инициализации.
  lifecycle.hh  — C++20-концепты, SimPhase, TickScope, ObjectStateInfo,
                    EntityContext, compile-time валидатор сигнатур
                    Validate_Component_Hooks<T>(), таблица ComponentOps и
                    потоковый адаптер ByteSaveStream.
  components.hh — служебные связки моста (LegacyObjectLink,
                    LegacyLocomotionLink, EntityBridgeMeta) и будущие
                    структуры компонентов новых механик.
  world.h       — класс FlecsWorldClass: привязка/удаление сущностей,
                    родительские связи, хранение компонентов и методы диспетчеризации
                    жизненного цикла. Не содержит заголовков flecs и Win32.
  world.cpp     — единственная единица трансляции с #include <flecs.h>.
  register.cpp  — FlecsWorldClass::Register_Tags(): явная регистрация тегов всех
                    65 включённых классов (проверяется CheckEntityInventory.cmake).
code/_flecs.cpp — глобальный объект constinit FlecsWorldClass FlecsWorld.
```

Особое свойство инициализации: конструктор `FlecsWorldClass` объявлен `constexpr noexcept`, а глобальный объект в `code/_flecs.cpp` помечен `constinit`. Внутренние контейнеры выделяются лениво при первом обращении (`Ensure_State()`, `Ensure_World()`), поэтому:
- глобальные объекты статической инициализации до `WinMain` (такие как `CellClass BlubCell`) безопасно создают мир и регистрируют сущности без риска перезаписи полей конструктором `_flecs.cpp` (Static Initialization Order Fiasco исключён на уровне стандарта C++20);
- локальные стековые экземпляры `FlecsWorldClass` в модульных тестах гарантированно начинают работу с нулевыми указателями;
- `on_remove`-хуки на `LegacyObjectLink` и `LegacyLocomotionLink` поддерживают точный счётчик `Live_Entity_Count()` даже при каскадном удалении дочерних `ChildOf`-сущностей вместе с родителем.

---

## 4. Полный жизненный цикл компонентов в движке OpenTS

### 4.1 Сводная таблица стадий жизненного цикла компонента

| Метод / стадия компонента | Аналог в современных движках | Когда вызывается в OpenTS | Что безопасно читать и изменять |
| --- | --- | --- | --- |
| Конструктор структуры `T()` | Godot `_init`, Unity ctor | При добавлении компонента в таблицу Flecs или пред-аллокации слота при загрузке `.SAV` | Только собственные поля по умолчанию. Указатели на мир, `Scen`, `Rule`, `House` и координаты ещё могут отсутствовать. |
| Чтение типа из INI (`Read_INI`) | Unreal `PostInitProperties` / DataAsset | Цепочка `TypeClass::Read_INI` (`RulesClass::Initialize`, `RulesClass::Addition`, `.INI` карты и `MISSION.INI`) | Ключи `CCINIClass` секции своего типа. Обязана быть идемпотентной при повторных аддитивных проходах чтения INI (`code/scenario.cpp:1861,1885,2103,2142`). |
| `On_Ready(ctx)` | Godot `_ready`, Unity `Start`, Unreal `BeginPlay` | В живой игре (`!Is_Scenario_Loading()` и вне `defer`) — сразу при первом добавлении компонента; при загрузке карты — пакетно в `Flush_Scenario_Ready()` после `Read_Scenario_INI`; при мутации внутри тика — сразу после `defer_end()` | Полностью готовое начальное состояние мира, финальные оверрайды правил миссии, разрешённые `Swizzler`-ссылки триггеров и команд. Выполняется не более одного раза за жизнь компонента и **подавляется при чтении `.SAV`**. |
| `On_Enter_World(ctx)` | Godot `_enter_tree`, Unity `OnEnable` | Хвост листового `Unlimbo()` при переходе `IsInLimbo` из `true` в `false` (в порядке регистрации компонентов) | Валидные мировые координаты `PositionCoord`, клетка карты, `House`, `Class_Of()`, членство в слоях `Map` и `Logic`. Может вызываться многократно (высадка из транспорта / госпиталя / Carryall). |
| `On_Exit_World(ctx)` | Godot `_exit_tree`, Unity `OnDisable` | Начало `ObjectClass::Limbo()` при `GameActive && !IsInLimbo` до снятия с карты и удаления из `Logic` (в **обратном** порядке регистрации) | Последние валидные мировые координаты, клетка и дом владельца. Временное убирание в трюм (`IsActive == true`) отличается от предсмертного ухода (`IsActive == false` после `Delete_Me()`). |
| `Process(ctx)` | Godot `_physics_process`, Unity `FixedUpdate`, Unreal `TickComponent` | Детерминированный тик внутри `LogicClass::AI()` (`SimPhase::PostObjects` или `SimPhase::PostHouses`), строго 1 раз за симуляционный `Frame` | Полное состояние симуляции, общий ГПСЧ `Scen->RandomNumber`, нанесение урона, приказы, спавн и отложенное удаление. Никогда не крутится внутри `Sync_Delay()`. |
| `Render_Foreground(ctx, surface) const` | Godot `_draw` / `_process`, Unity `LateUpdate` | Визуальный проход переднего плана в `Tactical::Render()` (`code/tactical.cpp`) от 0 до N раз за симуляционный кадр | Только поверхности отрисовки, локальная интерполяция, HUD и локальный звук. **Квалифицирован `const` на уровне C++20-концепта**: любые мутации состояния симуляции или вызовы `Scen->RandomNumber` запрещены. |
| `Detach(ctx, target, all)` | Сброс слабых ссылок / `EndPlay` цели | Внутри `Detach_This_From_All(target, all)` (`code/tracker.cpp`) | Очистка сохранённых указателей `AbstractClass const *` на уходящий/скрывающийся `target` с учётом флага `all` (`false` — маскировка/потеря захвата, `true` — уход в limbo или смерть). Разрешает самоудаление компонента. |
| `Serialize(stream)` | Сохранение / загрузка полей | Внутри `Serialize_Entity(entity, stream)` через `SaveStreamClass` | Скалярные поля и контейнеры компонента в фиксированном порядке. Передача сырых адресов полей внутри таблиц Flecs в `SwizzleManager` запрещена (§4.3); `EntityId` никогда не пишется. |
| `Post_Load_Resolve(ctx)` | Unreal `PostLoad` | В `Post_Load_Game()` (`code/scenario.cpp:1019`) строго **после** `Swizzler.Resolve()` (`code/saveload.cpp:1189`) | Все восстановленные и перешитые указатели C++-объектов, типы, дома, клетки и локомоторы. Восстанавливает производные кэши и объектные ссылки по сохранённым ID без сброса таймеров. |
| `Compute_CRC(ctx, crc) const` | Контрольная сумма lockstep-синхронизации | В `Object_CRCs()` (`code/saveload.cpp:327`) перед подмешиванием номера `Frame` | Детерминированная свёртка всех влияющих на симуляцию полей в `CRCEngine` в каноническом порядке по всем 65 классам. |
| `On_Remove(ctx)` | Unity `OnDestroy` | Автоматически через `ecs_type_hooks_t::on_remove` при `Remove_Component`, `Destroy_Entity`, каскадном удалении родителя, `Reset()` и `Shutdown()` | Освобождение собственных ресурсов компонента. Обязана допускать частично загруженные объекты после ошибки чтения сейва и нулевые глобальные указатели `Rule`/`Scen`/`TacticalMap` на выходе из процесса. |

---

### 4.2 Почему `On_Ready` нельзя выполнять в конструкторе класса и как устроена загрузка сценария

Пять реальных путей создания объектов делают конструктор C++-класса непригодным для логики `On_Ready`:

1. **Статическая инициализация до `WinMain`:** глобальный `CellClass BlubCell` конструируется до создания `Rule`, `Scen` и `TacticalMap`.
2. **Placement `new` клеток карты:** `MapClass::Init_Cells()` пересоздаёт слоты `CellClass` поверх старой памяти ещё до чтения карты `Map.Read_INI`.
3. **Пустые «оболочки» при загрузке сейва:** фабрика `Register_Class<T>` (`code/classfactory.h:26`) вызывает конструктор по умолчанию с нулевыми `Class == NULL` и `House == NULL` до вызова `Load(stream)`.
4. **Создание в состоянии `IsInLimbo == true`:** `Create_One_Of()` создаёт юнитов с координатами `(0,0,0)` вне слоёв `Map` и `Logic`; объект может остаться в транспорте/подкреплении или быть немедленно удалён, если `Unlimbo()` не найдёт свободную клетку.
5. **Непривязанный локомотор:** `Create_Locomotor(ClassID)` (`code/loco.cpp:184`) конструирует локомотор с `LinkedTo == NULL` до вызова `Link_To_Object(this)`.

Построчная трассировка `Read_Scenario_INI()` (`code/scenario.cpp:1775–2210`) показывает, почему даже в момент `Unlimbo()` на старте миссии данные мира ещё не финальны:

```text
Read_Scenario_INI (code/scenario.cpp)
  1777  ScenarioInit++                     → FlecsWorld.Begin_Scenario_Load()
  1780  Clear_Scenario()                   — очищает кучи (сохраняя IsometricTileTypes!)
  1790  Пересоздание TacticalMap; Init_Theater()
  1861  Rule->Initialize(*RuleINI)         — создаёт базовые Type-объекты из rules.ini
  1885  Rule->Addition(ini)                — применяет правила из файла карты
  1903  HouseClass::Read_All(ini)          — создаёт дома кампании
  1932  Assign_Start_Positions / Read_Spawn_Houses (скирмиш / мультиплеер)
  1938  TeamType, ScriptType, TaskForce, TriggerType, TagType, AITriggerType::Read_All
  1986  Map.Read_INI(ini)                  — placement new клеток и изометрических тайлов
  1995  TubeClass::Read_INI, OverlayClass::Read_INI, TerrainClass::Read_INI
  2048  UnitClass::Read_INI(ini)           — создаёт И вызывает Unlimbo() у юнитов!
  2052  AircraftClass::Read_INI(ini)       — создаёт И вызывает Unlimbo() у авиации!
  2058  InfantryClass::Read_INI(ini)       — создаёт И вызывает Unlimbo() у пехоты!
  2068  BuildingClass::Read_INI(ini)       — создаёт И вызывает Unlimbo() у зданий!
  2080  SmudgeClass::Read_INI(ini)         — создаёт кратеры/пятна
  2103  Rule->Addition(mini)               — ОВЕРРАЙДЫ ПРАВИЛ КАМПАНИИ (MISSION.INI) ПОСЛЕ Unlimbo!
  2142  Rule->Addition(mini)               — ОВЕРРАЙДЫ РЕЖИМА СКИРМИША ПОСЛЕ Unlimbo!
  2154  Map.Overpass()                     — пересчёт тиберия и атрибутов клеток
  2163  Multiplayer_Last_Minute_Fixups()   — спавн стартовых MCV/отрядов, удаление лишних объектов
  2167  Swizzler.Resolve()                 — привязка указателей Tag к размещённым объектам!
  2172  Process_Deferred_Deletion()        — физическое удаление отбракованных объектов
  2179  ScenarioInit--                     → FlecsWorld.Flush_Scenario_Ready()
```

Следствие: во время `Begin_Scenario_Load()` все новые компоненты складываются в `PendingReady`, а вызов `FlecsWorld.Flush_Scenario_Ready()` на строке 2180 отфильтровывает удалённые на этапе фиксов сущности, сортирует живые записи по `(ComponentIndex, Kind, FetchId, AttachSequence)` и выполняет `On_Ready` уже поверх полностью готовых правил, домов, клеток и разрешённых триггеров. Важная деталь `Clear_Scenario()` (`code/scenario.cpp:1077–1085`): экземпляры `IsometricTileTypeClass` временно вынимаются из `AbstractTypes` перед `Delete_All_Objects()` и возвращаются обратно, поэтому их сущности штатно переживают смену миссии одного театра.

---

### 4.3 Конвейер сохранения/загрузки и ловушки релокации памяти

Трассировка `Load_Game()` (`code/saveload.cpp:1125–1215`):

```text
Load_Game (code/saveload.cpp)
  1163  Get_All(stream, false)
          746  Clear_Scenario()
          796  Load_Vector<AnimTypeClass>(stream)
          800  Map.Load(stream)            — клетки, оверлеи и пятна грузятся ДО своих Type-классов!
          811  Logic.Load(stream)
          820  Load_Vector<UnitClass>      — юниты грузятся ДО UnitTypes (строка 823)!
                 └─ Load_Object() (строка 211):
                      1. Create_Object(classid) → пустая оболочка + Attach_Entity
                      2. persist->Load(stream)  → в полях-указателях лежат сырые SwizzleID!
                      3. persist->Post_Load()   → УКАЗАТЕЛИ ВСЁ ЕЩЁ НЕ РАЗРЕШЕНЫ!
          823  Load_Vector<UnitTypeClass>
          ...  остальные 40+ куч ...
  1189  Swizzler.Resolve()                 — превращает все SwizzleID в живые адреса C++
  1195  Post_Load_Game()                   — code/scenario.cpp:1019: ВСЕ УКАЗАТЕЛИ ВАЛИДНЫ
                                             → FlecsWorld.Post_Load_Resolve_All()
```

Три обязательных правила для сохранений компонентов:

1. **Бинарный формат с предварительным каталогом (`Serialize_Entity`):** при сохранении сущности мост записывает количество сериализуемых компонентов, затем таблицу заголовков `(длина_имени, имя, размер_полезных_данных)`, а следом — полезные данные каждого компонента с обратной простановкой фактической длины через `SaveStreamClass::Overwrite_Bytes`. Это работает строго однопроходно вперёд (совместимо с `SaveStreamClass` без `Seek`), позволяет безопасно пропускать удалённые в новых версиях компоненты по их длине, оставляет значения по умолчанию для новых компонентов на старых сейвах и отклоняет обрезанные/битые потоки до выхода за границы буфера.
2. **Пред-аллокация финального архетипа и запрет прямого `SwizzleManager` по внутренним полям таблицы:**
   - При загрузке сущности `Serialize_Entity` сначала читает весь каталог компонентов и сразу добавляет все известные компоненты на `entity` до чтения первого байта полезных данных, чтобы внутри одной сущности переходы между архетипами не двигали память уже прочитанных компонентов.
   - При этом добавление *следующих* сущностей в ту же таблицу Flecs может вызвать рост буфера колонки (`realloc`) и сдвинуть адреса компонентов предыдущих сущностей. Поскольку `SwizzleManager` запоминает точный адрес слота указателя в памяти, **компонентам, хранящимся по значению в таблицах Flecs, запрещено передавать `&this->Target` напрямую в `SwizzleManager`**: ссылки на объекты сохраняются как целочисленные `ID` (или внутри выделенного в куче блока `std::unique_ptr`) и разрешаются в указатели внутри `Post_Load_Resolve(ctx)`.
3. **Подавление `On_Ready` при загрузке:** во время `Serialize_Entity` в режиме `MODE_LOAD` флаг `InSaveLoad` блокирует постановку `On_Ready` в очередь, чтобы восстановленные из сейва счётчики и таймеры не перезаписывались стартовой инициализацией.

---

### 4.4 Вход и выход из мира (`Unlimbo` / `Limbo`), транспорт (`CargoClass`) и туннели (`TubeClass`)

- **Асимметрия `Unlimbo()` и `Limbo()`:**
  - `Unlimbo()` во всех классах выполняется **сверху вниз** (`ObjectClass::Unlimbo` → `TechnoClass::Unlimbo` → `FootClass::Unlimbo` → тело листового класса, где выставляются бибы зданий, башни, анимации и мощности). Поэтому `FlecsWorld.Notify_Enter_World(Entity)` вызывается **в самом конце листового `Unlimbo()`** при возврате `true`.
  - `Limbo()` в разных листьях вызывает `BASECLASS::Limbo()` в разном порядке: `BuildingClass::Limbo` (`building.cpp:3478`), `InfantryClass::Limbo` (`infantry.cpp:2440`) и `TerrainClass::Limbo` (`terrain.cpp:758`) чистят себя *до* базы, тогда как `UnitClass::Limbo` (`unit.cpp:5965`) и `BuildingLightClass::Limbo` (`blight.cpp:479`) вызывают `if (BASECLASS::Limbo())` **первой строкой** — после чего `PositionCoord`, клетка, выбор игрока и членство в `Logic` уже стёрты. Единственная чистая и универсальная точка для `FlecsWorld.Notify_Exit_World(Entity)` — самое начало `ObjectClass::Limbo()` (`code/object.cpp:1318`) внутри `if (GameActive && !IsInLimbo)` прямо перед `Unselect()`, `Detach_All()`, `Map.Remove(this)` и `Logic.Remove(this)`.
- **Транспорт, Carryall и госпитали (`CargoClass`):** погрузка в `CargoClass::Attach` (`code/cargo.cpp:200`) переводит пассажира в `Limbo()` (`IsSubmittedToLayer = false`), а метод `CargoClass::AI()` (`code/cargo.h:52`) пуст (`{}`). Компоненты с `TickScope::WorldActive` автоматически встают на паузу в трюме и возобновляют тики после высадки (включая дочерние локомоторы, наследующие `IsInLimbo` родителя), а компоненты с `TickScope::Always` (например, перезарядка способности в трюме) продолжают тикать, пока `IsActive == true`.
- **Подземные туннели (`TubeClass`):** юниты и пехота в туннеле (`CurrentTube >= 0`) остаются в `Logic`, но `UnitClass::AI()` (`code/unit.cpp:480–484`) и `InfantryClass::AI()` (`code/infantry.cpp:1387–1391`) делают ранний `Tunnel_AI(); Update_Radar_Position(); return;`, полностью обходя `FootClass::AI()`, `TechnoClass::AI()` и `ObjectClass::AI()`. Централизованный вызов `FlecsWorld.Run_Sim_Phase` из `LogicClass::AI()` не страдает от этого обхода.

---

### 4.5 Покадровый конвейер `Main_Loop()`, покрытие тиков и рендер

Полная последовательность одного кадра симуляции `Frame` в `Main_Loop()` (`code/mainloop.cpp:225–430`):

```text
Main_Loop() — кадр Frame (code/mainloop.cpp)
  1. Check_For_Focus_Loss(), Session.Trap_Object(), взвод FramePacer
  2. Локальный ввод и пред-рендер (при !Session.Play, SpecialDialog == SDLG_NONE, GameInFocus):
       UIShell.Tick() → Map.Input() → Keyboard_Process() → Update_Fogged_Objects() → Map.Render()
  3. Do_Record_Playback()
  4. DisplayClass::Layer[LAYER_GROUND].Sort()
  5. Logic.AI() (code/logic.cpp:231) — ДЕТЕРМИНИРОВАННЫЙ ТИК СИМУЛЯЦИИ:
       5.1  Проверки триггеров LogicTags + сброс флагов статуса сценария
       5.2  Нарастание пелены/тумана, шаг освещения, рост льда
       5.3  VeinholeMonsterClass::Update_All()
       5.4  TiberiumClass::Tiberium_Growth() и Tiberium_Spread()
       5.5  Map.Ice_Solidification_AI()
       5.6  TeamClass::AI() по снимку массива Teams
       5.7  SpotLight, LaserDraw, IonStorm::AI, LightSource::Process_Lighting(6),
            EMPulse::Update_All, Map.Terrain_Deformation_AI
       5.8  Объекты слоя Logic: for (index = 0; index < Count(); index++) (*this)[index]->AI()
       ───► [Точка #6а] FlecsWorld.Run_Sim_Phase(SimPhase::PostObjects)
       5.9  MoveFlashes[index]->AI() (только мультиплеер)
       5.10 IonBlastClass::Update_All(), AlphaShapeClass::Update_All()
       5.11 Map.Logic(), TacticalMap->AI()
       5.12 Factories[index]->AI()
       5.13 Houses[house]->AI() (энергия, радар, спавн команд, супероружие, поражение, HouseTags)
       5.14 Обновление камеры слежения за объектом
       ───► [Точка #6б] FlecsWorld.Run_Sim_Phase(SimPhase::PostHouses)
  6. Session.Messages.Manage()
  7. Queue_AI() (code/queue.cpp:502) — СЕТЕВОЙ LOCKSTEP, ОБМЕН CRC И КОМАНДЫ ИГРОКОВ:
       Сверка Object_CRCs() [Точка #8: FlecsWorld.Fold_CRCs(crc)] → OutList в DoList → EventClass::Execute()
  8. Call_Back() + проверка победы / поражения / рестарта / выхода
  9. Frame++
 10. Sync_Delay() (code/mainloop.cpp:498–535) — ОЖИДАНИЕ ТАЙМЕРА КАДРА (0..N ИТЕРАЦИЙ!):
       Пока FrameTimer > 10: Call_Back() → Map.Input() → Keyboard_Process() → TacticalMap->AI() → Map.Render()
 11. Process_Deferred_Deletion() (code/tracker.cpp:216):
       Физический delete объектов из ObjectsToDelete → деструкторы и Destroy_Entity
 12. SaveManager.Service() (чистое окно для автосохранения после физической очистки)
```

Почему тик компонентов обязан идти через `FlecsWorld.Run_Sim_Phase`, а не из виртуальных `ObjectClass::AI()`:
- Только **10 из 65** классов с сущностями вообще попадают в цикл 5.8 (`Logic`). Ещё 4 обновляются статическими пакетными функциями (причём `LightSourceClass::Process_Lighting(6)` шагает с шагом 6!), 3 крутятся в менеджерах (`Teams`, `Factories`, `Houses`), 4 тикают условно от владельца, 10 — активные внешние локомоторы, а оставшиеся **34 класса** (включая 262 144 `CellClass`, `OverlayClass`, `SmudgeClass`, `TriggerClass`, `TagClass`, `WaypointPathClass` и все 26 `Type`-классов) **вообще не имеют покадрового цикла `AI()`**.
- При этом сканировать все 262 144 клетки каждый кадр недопустимо по производительности: кэшированный `ecs_query_t *` в `Run_Sim_Phase` обходит **только таблицы архетипов, реально содержащие данный тикающий компонент** (0 наносекунд на пассивные клетки и пассивные маркеры).
- Внутри `Run_Sim_Phase` активные записи сортируются по каноническому ключу `(Kind, FetchId, AttachSequence)` и выполняются внутри `world->defer_begin()` / `world->defer_end()`. Если один компонент внутри `Process()` убивает другую сущность (`Destroy_Entity` или `Delete_Me()` → `IsActive = false`), повторная проверка живости перед вызовом `Process` гарантирует, что убитая в этом же тике сущность не выполнит «зомби-тик».

### 4.6 Отцепление целей (`Detach`), рендер, сетевой ввод и разрушение

1. **Ловушка ранних `return` в `Detach_This_From_All` (`code/tracker.cpp:117–159`):** функция содержит четыре ранних `return;` в ветках `RTTI_HOUSE` (строка 124), `RTTI_ANIM` (строка 134), `As_ObjectClass()` (строка 148) и `As_AbstractTypeClass()` (строка 158). При подключении точки #7 эти ветки необходимо свести в единый `if / else if`, чтобы `FlecsWorld.Detach_Target(target, all)` в конце функции гарантированно вызывался для каждого типа объектов. Если ни один зарегистрированный компонент не определяет метод `Detach`, вызов завершается за одну проверку булева флага `HasAnyDetach`.
2. **Двухконтурный рендер (`code/tactical.cpp:1244–1315`):**
   - Фоновый кэш тайлов (`TileSurface`, `DRAW_PASS_BACKGROUND`): рисует тайлы, оверлеи, деревья и здания **только внутри грязных областей** (`CellRedraw`, `DirtyAreas`, полосы прокрутки камеры). Компонент, меняющий визуальный вид здания, клетки или оверлея, обязан пометить объект через `obj->Mark(MARK_CHANGE)` или `TacticalMap->Register_Dirty_Area(...)`, иначе на экране останутся закэшированные пиксели.
   - Динамический передний план (`CompositeSurface`, `DRAW_PASS_FOREGROUND`): точка вызова `FlecsWorld.Render_Foreground(&TacticalRect)` после `Draw_Objects(true)`. Выполняется вне lockstep-синхронизации (в том числе многократно внутри `Sync_Delay()`), поэтому метод `Render_Foreground` обязан быть `const` и использовать только `NonCriticalRandomNumber` для визуального шума.
3. **Пользовательский ввод и команды:** обработчики UI и мыши никогда не меняют компоненты напрямую — любое игровое действие игрока оформляется пакетом `EventClass` в `OutList` с идентификацией объекта через `TargetClass` / `ID` (никогда не `EntityId`!) и применяется синхронно у всех клиентов внутри `EventClass::Execute()` (`code/event.cpp:701`).
4. **Порядок разрушения и выход из игры (`Prog_End`, `code/startup.cpp:968–1005`):**
   - При обычном `delete unit` сначала отрабатывает `~UnitClass()`, затем `~FootClass()` (разрушая `Locomotion` и дочернюю сущность локомотора), и лишь в самом конце `~AbstractClass()` вызывает `FlecsWorld.Destroy_Entity(Entity)`, когда производная часть C++-объекта уже разрушена. Поэтому в `On_Remove(ctx)` запрещено кастовать `ctx.Owner` к производному классу с вызовом виртуальных функций.
   - В `Prog_End()` движок удаляет `TacticalMap` (строка 969), `Rule` (строка 993) и `Scen` (строка 998) **до** вызова `FlecsWorld.Shutdown()` (строка 1001), а `Emergency_Exit()` заходит туда напрямую без `Clear_Scenario()`. Любая очистка в `On_Remove` обязана проверять глобальные указатели на `nullptr`, а при вшивке точки #9 `Clear_Scenario()` / очистку мира следует вызывать до удаления `Rule` и `Scen`.

---

## 5. Сравнение моделей диспетчеризации и выбранный Вариант B (`ComponentOps`)

| Критерий | Вариант A: Свободные функции на каждую фичу | **Вариант B: Таблицы `ComponentOps` на C++20-концептах (выбран)** | Вариант C: Полиморфные `ComponentClass` с `virtual` | Вариант D: Нативные `world.progress()` и `observer` Flecs |
| --- | --- | --- | --- | --- |
| Удобство `Ready` / `Process` для автора фичи | Низкое (ручная разводка функций) | **Высокое** (обычные методы на plain `struct`) | Высокое (`virtual`-методы) | Средне-высокое (лямбды и обзерверы) |
| Правки ядра движка при добавлении нового компонента | 6–8 файлов каждый раз | **Ноль правок** после однократной вшивки 9 точек | Ноль правок после вшивки | Ноль правок при глобальном открытии `flecs.h` |
| Защита lockstep и CRC от рассинхронов | Вручную (легко забыть `Compute_CRC` или `Detach`) | **Compile-time проверка сигнатур** + каноническая сортировка | Ручная дисциплина | Хрупко при фрагментации таблиц локальными UI-тегами |
| Накладные расходы на 262k пассивных клеток | Ноль | **Ноль** | 24 байта `std::vector` на объект + аллокации в куче | Ноль |
| Безопасность мутаций посреди цикла | Явная | **Явная** (`defer_begin` / `defer_end` + очередь `PendingReady`) | Риск инвалидации вектора при реентерабельности | Скрытая синхронная реентерабельность `OnSet` |
| Изоляция `<flecs.h>` внутри `code/flecs/world.cpp` | Да | **Да (100 %)** | Да | Нет (разносит заголовки Flecs по всему проекту) |

### Пример объявления компонента в Варианте B

```cpp
struct RegenerationShieldComponent {
	static constexpr SimPhase Phase = SimPhase::PostObjects;
	static constexpr TickScope Scope = TickScope::WorldActive;

	int CurrentShield = 0;
	int MaxShield = 0;
	int RechargeTimer = 0;
	int LastAttackerSavedId = -1;
	AbstractClass const * LastAttacker = nullptr;

	void On_Ready(EntityContext const & context);
	void Process(EntityContext const & context);
	void Detach(EntityContext const & context, AbstractClass const * target, bool all);
	void Compute_CRC(EntityContext const & context, CRCEngine & crc) const;
	void Serialize(SaveStreamClass & stream);
	void Post_Load_Resolve(EntityContext const & context);
};
```

Все методы и статические поля опциональны: простая структура данных `struct ArmorModifier { int FlatBonus = 0; };` не требует ни единого метода, но если автор объявит любой из 10 хуков с неверной константностью или неверными типами параметров, `detail::Validate_Component_Hooks<T>()` в `code/flecs/lifecycle.hh` остановит компиляцию с точным сообщением об ошибке.

---

## 6. Девять точек интеграции с движком и статус вшивки

| # | Статус | Файл и функция | Точное место вставки | Вызов моста |
| --- | --- | --- | --- | --- |
| 1 | **Готово** | `code/abstract.h`, `code/abstract.cpp`, `code/loco.h`, `code/loco.cpp`, `code/_flecs.cpp` | Поле `Entity` и copy-конструкторы; `~AbstractClass()` / `~LocomotionClass()`; `LocomotionClass::Link_To_Object` и `LocomotionClass::Post_Load`; `constinit FlecsWorld` | `Replace_Entity`, `Set_Entity_Parent`, `Destroy_Entity` |
| 2 | Ожидает | Хвосты листовых `Unlimbo()` (`unit.cpp`, `infantry.cpp`, `building.cpp`, `aircraft.cpp`, `bullet.cpp`, `terrain.cpp`, `blight.cpp`, `partsys.cpp`, `vanim.cpp`, `wave.cpp`) и стековые переключения `IPiggyback` | Сразу перед `return(true)` после полной инициализации листа; обновление `Set_Entity_Parent` при смене `IPiggyback` | `FlecsWorld.Notify_Enter_World(Entity)` |
| 3 | Ожидает | `code/object.cpp` — `ObjectClass::Limbo()` (`строка 1318`) | Внутри `if (GameActive && !IsInLimbo)` строго до `Unselect()` и `Detach_All()` | `FlecsWorld.Notify_Exit_World(Entity)` |
| 4 | Ожидает | `code/scenario.cpp` — `Read_Scenario_INI()` (`строки 1777` и `2180`) | Вход при `ScenarioInit++` и сброс очереди сразу после `ScenarioInit--` | `FlecsWorld.Begin_Scenario_Load()` и `FlecsWorld.Flush_Scenario_Ready()` |
| 5 | Ожидает | `code/abstract.cpp` (`Serialize`) и `code/scenario.cpp` — `Post_Load_Game()` (`строка 1020`) | Запись/чтение блока компонентов объекта (с обновлением `SAVE-FORMAT.md` и `tests/save`) и разрешение ссылок после `Swizzler.Resolve()` | `FlecsWorld.Serialize_Entity(Entity, stream)` и `FlecsWorld.Post_Load_Resolve_All()` |
| 6 | Ожидает | `code/logic.cpp` — `LogicClass::AI()` (`строки 376` и `415`) | После цикла объектов слоя `Logic` и после цикла `Houses[house]->AI()`, плюс регистрация `Set_Object_Inspector` | `FlecsWorld.Run_Sim_Phase(SimPhase::PostObjects)` и `FlecsWorld.Run_Sim_Phase(SimPhase::PostHouses)` |
| 7 | Ожидает | `code/tracker.cpp` — `Detach_This_From_All()` (`строка 207`) | Единый выход функции после замены четырёх ранних `return;` на `if / else if` | `FlecsWorld.Detach_Target(target, all)` |
| 8 | Ожидает | `code/saveload.cpp` — `Object_CRCs()` (`строка 340`) | Сразу после `OBJECT_HEAP_LIST(DO_OBJ_CRC)` и перед `crc((int)Frame)` | `FlecsWorld.Fold_CRCs(crc)` |
| 9 | Ожидает | `code/tactical.cpp` — `Tactical::Render()` (`строка 1295`) и `code/startup.cpp` — `Prog_End()` (`строка 967`) | После `Draw_Objects(true)` для переднего плана; очистка мира перед удалением `TacticalMap` / `Rule` / `Scen` при выходе | `FlecsWorld.Render_Foreground(&TacticalRect)` и `FlecsWorld.Shutdown()` |

---

## 7. Чеклист добавления нового компонента

Когда точки интеграции #2–#9 будут вшиты вместе с первой фичей, каждый новый компонент добавляется по единому регламенту в рамках PR самой фичи:

1. **Описать plain C++ структуру** в заголовке фичи или в `code/flecs/components.hh` с кратким комментарием о смысле данных и о том, что означает отсутствие компонента на сущности. Никакого дублирования существующих полей C++-классов.
2. **Зарегистрировать компонент** через `FlecsWorld.Register_Component<MyComponent>("MyComponent")` в явной точке инициализации (без глобальных статических регистраторов).
3. **Зафиксировать три границы совместимости:**
   - **Сохранения:** если состояние должно переживать save/load — реализовать `Serialize(SaveStreamClass &)` и при наличии объектных ссылок `Post_Load_Resolve(EntityContext const &)`, обновить [SAVE-FORMAT.md](SAVE-FORMAT.md) и покрыть round-trip в `tests/save`.
   - **Детерминизм и CRC:** если состояние влияет на симуляцию — реализовать `Compute_CRC(EntityContext const &, CRCEngine &) const`. Чисто визуальные компоненты не читаются из кода симуляции.
   - **Ссылочная целостность:** если компонент хранит `AbstractClass const *` — обязательно реализовать `Detach(EntityContext const &, AbstractClass const * target, bool all)`.
4. **Данные уровня типа:** читать новые ключи в `TypeClass::Read_INI` на сущность типа и копировать/вычислять начальные значения экземпляра при создании.
5. **Покрыть тестами в `tests/flecs`** (добавление/чтение/удаление, отсутствие компонента, `Serialize` + `Post_Load_Resolve`, свёртка `CRCEngine` при перемешанных архетипах) и оформить запись об изменении в `manual/changes/` для видимых игроку или моддеру возможностей.

---

## 8. Таблица раскатки (все 71 класс-кандидат)

Волны: **P** — пилотные классы · **B** — классы типов · **C** — эффекты объектов и клетки карты · **D** — остальные наследники `AbstractClass` · **E** — дочерние локомоторы · **✗** — исключён решением · **?** — решение открыто (§2.3). Колонка «В фабрике» указывает наличие регистрации в `RegisterClasses()`.

| Класс | Куча / хранение | В фабрике | Волна | Примечание |
| --- | --- | --- | --- | --- |
| `HouseClass` | `Houses` | да | P | Мало экземпляров, живут весь сценарий. |
| `UnitClass` | `Units`, `Technos` | да | P | Пилотный `Techno`; создаётся в `UnitTypeClass::Create_One_Of`. |
| `BuildingClass` | `Buildings`, `Technos` | да | P | Регистрируется также в списках заводов и ремонтных баз. |
| `InfantryClass` | `Infantry`, `Technos` | да | P | Ранний возврат в `AI()` при движении по `TubeClass`. |
| `AircraftClass` | `Aircraft`, `Technos` | да | P | Единственный лист, реализующий также `IFlyControl`. |
| `AITriggerTypeClass` | `AITriggerTypes` | да | B | |
| `AircraftTypeClass` | `AircraftTypes` | да | B | Тип для `AircraftClass`. |
| `AnimTypeClass` | `AnimTypes` | да | B | |
| `BuildingTypeClass` | `BuildingTypes` | да | B | |
| `BulletTypeClass` | `BulletTypes` | да | B | |
| `CampaignClass` | `Campaigns` | да | B | Прямой лист `AbstractTypeClass`; вне `OBJECT_HEAP_LIST`. |
| `HouseTypeClass` | `HouseTypes` | да | B | |
| `InfantryTypeClass` | `InfantryTypes` | да | B | |
| `IsometricTileTypeClass` | `IsometricTileTypes` | да | B | Переживает `Clear_Scenario()` при сохранении театра; вне `OBJECT_HEAP_LIST`. |
| `OverlayTypeClass` | `OverlayTypes` | да | B | |
| `ParticleSystemTypeClass` | `ParticleSystemTypes` | да | B | |
| `ParticleTypeClass` | `ParticleTypes` | да | B | |
| `ScriptTypeClass` | `ScriptTypes` | да | B | |
| `SideClass` | `Sides` | да | B | |
| `SmudgeTypeClass` | `SmudgeTypes` | да | B | |
| `SuperWeaponTypeClass` | `SuperWeaponTypes` | да | B | |
| `TagTypeClass` | `TagTypes` | да | B | |
| `TaskForceClass` | `TaskForces` | да | B | Два конструктора + конструктор копирования. |
| `TeamTypeClass` | `TeamTypes` | да | B | |
| `TerrainTypeClass` | `TerrainTypes` | да | B | |
| `TiberiumClass` | `Tiberiums` | да | B | Тип тиберия напрямую под `AbstractClass`. |
| `TriggerTypeClass` | `TriggerTypes` | да | B | Владеет списками `FirstEvent` / `FirstAction`. |
| `UnitTypeClass` | `UnitTypes` | да | B | |
| `VoxelAnimTypeClass` | `VoxelAnimTypes` | да | B | |
| `WarheadTypeClass` | `Warheads` | да | B | |
| `WeaponTypeClass` | `Weapons` | да | B | |
| `AnimClass` | `Anims` | да | ✗ | Исключён (§2.3): максимальная частота создания/удаления, чисто визуальный класс. |
| `BuildingLightClass` | `BuildingLights` | да | C | |
| `BulletClass` | `Bullets` | да | C | Включён (§2.3): боевой снаряд; под гейтом бенчмарка. |
| `IsometricTileClass` | `IsometricTiles` | **нет** | ✗ | Исключён (§2.3). Данные расширений — на `IsometricTileTypeClass`. |
| `OverlayClass` | `Overlays` | **нет** | C | Восстанавливается через клетки; вне `OBJECT_HEAP_LIST`. |
| `ParticleClass` | `Particles` | да | C | Высокая частота создания; под гейтом бенчмарка. |
| `ParticleSystemClass` | `ParticleSystems` | да | C | |
| `SmudgeClass` | `Smudges` | **нет** | C | Как `OverlayClass`; вне `OBJECT_HEAP_LIST`. |
| `TerrainClass` | `Terrains` | да | C | |
| `VeinholeMonsterClass` | список `Update_All` | **нет** | C | Вне `OBJECT_HEAP_LIST`. |
| `VoxelAnimClass` | `VoxelAnims` | да | C | Высокая частота создания; под гейтом бенчмарка. |
| `WaveClass` | `Waves` | да | C | Вне `OBJECT_HEAP_LIST`. |
| `AlphaShapeClass` | `AlphaShapes` | да | D | Два конструктора; вне `OBJECT_HEAP_LIST`. |
| `CellClass` | `MapClass::Array` (262 144) | да | C | Полная привязка вкл. `BlubCell`; placement `new`; замер памяти и сброса на выходе волны C. |
| `EMPulseClass` | `EMPulseClass::EMPulses` | да | D | Статическое поле-куча. |
| `FactoryClass` | `Factories` | да | D | Множественное наследование (`private StageClass`). |
| `FoggedObjectClass` | `FoggyObjects` | да | D | Пять конструкторов; вне `OBJECT_HEAP_LIST`. |
| `LightSourceClass` | `LightSources` | да | D | Тикает батчами `Process_Lighting(6)`. |
| `LogicClass` | глобальный `Logic` | **нет** | ✗ | Исключён (§2.3): драйвер кадра, не игровой объект. |
| `NeuronClass` | внутри `BrainClass` | да | D | Вне `OBJECT_HEAP_LIST`. |
| `ScriptClass` | `Scripts` | да | D | |
| `SuperClass` | `SuperWeapons` | да | D | Экземпляр супероружия дома. |
| `TActionClass` | `Actions` + списки триггеров | да | ✗ | Исключён (§2.3). |
| `TEventClass` | `Events` + списки триггеров | да | ✗ | Исключён (§2.3). |
| `Tactical` | глобальный `TacticalMap` | да | ? | Решение D5 открыто (§2.3). |
| `TagClass` | `Tags`, `MapTags`, `LogicTags` | да | D | Один объект состоит в нескольких списках, сущность одна. |
| `TeamClass` | `Teams` | да | D | |
| `TriggerClass` | `Triggers` | да | D | |
| `TubeClass` | `Tubes` | да | D | |
| `WaypointPathClass` | `WaypointPaths` | да | D | |
| `DriveLocomotionClass` | `FootClass::Locomotion` | да | E | Дочерняя сущность `ChildOf` (§2.3); вне `OBJECT_HEAP_LIST`. |
| `DropPodLocomotionClass` | `FootClass::Locomotion` | да | E | Зарегистрирован как `ClassID_BallisticLocomotion`. |
| `FlyLocomotionClass` | `FootClass::Locomotion` | да | E | |
| `HoverLocomotionClass` | `FootClass::Locomotion` | да | E | |
| `JumpjetLocomotionClass` | `FootClass::Locomotion` | да | E | |
| `LevitateLocomotionClass` | `FootClass::Locomotion` | да | E | |
| `MechLocomotionClass` | `FootClass::Locomotion` | да | E | |
| `TeleportLocomotionClass` | `FootClass::Locomotion` | да | E | |
| `TunnelLocomotionClass` | `FootClass::Locomotion` | да | E | |
| `WalkLocomotionClass` | `FootClass::Locomotion` | да | E | |

---

## 9. Риски и план верификации

| Риск | Где проявляется | Защита в архитектуре |
| --- | --- | --- |
| Попадание `EntityId` в `.SAV`, сетевой пакет или `CRCEngine` | Несовместимость сейвов и мгновенный сетевой десинк | Жёсткий запрет (§3.1), стресс-тест двух перемешанных миров с проверкой `CRCEngine` в `tests/flecs`. |
| Порядок обхода таблиц Flecs меняет результат ГПСЧ или CRC | Скрытый рассинхрон в мультиплеере | Каноническая сортировка по `(Kind, FetchId, AttachSequence)` в `Run_Sim_Phase`, `Flush_Scenario_Ready`, `Post_Load_Resolve_All` и `Fold_CRCs`. |
| Сдвиг адресов полей компонента при росте таблиц Flecs во время загрузки сейва | Запись по висячему адресу из `SwizzleManager` | Двухэтапная пред-аллокация архетипа в `Serialize_Entity` + правило разрешать объектные ссылки через ID внутри `Post_Load_Resolve`. |
| Пропуск конструктора или копирование чужого `EntityId` | Утечка/нулевая сущность или удаление живой сущности донора | Проверка `CheckEntityInventory.cmake`, зануление `Entity(0)` в copy-конструкторах `AbstractClass` и `LocomotionClass`. |
| Память и время сброса на 262 144 сущностях `CellClass` | Просадка загрузки карты или кадра | Обязательный замер на заполненной карте (`_bench` + `tests/flecs`) перед закрытием волны C. |
| Вызов `FlecsWorld.Shutdown()` после удаления `Rule`, `Scen`, `TacticalMap` | Разыменование `nullptr` в `On_Remove` при выходе | Запрет безусловного обращения к глобалам в `On_Remove` + перенос очистки сущностей до удаления `Rule`/`Scen` при вшивке точки #9. |

### Обязательная проверка перед завершением всей подсистемы

1. **Автономные тесты моста (`tests/flecs`):** сборка и прохождение Debug (`ASan + UBSan + LSan`) и Release, проверка `entityinventory` — **выполнено на Linux**.
2. **Целевая сборка Windows MSVC (`Win32` и `x64`, `Debug` и `Release`):** чистая конфигурация и сборка `Game.exe` / `GameD.exe` и прогон всего набора `ctest` (`tests/save`, `tests/desync`, `tests/syncrec`, `tests/spawner`, `tests/spawnhouse`) — **ожидает прогона на Windows / CI**.
3. **Игровая и нагрузочная проверка (после вшивки точек #2–#9 и выбора фичи D4):** прохождение миссии кампании, скирмиш-матча, цикла save → load → load-after-load с проверкой отсутствия утечек `Live_Entity_Count()`, замер 262k клеток в `_bench` и проверка идентичности CRC в сетевой партии.
