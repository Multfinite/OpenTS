---
id: report.software.2026-10-30-demo-produce-cash-component
title: "Демо-рефакторинг механики ProduceCash в EC-компонент (ProduceCashComponent)"
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

# Демо-рефакторинг механики `ProduceCash` в EC-компонент (`ProduceCashComponent`)

## Задача

Переработать игровую механику периодической генерации/списания кредитов и бонуса захвата нейтральных зданий из коммита `5eb03db34337153ba3f4f67162b8b97d3bb595f1` (*«Let a building produce cash on an interval»*) в полноценный демонстрационный компонент `EC::ProduceCashComponent` на базе статической C++20 EC-архитектуры (`SW-0001`) с полным сохранением игрового поведения, сериализации и мультиплеерной синхронизации (`Compute_CRC`).

## Что сделано

- `code/ec/components/produce_cash.h`, `code/ec/game_components.h` — реализован компонент `EC::ProduceCashComponent` (`using OwnerType = BuildingClass;`, `SaveName = "OpenTS.ProduceCash"`) и зарегистрирован в боевом реестре `EC::GameComponents`:
  - перенесены поля экземпляра `Timer` (`CDTimerClass<FrameTimerClass>`), `Remaining` и `IsStartupPaid`;
  - реализованы методы `Grand_Opening(BuildingClass &, bool captured)`, `Captured(BuildingClass &, HouseClass * oldowner, HouseClass * newowner)`, `PostAI(BuildingClass &)`, `Serialize(StreamT &)` и `Compute_CRC(CRCEngine &) const` с полным соблюдением контракта `manual/content/systems/produce-cash.md` (интервал `ProduceCashDelay`, лимит и дожим остатка `ProduceCashBudget`, отрицательный расход `Spend_Money`, защита от `INT_MIN` и переполнения `INT_MAX`, пауза/возобновление при `Powered=yes` и дефиците энергии или ЭМИ, подавление при `MISSION_DECONSTRUCTION`, `Strength <= 0`, `Limbo` и `MultiplayPassive`, а также одноразовый/многоразовый бонус `ProduceCashStartup` при захвате у нейтрального дома).
- `code/ec/concepts.h`, `code/ec/entity_inl.h`, `code/ec/registry.h`, `code/ec/engine_hooks.h`, `code/ec/engine_hooks.cpp`, `code/techno.cpp` — расширены C++20 концепты и диспетчеры `ComponentRegistry`:
  - добавлена статическая поддержка глобальных проходов и `Compute_CRC` для компонентов с явным подклассом владельца `using OwnerType = SubClass;` (`HasDerivedObjectOwner`, прямая передача `BuildingClass &` в `PostAI` без `dynamic_cast`);
  - добавлен точечный хук `On_Grand_Opening` / `Dispatch_Grand_Opening` для `BuildingClass::Grand_Opening(bool captured)` (до установки `HasOpened = true`);
  - расширен хук `On_Captured` / `Dispatch_Captured` передачей пары `(oldowner, newowner)` как в `TechnoClass::Captured`, так и в переопределённом `BuildingClass::Captured` с сохранением обратной совместимости для одноаргументных компонентов `Captured(newowner)`;
  - добавлен `<flecs.h>`-независимый хелпер `EC::Hooks::Ensure_Produce_Cash(BuildingClass &)` для ленивого прикрепления компонента без втягивания `<flecs.h>` в `code/building.cpp`.
- `code/builtype.h`, `code/building.h`, `code/building.cpp` — из `BuildingClass` полностью удалены поля `ProduceCashTimer`, `ProduceCashRemaining`, `IsProduceCashStartupPaid`, методы `Produce_Cash_AI()`, `Produce_Cash_Startup()` и ручные вызовы в `BuildingClass::AI`, `BuildingClass::Serialize` и `BuildingClass::Compute_CRC`; в `BuildingTypeClass` добавлен предикат `Has_Produce_Cash()`; обычные здания без `ProduceCash*` теперь не материализуют сущностей в `flecs` и не несут накладных расходов памяти, тика и `.SAV`.
- `tests/CMakeLists.txt`, `tests/producecash/CMakeLists.txt`, `tests/producecash/producecashtest.cpp`, `tools/tests/test_ec_flecs.py` — создан автономный C++20 тестовый набор `producecashtest` (15 контрактных проверок боевого `EC::GameComponents` + `ProduceCashComponent` поверх реального `thirdparty/flecs`, `CDTimerClass<FrameTimerClass>`, двухфазного блока `EC01` и 4 уровней `Compute_CRC`) и подключён в `tools/tests/test_ec_flecs.py`.
- `meta/decisions/software/SW-0001-static-ec-flecs-architecture.md`, `meta/architecture/software.md`, `meta/reports/README.md`, `meta/reports/software/README.md` — синхронизирован список `affects:` решения `SW-0001`, инвариант `SW-INV-04` и индексы отчётов.

## Проверка

| Проверка | Команда | Результат |
| --- | --- | --- |
| Автономные C++20 тесты `ectest` и `producecashtest` (`-Wall -Wextra -Werror`) | `python3 -m unittest tools/tests/test_ec_flecs.py -v` | `Ran 5 tests ... OK` (`15/15` проверок в `producecashtest`) `[В]` |
| Все модульные тесты инструментов и EC | `python3 tools/run_unit_tests.py` | `OK` `[В]` |
| Полный конвейер проверок HYAI и репозитория | `python3 HYAI/HYAI/tools/run_checks.py --root /home/user/repo` | `0 падений` (`15/15 OK`) `[В]` |

## Открытые вопросы

Открытых вопросов нет.
