# Developer documentation

The developer guides are split by subject:

- [Building OpenTS](BUILDING.md) — supported toolchain, commands, outputs,
  build identity, and continuous integration.
- [Style](STYLE.md) — source formatting, naming, C++ use, and comments.
- [History](HISTORY.md) — source lineage and reconstruction history.
- [Rationale](RATIONALE.md) — reconstruction tools, recovered structure, and
  non-obvious implementation choices.
- [Project direction](DIRECTION.md) — long-term architecture.
- [Entity-component adoption plan](ENTITY-COMPONENT-PLAN.md) — the proposed
  flecs adoption sequence: entity coverage for final game classes, components
  for new functionality only, and no flecs systems.
- [Component lifecycle and engine call sites](COMPONENT-LIFECYCLE.md) — the
  engine call sites, ordering invariants, and dispatch options for component
  callbacks (`Ready`, `Process`, world entry and exit, detachment, rendering,
  persistence, and checksums).
- [The saved game format](SAVE-FORMAT.md) — the layout of a `.SAV` file: its
  header, listing fields, compressed content, and object records.
- [ts-patches in OpenTS](TS-PATCHES.md) — each CnCNet ts-patches patch and
  what OpenTS does in its place.

See [CONTRIBUTING.md](../CONTRIBUTING.md) for contribution and review rules.
Player and modder documentation is under [manual/](../manual/README.md). When a
guide already covers a subject, link to it instead of copying the same facts.
