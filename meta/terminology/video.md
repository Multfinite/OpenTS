---
id: meta.terminology.video
title: "Терминология домена video"
type: meta
status: review
authority: none
visibility: internal
version: "1.0"
updated: "2026-10-29"
---

# Терминология `video`

| Термин | Значение |
| --- | --- |
| `video-canon` | Утверждённые визуальные/звуковые инварианты проекта в уровне authority домена video |
| `continuity bible` | Статические ID/invariants персонажей, мест, props, стиля и audio; не заменяет состояние действия |
| `temporal state` | Конкретное входное/выходное состояние shot: позы, позиции, props, действие, camera, light и sound |
| `continuity handoff` | Явно подтверждённый пользователем механизм, которым segment N получает контекст segment N−1 (last frame, frame tail/video, отдельный still reference или независимый запуск) |
| `I2V reference` | Статичное изображение, которое модель использует как reference; само по себе не свидетельствует о передаче motion/video context |
| `seamless boundary` | Граница двух сегментов, для которой brief требует непрерывного воспринимаемого движения/состояния, а не намеренного монтажного cut |
| `seam-window` | Отдельно обработанное и проверяемое временное окно вокруг фактического seam, охватывающее материал слева и справа от границы; включает audio window, когда sound нужен (`VID-POL-07`) |
| `bridge segment` | Дополнительно созданный переходный фрагмент между состояниями; capability и результат подтверждаются для выбранного пользователем средства |
| `human approval gate` | Явное решение человека о продолжении или принятии brief, storyboard, seam-window или delivery |
| `<video_root>` | Рабочий корень проекта из `root_paths` манифеста активного домена video; в самом HYAI `HYAI/core/domains/video/videos` |
