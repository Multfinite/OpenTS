---
id: report.software.2026-10-30-analysis-ec-flecs
title: "Разбор: внедрение статически типизированной EC-системы на flecs для ObjectClass, CellClass и LocomotionClass"
type: report
report_kind: analysis
domain: software
profile: software/analysis
status: review
authority: none
visibility: internal
date: "2026-10-30"
workflow: audit
decisions:
  - "P-0001"
cross_scope:
  - path: "meta/questions/software.md"
    why: "уточнение закрытого Q-SW-04 по индивидуальному Compute_CRC каждого компонента и актуализация Q-SW-06"
---

# Разбор: внедрение статически типизированной EC-системы на `flecs` для `ObjectClass`, `CellClass` и `LocomotionClass`

## Вопрос

Как встроить в `OpenTS` систему **Entity-Component (EC без ECS-систем)** поверх хранилища `flecs` (по одобренному владельцем **Варианту A**), где держателем компонентов `Entity` выступают три корня — **`ObjectClass`**, **`CellClass`** и **`LocomotionClass` (`ILocomotion`)**, поддерживаемые функции именуются в стиле `OpenTS` (`AI` = Pre-AI, `PostAI` и полный реестр точек врезки), сериализация пишется отдельным блоком в конец файла `.SAV`, а `Compute_CRC` работает в том числе **для каждого компонента в отдельности** без динамической типизации?

## Разбор

### 1. Прочитанные источники и границы проверки

