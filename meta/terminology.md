---
id: meta.terminology
title: "Терминология OpenTS: кросс-доменные термины"
type: meta
status: approved
authority: contract
visibility: internal
version: "1.0"
updated: "2026-10-30"
---

# Терминология проекта

Термины, которые используются более чем в одном домене `OpenTS`. Узкоспециальные термины живут в `meta/terminology/<domain>.md`.

| Термин | Значение | Где используется |
| --- | --- | --- |
| `домен` | Предметная область со своей нормативностью и системой доступа | все |
| `профиль` | Тема, задача и мыслительный процесс внутри домена | все |
| `контур` | Уровень доступа документа (`visibility`) | все |
| `норма` | Утверждение, определяющее поведение системы (`authority` выше `convention`) | все |
| `решение` | Зафиксированная норма с номером в `meta/decisions/<scope>/` | все |
| `открытый вопрос` | Неизвестное, оформленное как `Q-*` | все |
| `провенанс` | Маркер происхождения утверждения: `[А]/[И]/[В]/[Г]/[?]` | все |
| `чистый экспорт` | Выгрузка продуктового решения задачи в отдельную ветку от `origin/main` без любых упоминаний и артефактов `HYAI` (`PRJ-POL-01..06`, `P-0001`) | `software`, `docs` |
| `требование` | Проверяемое утверждение о должном с `REQ-ID` и критерием приёмки | `docs`, `software` |

## Доменные словари

- [`terminology/software.md`](terminology/software.md)
- [`terminology/docs.md`](terminology/docs.md)
- [`terminology/narrative.md`](terminology/narrative.md)
- [`terminology/picture.md`](terminology/picture.md)
- [`terminology/video.md`](terminology/video.md)
- [`terminology/kit.md`](terminology/kit.md)
