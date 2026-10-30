---
id: meta.questions.kit
title: "Открытые вопросы домена kit"
type: meta
status: approved
authority: none
visibility: internal
version: "2.0"
updated: "2026-10-22"
---

# Вопросы домена `kit`

| ID | Вопрос | Почему важен | Что закроет | Статус |
| --- | --- | --- | --- | --- |
| Q-KIT-01 | Какой host/runtime и provider/model используются, как они загружают AGENTS.md и позволяют ли читать доменные файлы по требованию? | Без этого неизвестно, сработает ли lazy loading и как измерить токены. | Спецификация host-контекста и baseline для сравнимого пилота до/после. | open |
| Q-KIT-02 | Какой корневой каталог закрепить за доменом `picture` (`HYAI/core/domains/picture/pictures/` независимо от `narrative` или перенос `styles/` из `narrative` с ревизией `NR-POL-15..19` и `narrative/image`)? | `KIT-POL-06` запрещает двум доменам владеть одним `root_paths`, а `styles/` уже принадлежит `narrative`. | Закрыто решением владельца (`[А]`): самодостаточный домен с корнем `HYAI/core/domains/picture/pictures/` в самом HYAI и собственным `<picture_root>` вне подмодуля во внешних проектах, зафиксировано в `KIT-0003`. | closed |
| Q-KIT-03 | Какую модель доступа использовать в `picture`: плоскую `flat` (`public`, `internal`, `restricted`) или расширенную контурами `spoiler` / `client` (либо `role_prism`)? | Изображения могут иллюстрировать закрытые сущности `narrative` (`spoiler`) и коммерческие материалы с ограничениями лицензий/NDA. | Закрыто решением владельца (`[А]` — на усмотрение агента): выбрана плоская модель `flat` (`public / internal / restricted`), зафиксировано в `KIT-0003`. | closed |
| Q-KIT-04 | Какой состав профилей домена `picture` и какую активную пару включить в `HYAI/meta/manifest.yaml` (`art-director` + `prompt-author` + `analysis` или иной набор)? | `check_domains.py` требует непересекающихся `scope_primary` у профилей домена (`KIT-POL-08`), а `active` задаёт профиль по умолчанию в `AGENTS.md`. | Закрыто решением владельца (`[А]` — на усмотрение агента): профили `instant`, `pipeline`, `art-director`, `analysis` и активная пара `picture/instant`, зафиксировано в `KIT-0003`. | closed |
| Q-KIT-05 | Какой префикс идентификаторов закрепить за доменом `picture` в `rule-ids.yaml` и `decisions.scopes`: `PIC` или `PICT`? | Префикс правил и решений закрепляется навсегда и не переиспользуется (`HYAI-DEC-17`). | Закрыто решением владельца (`[А]`, вариант A): префикс `PIC`, зафиксировано в `KIT-0003`. | closed |

Вопросы каркаса задаются, когда правило нечем проверить (`HYAI-PROV-09`): это сигнал, что либо норма сформулирована как пожелание, либо нужен новый движок.