Проверены исходные файлы репозитория:
- три ветки носителей `Entity`: [`code/object.h`](../../../code/object.h#L77), [`code/object.cpp`](../../../code/object.cpp#L639), [`code/techno.h`](../../../code/techno.h#L493), [`code/techno.cpp`](../../../code/techno.cpp#L3237), [`code/foot.h`](../../../code/foot.h#L53), [`code/foot.cpp`](../../../code/foot.cpp#L1008), [`code/cell.h`](../../../code/cell.h#L65), [`code/cell.cpp`](../../../code/cell.cpp#L1240), [`code/map.cpp`](../../../code/map.cpp#L161), [`code/iloco.h`](../../../code/iloco.h#L28), [`code/loco.h`](../../../code/loco.h#L35), [`code/loco.cpp`](../../../code/loco.cpp#L43), [`code/ipiggy.h`](../../../code/ipiggy.h#L18), [`code/startup.cpp`](../../../code/startup.cpp#L278);
- главный цикл и рендер: [`code/mainloop.cpp`](../../../code/mainloop.cpp#L235), [`code/logic.cpp`](../../../code/logic.cpp#L231), [`code/tactical.cpp`](../../../code/tactical.cpp#L1067), [`code/tracker.cpp`](../../../code/tracker.cpp#L216), [`code/scenario.cpp`](../../../code/scenario.cpp#L1019);
- сохранения, swizzle и диагностика рассинхронизации `Compute_CRC`: [`code/saveload.cpp`](../../../code/saveload.cpp#L327) (`Object_CRCs`, `Print_Heap_CRCs`, `Put_All`, `Get_All`), [`code/syncreport.h`](../../../code/syncreport.h#L1), [`code/savestream.h`](../../../code/savestream.h#L30), [`code/swizzle.h`](../../../code/swizzle.h#L18), [`code/swizzle.cpp`](../../../code/swizzle.cpp#L56), [`code/mouse.cpp`](../../../code/mouse.cpp#L398), [`code/queue.cpp`](../../../code/queue.cpp#L3710) (`Compute_Game_CRC`), [`docs/SAVE-FORMAT.md`](../../../docs/SAVE-FORMAT.md#L1).

Полная сборка `Game.exe` под MSVC в Linux-окружении не запускалась (`[И]` [`docs/BUILDING.md`](../../../docs/BUILDING.md#L10)); шаблонная диспетчеризация `TypedEntity<Root>` + `OwnerLink<Root>` + `ComponentRegistry<Ts...>` проверена компилятором `g++ 14.2.0 -std=c++20 -O2 -Wall -Wextra -Werror` `[В]`.

### 2. Архитектурное ядро (согласованные решения `Q-SW-01..05`)

1. **Три корня носителей `Entity` (`Q-SW-03`):**
   - `ObjectClass` ([`code/object.h:77`](../../../code/object.h#L77)) — поле `TypedEntity<ObjectClass> Components{this};` (покрывает все 21 класс ветки боевых и визуальных объектов);
   - `CellClass` ([`code/cell.h:65`](../../../code/cell.h#L65)) — поле `TypedEntity<CellClass> Components{this};` (с очисткой `reset()` перед `placement new` на [`code/map.cpp:701`](../../../code/map.cpp#L701), [`798`](../../../code/map.cpp#L798), [`950`](../../../code/map.cpp#L950) и переносом владения при сдвиге клеток на [`code/map.cpp:754`](../../../code/map.cpp#L754)–[`951`](../../../code/map.cpp#L951));
   - `LocomotionClass` ([`code/loco.h:35`](../../../code/loco.h#L35)) — поле `TypedEntity<LocomotionClass> Components{this};` (покрывает все 10 локомоторов из [`code/startup.cpp:278`](../../../code/startup.cpp#L278)–[`287`](../../../code/startup.cpp#L287)) + метод доступа `virtual TypedEntity<LocomotionClass> & Components() noexcept = 0;` в `ILocomotion` ([`code/iloco.h:28`](../../../code/iloco.h#L28)) для прямого доступа из `FootClass::Locomotion` без `dynamic_cast`.

2. **Статическая типизация и сигнатуры (`Q-SW-02`):**
   - При ленивой материализации сущности `TypedEntity<Root>` прикрепляет к ней в `flecs` служебный компонент `OwnerLink<Root> { Root* ptr; }`.
   - `ComponentRegistry<Head, Tail...>` на концептах C++20 через `if constexpr` поддерживает как общие сигнатуры (`Hook()`, `Hook(EntityView)`), так и типизированные по корню (`Hook(ObjectClass&)`, `Hook(CellClass&)`, `Hook(LocomotionClass&)`), а концепт `AllowedOnRoot<C, Root>` на этапе компиляции запрещает прикрепление компонента к чужому корню `[В]`.

3. **Автоматическое усыпление неактивных носителей (`Q-SW-05`):**
   - При уходе `ObjectClass` в `Limbo()` ([`code/object.cpp:1315`](../../../code/object.cpp#L1315)) или `Delete_Me()` ([`code/object.cpp:2435`](../../../code/object.cpp#L2435)) на его сущность (и сущность его `Locomotion`, если это `FootClass`) ставится встроенный тег `flecs::Disabled`; при `Unlimbo()` ([`code/object.cpp:1382`](../../../code/object.cpp#L1382)) тег снимается.
   - При помещении локомотора в `Piggybacker` через `Begin_Piggyback` (`Drive`/`Walk`/`DropPod`) на переносимый локомотор ставится `flecs::Disabled`, а при `End_Piggyback` — снимается. Табличные запросы `flecs` автоматически пропускают архетипы с `EcsDisabled` без `if` внутри цикла `[В]`.

### 3. Полный каталог точек врезки функций компонентов (`Q-SW-01`)

Все хуки генерируются рекурсивным `ComponentRegistry<Head, Tail...>`: если в типе `Head` метода нет, ветка `if constexpr` полностью отбрасывается компилятором (`0` инструкций).

#### Группа I. Глобальные табличные хуки (`GameComponents::<Hook>_All(...)`)

Выполняют прямой проход по плотным таблицам `flecs` за `O(число_активных_компонентов_с_хуком)`, охватывая все три корня (`ObjectClass`, `CellClass`, `LocomotionClass`):

| Код | Имя метода в компоненте | Точка врезки в движке (`файл:строка`) | Момент вызова и назначение |
| --- | --- | --- | --- |
| **G01** | **`AI`** *(Pre-AI)* | [`code/logic.cpp:359`](../../../code/logic.cpp#L359) (`LogicClass::AI`, **перед** циклом `for (...) obj->AI()`) | Основной тик компонентов **до** штатного `ObjectClass::AI()`: таймеры баффов/дебаффов, модификаторы характеристик на текущий кадр, клеточная логика до движения юнитов. |
| **G02** | **`PostAI`** | [`code/logic.cpp:378`](../../../code/logic.cpp#L378) (`LogicClass::AI`, **сразу после** цикла `obj->AI()`, до `Map.Logic()`) | Пост-обработка кадра **после** выполнения `ObjectClass::AI()` и вложенных `Locomotion->Process()`: реакция на итоговое перемещение, смену цели, нанесённый за тик урон. |
| **G03** | **`Environment_AI`** | [`code/logic.cpp:448`](../../../code/logic.cpp#L448) (`LogicClass::Environment_AI`, после прохода локомоторов) | Тик природных/средовых компонентов (лёд, туман/пелена, деформация рельефа, атмосферные поля на клетках). |
| **G04** | **`Frame_Begin`** | [`code/mainloop.cpp:299`](../../../code/mainloop.cpp#L299) (`Main_Loop`, до ввода и `Map.Render()`) | Сброс покадровых флагов/кэшей перед обработкой пользовательского ввода и кадра рендера. |
| **G05** | **`Frame_End`** | [`code/mainloop.cpp:422`](../../../code/mainloop.cpp#L422) (`Main_Loop`, после `Queue_AI()`, перед `Process_Deferred_Deletion()`) | Финализация кадра после отработки сетевых команд `Queue_AI()` и непосредственно перед физическим `delete` отложенных объектов. |
| **G06** | **`Pre_Render`** | [`code/mainloop.cpp:313`](../../../code/mainloop.cpp#L313) / [`code/tactical.cpp:1067`](../../../code/tactical.cpp#L1067) (перед `Tactical::Render`) | Подготовка чисто визуальных данных компонентов (интерполяция, подсветка, экранные координаты) только в рендер-кадрах. |
| **G07** | **`Post_Render`** | [`code/tactical.cpp:1067`](../../../code/tactical.cpp#L1067) (в конце `Tactical::Render`) | Глобальная отрисовка индикаторов/эффектов компонентов поверх тактической сцены. |
| **G08** | **`Scenario_Init`** | [`code/scenario.cpp`](../../../code/scenario.cpp#L1059) (после чтения INI и расстановки объектов сценария) | Связывание и первичная инициализация компонентов после полной загрузки карты миссии. |
| **G09** | **`Scenario_Clear`** | [`code/scenario.cpp:1059`](../../../code/scenario.cpp#L1059) (`Clear_Scenario`, перед пересозданием `flecs::world`) | Статический хук `static void Scenario_Clear()` для очистки разделяемых кэшей уровня типа компонента. |
| **G10** | **`Serialize`** | [`code/saveload.cpp:735`](../../../code/saveload.cpp#L735) (`Put_All`) и [`code/saveload.cpp:985`](../../../code/saveload.cpp#L985) (`Get_All`) | Запись и чтение состояния каждого компонента в хвостовой секции в конце `.SAV` через `SaveStreamClass`. |
| **G11** | **`Post_Load`** | [`code/scenario.cpp:1019`](../../../code/scenario.cpp#L1019) (`Post_Load_Game`, вызывается из [`code/saveload.cpp:1179`](../../../code/saveload.cpp#L1179) после `Swizzler.Resolve()`) | Восстановление кэшей, графических указателей и производных таблиц после разрешения всех swizzle-ссылок. |
| **G12** | **`Compute_CRC`** *(табличный и покомпонентный отчёт)* | [`code/saveload.cpp:341`](../../../code/saveload.cpp#L341) (`Object_CRCs`), [`code/saveload.cpp:388`](../../../code/saveload.cpp#L388) (`Print_Heap_CRCs`) и [`code/queue.cpp:3770`](../../../code/queue.cpp#L3770) (`Compute_Game_CRC`) | Сводный и **индивидуальный по каждому типу и каждому экземпляру компонента** расчёт `CRC` (подробно в разделе 5). |

#### Группа II. Точечные хуки конкретного носителя и отдельного компонента (`Components.<Hook>(...)`)

Вызываются точечно на конкретном экземпляре `ObjectClass`, `CellClass` или `LocomotionClass` (для пустых сущностей с `id_ == 0` проверка стоит одно сравнение `id_ != 0`):

| Код | Имя метода в компоненте | Корни | Точка врезки (`файл:строка`) | Событие и возможности перехвата |
| --- | --- | --- | --- | --- |
| **E01** | **`On_Attach` / `On_Remove`** | Все 3 | `code/ec/entity.h` (`add<C>`, `remove<C>`, `reset`, `~TypedEntity`) | Конструктивный хук при прикреплении компонента к живому носителю и очистка перед снятием/уничтожением. |
| **E02** | **`Detach`** | Все 3 | [`code/object.cpp:1470`](../../../code/object.cpp#L1470), [`code/cell.cpp:5206`](../../../code/cell.cpp#L5206), `FootClass::Detach` (для `Locomotion`) | Обнуление указателей `AbstractClass*` внутри компонентов при удалении цели из игры (`Detach(AbstractClass const* target, bool all)`). |
| **E03** | **`Limbo` / `Unlimbo`** | `Object`, `Loco` | [`code/object.cpp:1315`](../../../code/object.cpp#L1315), [`code/object.cpp:1382`](../../../code/object.cpp#L1382), [`code/loco.cpp:423`](../../../code/loco.cpp#L423) | Снятие объекта с карты (в транспорт/резерв) и выход на карту; синхронное переключение `flecs::Disabled`. |
| **E04** | **`Delete_Me`** | `Object` | [`code/object.cpp:2435`](../../../code/object.cpp#L2435) | Пометка объекта на удаление (`ObjectsToDelete`) + немедленная постановка `flecs::Disabled`. |
| **E05** | **`Take_Damage`** | `Object` | [`code/object.cpp:1618`](../../../code/object.cpp#L1618) / [`code/techno.cpp:4971`](../../../code/techno.cpp#L4971) | Модификация входящего `int& damage` (щиты, резисты, поглощение) и реакция на попадание/уничтожение (`ResultType`). |
| **E06** | **`Can_Fire` / `Fire_At`** | `Object` (`Techno`) | [`code/techno.cpp:3921`](../../../code/techno.cpp#L3921) (`TechnoClass::Fire_At` и `Can_Fire`) | Запрет/разрешение выстрела и реакция на произведённый выстрел (`BulletClass*`). |
| **E07** | **`Record_The_Kill`** | `Object` (`Techno`) | [`code/techno.cpp:5323`](../../../code/techno.cpp#L5323) | Реакция компонентов на уничтожение цели данным юнитом/зданием (опыт, вампиризм, стаки). |
| **E08** | **`Captured`** | `Object` (`Techno`) | [`code/techno.cpp:4853`](../../../code/techno.cpp#L4853) | Смена дома-владельца при захвате (`HouseClass* newowner`). |
| **E09** | **`Assign_Target` / `Assign_Destination`** | `Object` (`Techno`) | [`code/techno.cpp:3682`](../../../code/techno.cpp#L3682), [`code/techno.cpp:7501`](../../../code/techno.cpp#L7501) | Реакция на смену боевой цели (`TarCom`) или цели движения (`NavCom`). |
| **E10** | **`Set_Coord`** | `Object` | [`code/object.cpp:2561`](../../../code/object.cpp#L2561) | Отслеживание смены координат объекта (`old_coord` → `new_coord`). |
| **E11** | **`Per_Cell_Process`** | `Object` (`Foot`) | [`code/foot.cpp:1937`](../../../code/foot.cpp#L1937) | Штатный триггер прохождения центра клетки (`PCPType`) для пехоты, техники и авиации. |
| **E12** | **`Receive_Message`** | `Object` | [`code/object.cpp:1564`](../../../code/object.cpp#L1564) | Участие компонентов в радиопротоколе (`RadioMessageType` между транспортом, заводом, доком и юнитом). |
| **E13** | **`Do_Cloak` / `Do_Uncloak` / `Stun`** | `Object` (`Techno`) | [`code/techno.cpp:5507`](../../../code/techno.cpp#L5507), [`5477`](../../../code/techno.cpp#L5477), [`3657`](../../../code/techno.cpp#L3657) | События ухода в маскировку, раскрытия и оглушения. |
| **E14** | **`Select` / `Unselect`** | `Object` | [`code/object.cpp:1046`](../../../code/object.cpp#L1046), [`1011`](../../../code/object.cpp#L1011) | Реакция UI/визуальных компонентов на выделение объекта игроком. |
| **E15** | **`Hidden` / `Revealed`** | `Object` | [`code/object.cpp:639`](../../../code/object.cpp#L639), [`1893`](../../../code/object.cpp#L1893) | Уход под пелену и раскрытие объекта для конкретного `HouseClass*`. |
| **E16** | **`Draw_Pre_Render` / `Draw_It` / `Draw_Post_Render`** | `Object` | [`code/tactical.cpp:2724`](../../../code/tactical.cpp#L2724)–[`2725`](../../../code/tactical.cpp#L2725) | Отрисовка компонентных эффектов под конкретным объектом и поверх него. |
| **E17** | **`Occupy_Down` / `Occupy_Up`** | `Cell` | [`code/cell.cpp:1741`](../../../code/cell.cpp#L1741), [`1816`](../../../code/cell.cpp#L1816) | Наступание объекта на клетку и уход с клетки (с учётом флага `bridge`) — ловушки, мины, ауры. |
| **E18** | **`Recalc_Passability` / `Recalc_Attributes`** | `Cell` | [`code/cell.cpp:4545`](../../../code/cell.cpp#L4545), [`1240`](../../../code/cell.cpp#L1240) | Пересчёт проходимости и атрибутов клетки с учётом блокирующих/ускоряющих компонентов клетки. |
| **E19** | **`Grow_Tiberium` / `Spread_Tiberium` / `Reduce_Tiberium`** | `Cell` | [`code/cell.cpp:4311`](../../../code/cell.cpp#L4311), [`4336`](../../../code/cell.cpp#L4336), [`2879`](../../../code/cell.cpp#L2879) | Кастомный рост, распространение и сбор ресурсов/заражений на клетке. |
| **E20** | **`Goodie_Check`** | `Cell` | [`code/cell.cpp:3476`](../../../code/cell.cpp#L3476) | Обработка подбора бонусов/ящиков при заходе `FootClass*` на клетку. |
| **E21** | **`Cell_Draw_It` / `Draw_Overlay`** | `Cell` | [`code/cell.cpp:2637`](../../../code/cell.cpp#L2637), [`2272`](../../../code/cell.cpp#L2272) | Отрисовка визуальных слоёв компонентов поверхности и оверлея клетки. |
| **E22** | **`Link_To_Object`** | `Loco` | [`code/loco.cpp:66`](../../../code/loco.cpp#L66) | Привязка локомотора к ведущему `FootClass*`. |
| **E23** | **`Loco_Process` / `Loco_PostProcess`** | `Loco` | `FootClass::AI` и [`code/logic.cpp:444`](../../../code/logic.cpp#L444) | Хук непосредственно до и после шага движения конкретного локомотора `Locomotion->Process()`. |
| **E24** | **`Move_To` / `Stop_Moving`** | `Loco` | [`code/loco.cpp`](../../../code/loco.cpp#L43) | Перехват начала движения к координате и остановки локомотора. |
| **E25** | **`Power_On` / `Power_Off`** | `Loco` | [`code/loco.cpp:141`](../../../code/loco.cpp#L141), [`154`](../../../code/loco.cpp#L154) | Реакция компонентов локомотора на EMP-импульс и восстановление питания. |
| **E26** | **`Begin_Piggyback` / `End_Piggyback` / `Draw_Matrix`** | `Loco` | [`code/drive.cpp:183`](../../../code/drive.cpp#L183), [`code/droppod.cpp:274`](../../../code/droppod.cpp#L274), [`code/loco.cpp:78`](../../../code/loco.cpp#L78) | Управление `flecs::Disabled` на вложенном локомоторе и модификация воксельной матрицы `Matrix3D`. |
| **E27** | **`Compute_CRC`** *(индивидуальный и посущностный)* | Все 3 | `Components.Compute_CRC<C>(crc)` и `Components.Compute_CRC(crc)` в [`code/object.cpp:2249`](../../../code/object.cpp#L2249), [`code/foot.cpp:3656`](../../../code/foot.cpp#L3656), `CellClass::Compute_CRC`, `LocomotionClass::Compute_CRC` | Расчёт `CRC` как одного конкретного компонента `C` на сущности, так и суммы всех компонентов данного носителя. |

### 4. Спецификация хвостовой секции `.SAV` в конце файла (`Q-SW-04`)

Данные EC записываются **отдельным блоком `EC01` в самом конце потока `SaveStreamClass`**, не трогая существующие записи объектов и клеток:

1. **Точки вызова в `code/saveload.cpp`:**
   - **Запись (`Put_All`, после `Session.Options.Save(stream)` на [`code/saveload.cpp:735`](../../../code/saveload.cpp#L735)):** вызывается `EC_Save_Trailer(stream)`. К этому моменту все живые `CellClass`, `ObjectClass` и `LocomotionClass` (включая вложенные `Piggybacker`) уже сохранены и получили свои `SwizzleIDType` в `Swizzler` ([`code/swizzle.cpp:76`](../../../code/swizzle.cpp#L76)) `[И]`.
   - **Чтение (`Get_All`, после `Session.Options.Load(stream)` на [`code/saveload.cpp:985`](../../../code/saveload.cpp#L985)):** если `stream.Offset() == stream.Size()`, открыт старый `.SAV` без хвоста EC — функция сразу возвращает `true` (100% обратная совместимость). Иначе `EC_Load_Trailer(stream)` проверяет сигнатуру `EC01` и версию.

2. **Двухпроходное чтение хвоста для защиты `Swizzler` от реаллокации таблиц `flecs`:**
   - Поскольку `Swizzler.Swizzle(&ptr, id)` ([`code/savestream.h:51`](../../../code/savestream.h#L51)) запоминает физический адрес поля в памяти до вызова `Swizzler.Resolve()` ([`code/saveload.cpp:1173`](../../../code/saveload.cpp#L1173)), хвост `EC01` делится на **Подблок 1 (оглавление архетипов)** и **Подблок 2 (полезная нагрузка компонентов)**:
     - **Подблок 1** читает для каждой сохранённой сущности `{ root_kind, owner_swizzle_id, flags, comp_count, comp_type_ids[...] }`, находит загруженный носитель и сразу создаёт все пустые слоты компонентов (`ensure<Head>()`). После этого все архетипы и буферы таблиц `flecs` заморожены и больше не реаллоцируются.
     - **Подблок 2** в `SaveStreamClass::BoundScope(stream, start + payload_length)` вызывает `comp.Serialize(stream)` прямо по финальному адресу компонента в таблице `flecs` (поэтому `SwizzleTarget`-указатели на `AbstractClass*` и `LocomotionClass*` внутри компонентов штатно разрешаются в `Swizzler.Resolve()`), а затем в `Post_Load_Game()` ([`code/scenario.cpp:1019`](../../../code/scenario.cpp#L1019)) вызывается `Post_Load`.

### 5. Архитектура покомпонентного и сводного `Compute_CRC` (`E27` + `G12`)

В `OpenTS` диагностика рассинхронизации работает в двух режимах ([`code/saveload.cpp:327`](../../../code/saveload.cpp#L327)–[`388`](../../../code/saveload.cpp#L388), [`code/queue.cpp:3710`](../../../code/queue.cpp#L3710)) `[И]`: быстрый кадровый `GameCRC` / `Object_CRCs()` и детальный отчёт `Print_Heap_CRCs(FILE * fp)`, который выводит `HEAP_SUMMARY` (отдельный `CRC` каждой кучи типов) и `HEAP_ROWS` (отдельный `CRC` каждого экземпляра объекта по его стабильному `Fetch_ID()`).

Чтобы `Compute_CRC` полноценно работал **для каждого компонента в отдельности**, `ComponentRegistry<Ts...>` генерирует единую статическую цепочку из четырёх уровней на базе концепта `HasComputeCRC<C>` (`void Compute_CRC(CRCEngine&) const` или `void Compute_CRC(CRCEngine&, Root const&) const`):

1. **Индивидуальный `CRC` одного конкретного компонента `C` (`E27a`):**
   - `uint32_t Single_Component_CRC(C const & comp)` и `bool TypedEntity<Root>::Compute_CRC<C>(CRCEngine & crc) const` — вычисляют контрольную сумму ровно одного выбранного компонента `C` на конкретном носителе (используется в модульных тестах, точечной отладке и построчном отчёте десинка).
2. **Вклад каждого компонента в `Compute_CRC` своего носителя (`E27b`):**
   - `TypedEntity<Root>::Compute_CRC(CRCEngine & crc) const` проходит по зарегистрированным типам `Head` (`if constexpr (HasComputeCRC<Head>)`) и для каждого прикреплённого компонента вписывает в `crc` пару `(ComponentTypeID<Head>, comp.Compute_CRC(crc))`.
   - Вызывается в `ObjectClass::Compute_CRC` ([`code/object.cpp:2249`](../../../code/object.cpp#L2249)), `CellClass::Compute_CRC` и `LocomotionClass::Compute_CRC` (вызываемом из `FootClass::Compute_CRC` на [`code/foot.cpp:3656`](../../../code/foot.cpp#L3656) и для активного `Locomotion`, и для `Piggybacker`), благодаря чему в `HEAP_ROWS` (`Units`, `Infantry`, `Buildings`, `Aircraft`) сразу меняется `CRC` того юнита, чей компонент или локомотор разошёлся `[В]`.
3. **Отдельная таблица `CRC` по каждому типу и каждому экземпляру компонента в `Print_Heap_CRCs` (`G12a`, [`code/saveload.cpp:388`](../../../code/saveload.cpp#L388)):**
   - В `Print_Heap_CRCs(FILE * fp)` после куч вызывается `GameComponents::Print_Component_CRCs(world, fp)`:
     - **Сводка по каждому типу компонента отдельно (`COMPONENT_SUMMARY`):** для каждого `Head` с `HasComputeCRC<Head>` собираются все его экземпляры, сортируются по стабильному межмашинному ключу владельца `(root_kind, owner_stable_id, subslot)` (а не по внутреннему `ecs_entity_t`, который может сдвигаться локальными визуальными операциями!) и печатается строка `%-28s count=%-5d crc=%08x` ровно в формате `HEAP_SUMMARY`;
     - **Построчный дамп каждого экземпляра компонента отдельно (`COMPONENT_ROWS`):** для каждого экземпляра `Head` вычисляется его **собственный изолированный** `CRCEngine comp_crc; comp.Compute_CRC(comp_crc);` и печатается строка `%05d  Owner:%s:%-8d  %08x`. При десинке это мгновенно указывает точный тип компонента и точный экземпляр (будь то юнит, локомотор или клетка `CellID`), который разошёлся между пирами `[В]`.
4. **Вклад в сетевой `GameCRC` и `Object_CRCs` (`G12b`, [`code/queue.cpp:3770`](../../../code/queue.cpp#L3770) и [`code/saveload.cpp:341`](../../../code/saveload.cpp#L341)):**
   - В `Compute_Game_CRC()` (который сейчас хеширует только координаты юнитов, слои и `RandomNumber`!) и в `Object_CRCs()` добавляется детерминированная свёртка по типам компонентов (без двойного учёта уже пройденных куч в `Object_CRCs` — для `Object_CRCs` добираются `CellClass`-компоненты, а в кадровом `Compute_Game_CRC` учитываются все симуляционные компоненты по детерминированному ключу владельца).

## Варианты

| Вариант | Плюсы | Минусы | Цена | Что ломает |
| --- | --- | --- | --- | --- |
| **Вариант A (согласован `[А]`): Ленивый `TypedEntity<Root>` в `ObjectClass`, `CellClass`, `LocomotionClass` + `ComponentRegistry<Ts...>` (`AI`/`PostAI` + каталог хуков) + двухфазный хвост `EC01` в конце `.SAV` + индивидуальный и сводный `Compute_CRC`** | 1. 100% статическая типизация без `void*`/`dynamic_cast`.<br>2. Имена и точки врезки соответствуют `OpenTS` (`AI`, `PostAI`, `Limbo`/`Unlimbo`, `Detach`, `Serialize`, `Post_Load`, `Compute_CRC`).<br>3. Хвостовой блок `EC01` в конце `.SAV` сохраняет обратную совместимость и корректно работает со `Swizzler`.<br>4. Покомпонентный `Compute_CRC` покрывает и одиночный компонент, и объект в `HEAP_ROWS`, и отдельные таблицы компонентов в отчёте десинка, и `GameCRC`. | Требует статического имени/идентификатора у сериализуемых и CRC-компонентов (`SaveName`) для стабильной диагностики и независимости `.SAV` от порядка типов. | Подмодуль `thirdparty/flecs` (`FLECS_CUSTOM_BUILD` без систем), модуль `code/ec/`, врезка хуков, секции `.SAV` и `Print_Component_CRCs` + автономные unit/CTest-тесты. | Ничего: старые `.SAV` читаются штатно (`Offset() == Size()`), пустой реестр даёт `0` накладных расходов в рантайме. |
| **Варианты B / C / D** | Отклонены владельцем на предыдущих шагах разбора. | — | — | — |

## Рекомендация

Переходить от анализа к реализации (**Вариант A**), включив в первую итерацию ядро `thirdparty/flecs`, `TypedEntity<Root>`, `ComponentRegistry<Ts...>`, двухпроходный хвост `EC01` в `.SAV`, полную 4-уровневую подсистему `Compute_CRC` (`E27` + `G12`) и базовый контур жизненных/тиковых хуков.

## Открытые вопросы

Вопросы `Q-SW-01..05` закрыты решениями владельца `[А]` в [`meta/questions/software.md`](../../questions/software.md). Остаётся подтверждение объёма врезки перед переключением из `software/analysis` на реализацию (`HYAI-INT-09`, `HYAI-PERM-G02`, `HYAI-PERM-G03`):

- `Q-SW-06` — Стартуем реализацию с **базовым контуром из 20 точек** (`G01..G05`, `G09..G12` + `E01..E05`, `E11`, `E17`, `E22`, `E25..E27`, включая хвост `.SAV` и полный покомпонентный `Compute_CRC`), или врезаем сразу **все 39 точек** (`G01..G12` + `E01..E27`)?
