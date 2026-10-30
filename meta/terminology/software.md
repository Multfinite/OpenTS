---
id: meta.terminology.software
title: "Терминология домена software (OpenTS)"
type: meta
status: approved
authority: contract
visibility: internal
version: "1.0"
updated: "2026-10-30"
---

# Термины домена `software`

| Термин | Значение | Привязка в коде |
| --- | --- | --- |
| `контракт` | Публичный заголовок, формат сейва/сетевого пакета или INI-ключ, изменение которого требует `HYAI-PERM-G02` | `code/*.h`, `docs/SAVE-FORMAT.md` |
| `инвариант` | Свойство детерминизма симуляции, совместимости сейвов или чистоты экспорта | `tests/*`, `tools/export_branch.py` |
| `игровой объект` | Наследник базовой иерархии сущностей мира *Tiberian Sun* | `code/abstract.h` (`AbstractClass`, `ObjectClass`, `TechnoClass`) |
| `чистый экспорт` | Отдельная Git-ветка от апстрим-базы без следов и артефактов `HYAI` (`PRJ-POL-01..06`) | `tools/export_branch.py` |
| `таргет сборки` | Поддерживаемая комбинация платформы (`Win32`/`x64`) и конфигурации (`Debug`/`Release`) | `CMakeLists.txt`, `cmake/` |
| `контур` | Уровень доступа артефакта (`public` / `internal` / `restricted`) | `meta/schema/domains/software.yaml` |
