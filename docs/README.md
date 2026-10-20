# Developer documentation

The developer guides are split by subject:

- [Building OpenTS](BUILDING.md) — supported toolchain, commands, outputs,
  build identity, and continuous integration.
- [Style](STYLE.md) — source formatting, naming, C++ use, and comments.
- [History](HISTORY.md) — source lineage and reconstruction history.
- [Rationale](RATIONALE.md) — reconstruction tools, recovered structure, and
  non-obvious implementation choices.
- [Project direction](DIRECTION.md) — long-term architecture.
- [Entity-component adoption plan](ENTITY-COMPONENT-PLAN.md) — entity coverage
  for final game classes, component lifecycle hooks and engine call sites, and
  compatibility boundaries (in Russian while the layer is in progress).
- [The saved game format](SAVE-FORMAT.md) — the layout of a `.SAV` file: its
  header, listing fields, compressed content, and object records.
- [ts-patches in OpenTS](TS-PATCHES.md) — each CnCNet ts-patches patch and
  what OpenTS does in its place.

See [CONTRIBUTING.md](../CONTRIBUTING.md) for contribution and review rules.
Player and modder documentation is under [manual/](../manual/README.md). When a
guide already covers a subject, link to it instead of copying the same facts.
