# Entity-component adoption plan (flecs, no systems)

Status: proposal, with Phases 0, 1, and 3 implemented on the adoption
branch and not yet merged. On `main` the game does not use flecs or an
entity-component store today, and nothing in this page describes behavior
there. Each remaining phase starts only after a maintainer agrees to its
scope, as [Contributing](../CONTRIBUTING.md#before-starting) requires for
non-trivial work. [Project direction](DIRECTION.md) owns the long-term
decision to move toward an entity-component architecture; this page owns
the adoption sequence.

Implementation status on the adoption branch:

- Phase 0: done. `cmake/CheckEntityInventory.cmake` runs as the
  `entityinventory` CTest check and verifies all 65 included classes
  against `code/flecs/register.cpp` and the constructor attach calls.
- Phase 1: done. flecs v4.1.6 is a pinned submodule; `code/flecs/`
  (`id.hh`, `lifecycle.hh`, `world.h`, `world.cpp`, `components.hh`,
  `register.cpp`) builds the bridge and the Option B `ComponentOps`
  lifecycle dispatcher ([Component lifecycle](COMPONENT-LIFECYCLE.md))
  with `OPENTS_EC` on by default; `tests/flecs` covers attach, tags,
  parenting, cascade count accuracy, C++20 component lifecycle hooks,
  canonical `(Kind, FetchId, AttachSequence)` lockstep ordering, CRC
  folding, directory-framed save streams, reset, and shutdown across
  Debug (ASan + UBSan + LSan), Release, and `OPENTS_EC_DISABLED` on a
  Linux toolchain; the four MSVC configurations still need CI or
  maintainer builds, which are build evidence only.
- Phase 2: attach half and core lifecycle bridge done; engine call-site
  wiring (`logic.cpp`, `scenario.cpp`, `object.cpp`, `tracker.cpp`,
  `saveload.cpp`, `tactical.cpp`, `startup.cpp`) and the vertical slice
  stay open until a first feature (D4) lands, by rule 3.
- Phase 3: done in code. All 65 included classes attach in their
  constructors (81 constructor bodies), preserve clean entity state on
  copy construction, and destroy from `~AbstractClass()` /
  `~LocomotionClass()`; locomotors parent through `Link_To_Object` and
  restore parenting in `LocomotionClass::Post_Load`. MSVC builds and
  played checks are still open.
- Phase 4: starts when a first feature lands by the §7 checklist.
- Phase 5: partially done (bridge-level leak accounting through
  `Live_Entity_Count`, reset and failed-destroy tolerance in
  `tests/flecs`). Played and long-session checks are open.

The plan has three fixed rules:

1. **No flecs systems.** No registered system, pipeline, phase, or
   `world.progress()` call runs game logic at any point in this plan.
2. **Instances of existing final game classes get a flecs entity, except
   the classes named as excluded in [Scope](#2-scope-final-classes).**
   "Final" means a leaf of the inherited game-object hierarchy as defined
   there — not every leaf class in `code/`.
3. **Components carry only new functionality.** No existing class member
   moves into a component under this plan. The entity is an identity and an
   attachment point; the C++ object keeps owning its current state and
   behavior.

## 1. What "no systems" means in flecs terms

flecs separates data storage from the mechanisms that act on it. This plan
uses only the storage side:

| flecs mechanism | Use in this plan |
| --- | --- |
| `flecs::world`, `flecs::entity` | Yes. One world owns every entity. |
| Components and tags (`world.component<T>()`, `entity.set/add/get/remove`) | Yes, for new functionality only. |
| Relationships (`ChildOf`, pairs) | Yes, where a new feature needs them, and for the type link and locomotor children in §5. |
| Prefabs (one prototype entity per final class) | Optional in Phase 3. A prefab is stored data, not scheduled code. Instance entities may link to it with `IsA` only after the copy semantics are decided per class. |
| Queries (`world.query`, `world.each`) | Yes, but only when called explicitly from an existing call site. A query here is a loop the caller starts and finishes; nothing registers it for later execution. |
| Deferred mode (`world.defer_begin/end`) | Yes, as a batching scope around explicit loops that create or destroy entities. It schedules nothing. |
| Observers (`world.observer`, `OnAdd`/`OnSet`/`OnRemove` callbacks) | No in Phases 1–3. An observer is not a system, but it is hidden control flow: a `set()` in one place runs a callback registered elsewhere. Gameplay observers need a separate maintainer decision with a determinism review. Component hooks get the same treatment. |
| Systems (`world.system`, `ecs_system`) | **Never under this plan.** |
| Pipelines, phases, `world.progress()`, timers, `flecs::app` | **Never under this plan.** Nothing may advance the world implicitly. |
| flecs add-ons: script, JSON, meta/reflection, REST, HTTP, stats, units | No. They add parsers, sockets, or string-keyed access the engine does not need, and REST/HTTP would open a network surface. Build flecs without them. |
| Multithreading (`world.set_threads`, OS API callbacks) | No. The simulation stays single-threaded in its current call order. |

Behavior stays where it is now: virtual functions such as
`ObjectClass::AI()`, driven by `LogicClass::AI()` and
`LogicClass_AI_Logic_507470()` in `code/logic.cpp`, and the per-class
`Update_All()` helpers. New functionality written against components is
called from those existing places, by name, in an order the caller can see.
If a future feature wants scheduled systems, that is a new proposal that
replaces rule 1; it is not a later phase of this plan.

## 2. Scope: final classes

### 2.1 How the inventory was derived

The inventory was taken from the class definitions in `code/*.h` and
`code/*.hh` (excluding `code/language/`): a class is in scope when it is
defined in the `AbstractClass` or `LocomotionClass` tree and no other class
in the tree derives from it. No class in the tree is marked `final`, so
"final" is a property of the current hierarchy, and the inventory must be
regenerated whenever a class is added — Phase 0 turns the derivation into a
checked script.

Measured on the current tree:

| Population | Count |
| --- | --- |
| Classes defined in `code/*.h`, `code/*.hh` | 598 |
| Leaf classes across the whole tree (UI, files, network, rendering, containers, …) | 541 |
| Internal (non-leaf) classes in the two game trees | 11 |
| Leaf classes in the two game trees (candidates) | 71 |
| — leaves under `AbstractClass` | 61 |
| — leaves under `LocomotionClass` | 10 |
| **Excluded by decision** | **5** |
| **Classes getting entities** | **65, plus `Tactical` once its open decision is made** |

Creating entities for all 541 leaves would attach flecs to gadget, file,
blitter, and socket classes that have no game identity, no heap, and no save
record. They are out of scope.

Five candidate classes are excluded by decision, not by oversight:
`AnimClass`, `IsometricTileClass`, `LogicClass`, `TActionClass`, and
`TEventClass` get no entity (reasons in §3.6 and §8). `Tactical` is the one
class with no decision yet; it is counted neither in nor out until the
Phase 0 decision is recorded. A new game class joins the scope when it
derives from `AbstractClass` or `LocomotionClass` and has no subclass; the
Phase 0 check fails the build's test run until its row is added to the table
in §8, as included, excluded, or undecided.

The 11 internal classes get no entity of their own. An instance of
`UnitClass` is one entity, tagged as a unit — not four entities for
`ObjectClass`, `MissionClass`, `RadioClass`, `TechnoClass`, `FootClass`,
and `UnitClass`:

- `AbstractClass` (`code/abstract.h`), `ObjectClass` (`code/object.h`),
  `MissionClass`, `RadioClass`, `TechnoClass`, `FootClass`, `LayerClass`;
- `AbstractTypeClass` (`code/abstype.h`), `ObjectTypeClass`,
  `TechnoTypeClass`;
- `LocomotionClass` (`code/loco.h`).

### 2.2 The two trees

```text
AbstractClass : IPersistent                          code/abstract.h
├─ AbstractTypeClass                                 code/abstype.h
│  ├─ 13 direct leaves (CampaignClass, HouseTypeClass, ScriptTypeClass,
│  │   SideClass, SuperWeaponTypeClass, TagTypeClass, TaskForceClass,
│  │   TeamTypeClass, TiberiumClass, TriggerTypeClass, WarheadTypeClass,
│  │   WeaponTypeClass, AITriggerTypeClass)
│  └─ ObjectTypeClass
│     ├─ 9 direct leaves (AnimType, BulletType, IsometricTileType,
│     │   OverlayType, ParticleSystemType, ParticleType, SmudgeType,
│     │   TerrainType, VoxelAnimType)
│     └─ TechnoTypeClass → AircraftType, BuildingType, InfantryType, UnitType
├─ ObjectClass                                       code/object.h
│  ├─ 12 direct leaves (Anim ✗, BuildingLight, Bullet, IsometricTile ✗,
│  │   Overlay, Particle, ParticleSystem, Smudge, Terrain,
│  │   VeinholeMonster, VoxelAnim, Wave)
│  └─ MissionClass → RadioClass → TechnoClass
│        ├─ BuildingClass
│        └─ FootClass → AircraftClass, InfantryClass, UnitClass
├─ LayerClass → LogicClass ✗ (singleton driver, see §3.6)
└─ 18 direct leaves (AlphaShape, Cell, EMPulse, Factory, FoggedObject,
    House, LightSource, Neuron, Script, Super, TAction ✗, TEvent ✗,
    Tactical ?, Tag, Team, Trigger, Tube, WaypointPath)

LocomotionClass : IPersistent, ILocomotion           code/loco.h
└─ Drive, DropPod, Fly, Hover, Jumpjet, Levitate, Mech, Teleport,
   Tunnel, Walk (10 leaves; some also implement IPiggyback)
```

✗ = excluded by decision, ? = no decision yet (§3.6, §4).

Counts check: 26 type leaves + 12 non-techno object leaves + 4 techno
leaves + 19 remaining `AbstractClass` leaves = 61 candidates; minus the
5 excluded (`AnimClass`, `IsometricTileClass`, `LogicClass`,
`TActionClass`, `TEventClass`) = 56, of which `Tactical` is undecided;
plus 10 locomotors = 65 classes getting entities, or 66 with `Tactical`.

## 3. Analysis of the current engine

### 3.1 Four identities already exist; the entity is a fifth

| Identity | Type and owner | Survives | Used by |
| --- | --- | --- | --- |
| Heap index | `int`, from `Fetch_Heap_ID()` / the owning `DynamicVectorClass<T*>` position | One scenario run | Iteration, `OBJECT_HEAP_LIST` in `code/objheaps.hh` |
| Abstract ID | `int AbstractClass::ID`, from `Create_ID()` (`Scen->Get_Unique_ID()`) | A save record (`Serialize` writes it) | Cross-references, CRC reports |
| RTTI type | `RTTIType` enum, 61 values plus `RTTI_COUNT`, `code/rtti.hh` | Source-level only | `What_Am_I()`, `Fetch_RTTI()`, casts |
| Class identifier | 16-byte `ClassID`, `code/classid.h`, values in `code/classids.cpp` | Save files and `Locomotor=` INI keys | `Register_Class` / `Create_Object` in `code/classfactory.*`, called from `RegisterClasses()` in `code/startup.cpp` |
| Swizzle identity | `uint32_t`, `code/swizzle.h` | One save or load operation | Pointer remapping while a save is written or read |

Consequences for the design:

- The flecs entity replaces none of these. Saves keep naming objects by
  class identifier and swizzle identity; the CRC keeps folding `ID` and
  members; INI keeps naming locomotors by class identifier.
- The raw flecs entity value is process-local. flecs recycles entity IDs
  with generation bits, and two runs — let alone two network peers — do not
  assign the same values. It must never be written to a save, a network
  packet, a replay, or a CRC. §6 fixes this as a rule, and the Phase 2 test
  harness checks that no entity value reaches `Object_CRCs()` output by
  comparing CRCs across two differently-ordered runs.
- Mapping direction is one-way: entity → C++ object is stored in the bridge
  component (§5.3); C++ object → entity is stored on the object (§5.2).
  Nothing outside `code/flecs/` may cache a flecs entity across a frame,
  because the object it names can be destroyed and the ID recycled. Callers
  re-resolve through the object, or store the object pointer as they do
  today and ask for its entity when needed.

### 3.2 Lifecycle: objects register themselves, in layers

There is no central factory for game objects. Each hierarchy level adds
`this` to its own global heap in its constructor and removes it in its
destructor:

- `ObjectClass` adds to `Objects` and to the pointer trackers
  (`ObjectPtrTracker`, `AbstractTypePtrTracker`, `TagPtrTracker`);
- `TechnoClass` adds to `Technos` and `HousePtrTracker`;
- `UnitClass`, `InfantryClass`, `BuildingClass`, `AircraftClass` add to
  `Units`, `Infantry`, `Buildings`, `Aircraft`; `BuildingClass` also adds
  to factory and repair-facility lists;
- `HouseClass`, `CellClass`, `IsometricTileClass`, `ScriptClass`,
  `TActionClass`, `TEventClass`, and the rest follow the same pattern with
  the heaps declared in `code/globals.h` and the per-class headers.

Destruction is equally distributed: destructors call
`Detach_This_From_All()`, delete from every heap and tracker, and clear
`IsActive`. Deletion during iteration is deferred through
`ObjectsToDelete`. The constructor audit for the 61 `AbstractClass` leaves
finds two constructors for 13 of them (`AlphaShapeClass`, `AnimClass`,
`EMPulseClass`, `LightSourceClass`, `ParticleClass`,
`ParticleSystemClass`, `SuperClass`, `TaskForceClass`, `TerrainClass`,
`VeinholeMonsterClass`, `VoxelAnimClass`, `WaveClass`,
`WaypointPathClass`), five for `FoggedObjectClass`, none defined for
`LogicClass`, and one for each of the remaining 46. The exact per-class
count is rechecked by the Phase 0 script.

Consequences:

- Entity creation cannot hook one factory. It must hook the one place
  every included class passes through — the leaf constructor — or be driven from the
  heaps after the fact. §5.2 chooses the leaf constructor, following the
  existing `Units.Add(this)` pattern so the new line reads as part of a
  pattern reviewers already know.
- Entity destruction can hook the one place all of them pass through on
  the way out: `~AbstractClass()`. By then the leaf part is gone, which is
  fine — destruction needs only the stored entity ID. Locomotors do not
  derive from `AbstractClass`, so they need the matching call in
  `~LocomotionClass()` (`code/loco.cpp`).
- An attach operation must be idempotent and must tolerate running before
  startup finishes. The `CellClass BlubCell` sentinel is constructed
  before any startup code runs, and cells attach (§3.6). (`LogicClass
  Logic` is also a pre-`main` global, but it is excluded and never
  attaches.) The world therefore cannot be created only in
  `RegisterClasses()`; §5.1 makes creation lazy and shutdown explicit.
- Placement construction exists: `MapClass::Init_Cells()` runs
  `new (Array[cellnum]) CellClass` over cells that may already hold a
  constructed object, without a matching destructor call first. Cells get
  entities (§3.6), so attach defines this case directly: constructing
  over a live entity ID destroys the old entity first and creates a fresh
  one (§5.3, step 1). No stale component survives a cell reset.

### 3.3 Creation paths an entity must survive

| Path | Where | What the plan must guarantee |
| --- | --- | --- |
| `new XClass(...)` at hundreds of sites across the included classes | All of `code/` | The leaf constructor creates the entity; no call site changes. |
| Rules and scenario loading: `Read_INI` chains build the 26 type classes; `Create_One_Of` / `Create_And_Place` (pure virtual on `ObjectTypeClass`) build instances | `code/techtype.cpp`, `code/objtype.h`, per-type `.cpp` | Type entities exist before the first instance asks for its type link. |
| Save loading: `Create_Object(ClassID)` → constructor → `Load()` → `Post_Load()`, with the object self-attaching to its heap during construction | `code/saveload.cpp` (`Load_Object`, `Load_Vector`), `code/classfactory.cpp` | The entity exists from construction, so `Load` failures already have one; the failed-load path destroys the `unique_ptr`, whose destructor must destroy the entity and leave no bridge entry behind. `Swizzler.Abandon(mark)` already rolls back pointer registrations on failure — the entity must need no equivalent rollback beyond destruction. |
| Deferred deletion through `ObjectsToDelete` and `IsActive` checks | `code/object.cpp`, `code/logic.cpp` | Destroying an entity while a query loop or `LogicClass::AI()` is on the stack must not invalidate the loop. §5.5 restricts when destruction is allowed and uses flecs deferred mode around explicit loops. |
| Scenario teardown and restart (`Get_All` tears the scenario down before loading) | `code/saveload.cpp`, scenario code | Every entity of the old scenario is gone before the new one's first object is constructed, or is provably recycled with a new generation. Phase 5 tests a load-after-load cycle. |

Three included leaf classes are **not** registered in
`RegisterClasses()` (`OverlayClass`, `SmudgeClass`,
`VeinholeMonsterClass`). Their class identifiers exist, but saves restore
them through owning records — cells and the map — not through
`Load_Object`. Entity creation therefore cannot live in `Create_Object`;
the constructor is the only hook those three share with every other
included class. This is the deciding argument for §5.2. (Two more
unregistered leaves, `IsometricTileClass` and `LogicClass`, are excluded
anyway, §3.6.)

### 3.4 Persistence, CRC, and network boundaries

Three boundaries constrain every later phase
([Contributing](../CONTRIBUTING.md#compatibility-boundaries)):

- **Saves.** A record is a 16-byte class identifier, a length, the swizzle
  identity, then the members `Serialize` names in header order
  ([Save format](SAVE-FORMAT.md)). Adding a member to a `Serialize` chain
  changes the format. The entity ID is not a member and is never added.
  New components are not saved unless a feature's own change adds them to
  the owning class's `Serialize` deliberately, updates SAVE-FORMAT.md, and
  adds a round-trip case to `tests/save`.
- **Determinism and CRC.** `Object_CRCs()` walks `OBJECT_HEAP_LIST` in a
  fixed order and folds each object's `Compute_CRC`, then the frame
  counter. flecs stores components per archetype, so query iteration order
  follows component-add order and archetype layout — not heap order. Any
  component value that can change a simulation result must enter the CRC
  through the owning leaf class's `Compute_CRC` override, in that class's
  existing position in the chain. A component with no such hook is by
  definition non-simulation data (display, audio, local UI) and must not
  be read by simulation code. Reviewers check this per component (§7).
- **Frame order.** `LogicClass::AI()` processes tags, teams, environment,
  then each submitted object, then factories and houses.
  `LogicClass_AI_Logic_507470()` repeats the walk per concrete heap.
  Component-driven code called from these places inherits their order.
  Component-driven code that iterates a flecs query directly must first
  copy the matching objects into a list and sort by `Fetch_ID()` (heap
  order where an ID is ambiguous), or must document why its result does
  not depend on order. §5.5 makes this a rule with two named patterns.

### 3.5 Build and vendoring

The engine builds only with Visual Studio 2022 MSVC, `Win32` and `x64`,
Debug and Release, C++20, static CRT (`/MT`, `/MTd`), `/fp:precise`
([Building](BUILDING.md)). Third-party code is vendored under
`thirdparty/`, wired in `thirdparty/CMakeLists.txt`, configured to link
statically, and listed in `THIRD_PARTY_NOTICES.md` with its license text
under `thirdparty/licenses/`. Two precedents matter:

- Submodules pinned to a tested tag (`bgfx.cmake`, `SDL`, `RmlUi`,
  `freetype`, `imgui`, `miniaudio`), each with a configure-time
  `FATAL_ERROR` naming the `git submodule update --init --recursive` fix.
- Checked-in sources where upstream ships tarballs (LZO), built directly
  in `thirdparty/CMakeLists.txt` because the upstream build is unusable.

flecs is a C99 core with a C++ API (`flecs.h` / `flecs.hpp`, or the
amalgamated `flecs.c` + headers from a release). It is MIT-licensed. The
plan vendors it as a **submodule pinned to a tested v4.1.x tag** (v4.1.6 is
the latest release at the time of writing; the implementer confirms the
tag still builds on all four configurations before pinning), following the
bgfx/SDL pattern:

- `thirdparty/CMakeLists.txt` gains a `flecs` static library built from
  the C sources as C and the C++ binding as C++, with the add-ons in §1
  disabled, `BUILD_SHARED_LIBS` forced off, and the solution-folder helper
  applied like every other third-party target.
- The macro set flecs is compiled with must be identical for every
  translation unit that includes its headers — the miniaudio and ImGui
  sections of `thirdparty/CMakeLists.txt` already document why: macros
  change struct layout. The `flecs` target therefore exports its compile
  definitions `PUBLIC`, and exactly one engine translation unit
  (`code/flecs/world.cpp`, §5.1) includes `flecs.hpp`. No other engine
  file includes a flecs header. This keeps the PCH-style build
  (`code/always.h` first in most files) and compile times unchanged
  everywhere else.
- Runtime library, `/arch:SSE2`, and `/fp:precise` settings match the
  engine's, as the miniaudio target already does. flecs performs no
  floating-point simulation work in this plan, but the CRT must agree.
- `THIRD_PARTY_NOTICES.md` gains a flecs row (MIT) and
  `thirdparty/licenses/` gains the license text, in the same change.

A checked-in amalgamation is the fallback if the submodule's own CMake
cannot be constrained to the four supported configurations; LZO records
how to take a later release in that case. Choosing between the two is a
Phase 1 task with a build result on all four configurations as evidence —
a build result is not runtime evidence, and Phase 1 claims nothing about
runtime behavior.

### 3.6 Special cases that need their own decision

| Case | Evidence | Decision required |
| --- | --- | --- |
| `CellClass` — 262,144 slots (`MAP_CELL_W × MAP_CELL_H` = 512×512, `code/sun.h`), held as `VectorClass<CellClass*> MapClass::Array`, allocated by `Alloc_Cells`, placement-reconstructed by `Init_Cells`, one global sentinel `BlubCell` | An entity per cell is ~262k entities before any object exists — larger than every other population combined by an order of magnitude. flecs handles this, but the memory and reset cost is unmeasured in this engine. | **Decided: cells get entities, full attach** — every constructed cell, including `BlubCell`. The cost concern does not change the decision; it changes the evidence required. Wave C does not finish until world size, scenario reset time, and frame cost with 262k cell entities are measured against the Phase 0 budget. Placement reconstruction follows the replace rule in §5.3 step 1. |
| `IsometricTileClass` — one per isometric tile in `IsometricTiles`, created from `isotype.cpp` | Same shape as cells, smaller count, but not registered for save creation (§3.3). | **Decided: no entities.** Tiles stay reachable through the map and their type entities (wave B) only. A future feature that needs per-tile data reopens this row with its own justification. |
| `LogicClass` — leaf under `LayerClass`, but the instance is the global `Logic` that drives every frame | Giving the driver an entity is harmless; treating it as a game object is not. It is never saved (`Logic.Save` saves layer membership, not the driver as an object record) and never destroyed. | **Decided: no entity.** The driver is not a game object. The Phase 0 script lists it as excluded so the inventory stays complete. |
| `Tactical` — the tactical-map object, held by the global pointer `TacticalMap`, created once | A normal attach works; it is registered and saved. | **No decision yet.** Phase 0 records it as undecided; the rollout proceeds without it, and its §8 row is resolved before wave D closes. Nothing may depend on a `Tactical` entity until then. |
| Locomotors (10 leaves) — owned as `std::unique_ptr<ILocomotion> FootClass::Locomotion`, created by `Create_Locomotor(ClassID)`, saved as a nested record inside the unit's record | They are not heap objects, have no `AbstractClass` base, and their lifetime is their owner's. A top-level entity per locomotor would outlive nothing and be discoverable by queries that expect game objects. | **Decided: locomotors get entities**, modeled as a **child entity** of the owner's entity (`ChildOf` pair), created in the locomotor constructor and destroyed in `~LocomotionClass`. Queries for game objects never match children unless a feature asks for the pair explicitly. |
| `AnimClass` — `Anims` heap, 109 `new` sites | The highest-churn class in the engine; animations are short-lived visual effects. | **Decided: no entity.** Animations carry no new-feature state under this plan; an effect that needs data attaches it to its owner (`ObjectClass` with `IsAnimAttached`) or its type. Reopens only with a named feature. |
| `TActionClass`, `TEventClass` — global heaps `Actions`, `Events`, and intrusive lists from `TriggerTypeClass::FirstAction/FirstEvent` | Dual ownership (heap plus list) already exists; the entity would add a third view and own nothing. Trigger behavior is driven through `TriggerClass`/`TriggerTypeClass`, which do get entities. | **Decided: no entities.** Actions and events stay plain records owned by their trigger type. |
| High-churn leaves that do get entities — `BulletClass`, `ParticleClass`, `ParticleSystemClass`, `VoxelAnimClass`, `WaveClass` | Entity create/destroy per effect per frame is the main performance risk. flecs recycles IDs, but the cost in this engine is unmeasured. `BulletClass` is included by decision despite the churn: bullets are gameplay objects (damage, payback), unlike animations. | Wave C starts only after the Phase 2 benchmark (spawn/destroy 10k bullets and particles per frame in the test harness) meets the budget set at Phase 0: no measurable frame-time change in the `_bench` harness on a populated map. If it fails, these classes attach lazily (entity created on first component access) and the table records the change. |
| Type classes (26 leaves) — INI-defined prototypes, lifetimes span a scenario, `Create_One_Of` builds instances | The type's entity is the natural home for new type-level data (§7). Instance-to-type is currently `Class_Of()`. | Full attach in wave B. Instances link to the type entity with a `TypeOf` pair (§5.4); nothing copies type data into instances automatically. |

## 4. Decisions to record before code

Phase 0 ends with these decisions written into this page (or a linked
maintainer decision) — not left in a chat log:

- **D1 — Scope (decided).** The §8 table is the scope: 65 classes get
  entities, 5 are excluded (`AnimClass`, `IsometricTileClass`,
  `LogicClass`, `TActionClass`, `TEventClass`), and `Tactical` is
  undecided. The derivation script and its test keep the inventory
  current; an exclusion is a row in the table, not a missing class.
- **D2 — No observers.** §1's observer ban holds through Phase 3. Revisit
  only with a determinism argument.
- **D3 — Entity storage.** The raw entity ID lives on `AbstractClass`
  (and on `LocomotionClass`), as §5.2 describes. The alternative — a
  side map from object pointer to entity inside the registry — is the
  fallback if a layout review objects. The side map costs a hash lookup
  on every access and a second lifetime to keep in sync; the member
  costs 8 bytes per object and one definition-only header. Neither is
  serialized.
- **Cells and locomotors (decided).** Full attach for `CellClass`;
  child entities for locomotors (§3.6). Only the cell cost measurement
  remains open, as a wave C exit check, not as a decision.
- **D5 — `Tactical` (open).** Decide before wave D closes whether the
  tactical-map object gets an entity. The plan does not depend on the
  answer.
- **D4 — First component.** Phase 2 needs one real new feature to prove
  the path end to end (INI → type entity → instance entity → explicit
  call site → test). The feature is chosen by maintainers; this plan
  does not invent one. A marker component used only by tests proves
  storage, not the path.

## 5. Target design

### 5.1 Layout of the new code

The engine is flat: most of `code/` is one directory, subdirectories
exist for `audio`, `ui`, `sdl`, `language`, `vqalib`. The EC bridge is
one new subdirectory, because its whole purpose is to be the only place
that includes flecs headers:

```text
code/flecs/
  id.hh         — definition-only: EntityId (uint64_t) and the invalid
                    zero sentinel. Includable from abstract.h without
                    pulling in flecs. Follows the .hh rule in
                    docs/STYLE.md: no static initialization, no
                    nontrivial inline code.
  lifecycle.hh  — C++20 concepts, SimPhase, TickScope, EntityContext,
                    and ComponentOps trait generation for component
                    lifecycle dispatch (docs/COMPONENT-LIFECYCLE.md).
  world.h       — FlecsWorldClass: the EC layer as one class. Declares
                    attach, destroy, parenting, component storage, and
                    lifecycle dispatch methods. No flecs type appears in
                    the header.
  world.cpp     — implements FlecsWorldClass. The only translation unit
                    that includes flecs.h; the flecs::world is held
                    behind a void * and created lazily on first attach.
  components.hh — bridge link structs and component structs for new
                    features, data only (grows one struct per feature,
                    §7).
  register.cpp  — FlecsWorldClass::Register_Tags: registers every class
                    tag with the world, explicitly, in one function. No
                    static initializers register components.
code/_flecs.cpp — the one constinit global FlecsWorld object, beside the
                    other engine globals in underscore-prefixed init
                    files.
```

`code/CMakeLists.txt` gathers sources by recursive glob, so the new
directory builds without a CMake change there; the link change is one
line adding `flecs` to `OpenTS`'s libraries, in the same change that
adds the third-party target.

Naming follows [Style](STYLE.md): classes `PascalCase` ending in
`Class` where they are classes, functions `Pascal_With_Underscores`,
`const` to the right, tabs, centered pointer declarators. Component
structs are not classes in the engine sense; they are named for the data
(`struct VeterancyGate` style names come from the feature, not from this
plan) and live in `components.hh` or a feature's own definition
header.

### 5.2 The entity handle on the object

`AbstractClass` gains one member, declared in `abstract.h` through
`id.hh` only:

```cpp
EntityId Entity;   // 0 until the leaf constructor attaches; never saved.
```

- It is not added to `AbstractClass::Serialize`, so the save format does
  not change. It is not folded into `Compute_CRC`. It is not sent over
  the network. Class layout changes (every game object grows by 8 bytes
  plus alignment), which is an internal change: no serialized, COM, or
  network structure contains an `AbstractClass` by value. Phase 2 states
  this classification in its change record, with `sizeof` checks in the
  test harness for the pilot classes.
- `LocomotionClass` gains the same member through the same header.
- Access is through the `FlecsWorld` object, so the
  raw value never leaves `code/flecs/` except as an opaque ID.

### 5.3 Attach and destroy

`FlecsWorldClass` does the work; the engine holds one global object,
`FlecsWorld`, defined in `code/_flecs.cpp`. The class name is passed as a
string, so the entity receives the correct class tag without a virtual
call during construction (the dynamic type is not yet the leaf while
base constructors run — which is also why attach cannot live in
`AbstractClass`'s constructor):

```cpp
// In each leaf constructor, beside the existing heap registration:
Entity = FlecsWorld.Replace_Entity(Entity, this, "UnitClass");

// In ~AbstractClass() and ~LocomotionClass(), the only destroy call:
FlecsWorld.Destroy_Entity(Entity);
```

`Replace_Entity`:

1. Destroys the entity the object already holds, if any, then attaches a
   fresh one. That makes the placement reconstruction of `CellClass`
   (§3.2) safe without a destructor call, and makes a doubled attach in
   one constructor replace rather than duplicate.
2. Creates the flecs world on first use, inside
   `FlecsWorldClass::Ensure_World`. The `FlecsWorld` constructor
   deliberately does not reset its members: static storage has already
   zeroed them, and the `CellClass BlubCell` sentinel, which attaches
   before `main` and possibly before `FlecsWorld` is constructed, has
   already created the world through them. `Shutdown` is called from
   `Prog_End`, and attach recreates the world on demand, so a global
   destroyed after shutdown cannot crash on it.
3. Creates the entity, adds the tag registered for `T`, and sets the one
   infrastructure component every entity carries:

   ```cpp
   struct LegacyObjectLink { AbstractClass * Object; };  // bridge only
   ```

   `LegacyObjectLink` is the single exception to "components only for
   new functionality": it holds no game state, duplicates no member, and
   exists so a query result can reach the C++ object. Locomotor children
   carry the matching `LegacyLocomotionLink`. Both are registered by
   `Ensure_World`, are never saved, and are never read by simulation
   code except to resolve the object.
4. The `TypeOf` pair of §5.4 is not added yet; it lands with the first
   feature that needs it, from the place each constructor sets its type.

`Destroy_Entity` destroys the entity (which removes all its
components and pairs) and tolerates a 0 or a missing world. It runs from
the base destructor, after every heap and tracker removal in the leaf
destructor has already happened, so no legacy code can observe a
half-destroyed pairing.

World reset between scenarios needs no separate sweep: scenario teardown
already `delete`s every heap object, and each destructor destroys its
entity. `FlecsWorld.Reset` exists for tests and for checking that a
teardown left no live entity behind (`Live_Entity_Count`).

### 5.4 Class tags, type links, and prefabs

- Each final class registers one empty tag component, named after the
  class (`UnitTag`, `BuildingTag`, …), in `register.cpp`. Tags let a
  feature ask "all entities whose object is a `UnitClass`" without a
  C++ cast, and they make the Phase 0 inventory testable: the test
  creates one instance per registered class (where a test constructor
  is practical) and asserts exactly one tag.
- Each instance entity carries `(TypeOf, <type entity>)` once its type
  is known. The type entity is the entity of the `Class_Of()` object
  itself (type classes are in scope, §2). This replaces no existing
  call: `Class_Of()` stays the way to reach type *members*; the pair is
  how a *component query* narrows by type.
- Prefabs are deferred. flecs `IsA` component inheritance would copy
  type components onto instances automatically, which is a behavior
  decision per component (copy at spawn? live view of the type?).
  §7's default is an explicit copy at spawn in the leaf's
  `Create_One_Of`; adopting `IsA` for a component is recorded on that
  component's row when a feature needs live type data.

### 5.5 Iteration without systems: two patterns

**Pattern A — heap-driven (default, required for simulation).** Existing
code iterates the existing heap (`Units`, `Buildings`, the `Logic`
layer, …) in its current order and asks the bridge for the component:

```cpp
for (int i = 0; i < Units.Count(); i++) {
    UnitClass * unit = Units[i];
    if (SomeComponent const * c = Get_Component<SomeComponent>(unit)) {
        // new-feature behavior for this unit, in heap order
    }
}
```

Order, skipping, and mid-loop deletion behavior are exactly what the
surrounding code already has. This is the only pattern allowed to
change a simulation result.

**Pattern B — query-driven (non-simulation, or sorted).** Code that
needs "every entity with components X and Y" and does not affect the
simulation (rendering helpers, debug overlays, editor-style tools) may
iterate a flecs query directly. Code that *does* affect the simulation
may use a query only by first collecting the matching objects into a
`DynamicVectorClass<AbstractClass*>`, sorting by `Fetch_ID()`, and then
working from the list. The sort is the price of leaving heap order; it
is paid visibly at the call site, not hidden in the bridge.

Creation and destruction during either pattern go through flecs
deferred mode scoped to the loop (`Defer_Changes` RAII in
`world.h`), mirroring the `ObjectsToDelete` discipline the engine
already uses. No component callback runs during the loop, because there
are no observers (§1).

### 5.6 What the bridge deliberately does not offer

- No `Set_Component` that simulation code can call during another
  object's `AI()` without the deferred scope — the API makes the scope
  the easy path.
- No string-based component or entity lookup in shipped code (no
  `world.lookup`, no flecs script). Names exist only as Debug entity
  names (RTTI name + abstract ID, e.g. `UnitClass#1042`) to make a
  future flecs-explorer session useful; Release builds may omit them.
- No entity persistence across scenarios. A component that must survive
  a save does so through the owning class's `Serialize` (§3.4), and the
  loaded object gets a fresh entity whose components are filled from
  the loaded members by the feature's own load code.

## 6. Compatibility rules (apply to every phase)

| Boundary | Rule |
| --- | --- |
| Save format | Entity IDs and tags are never serialized. A component is saved only by adding its fields to the owning leaf class's `Serialize`, in declaration order, with SAVE-FORMAT.md updated and a `tests/save` case added in the same change. Old saves must load with the component absent (default value), or the change is classified intentionally changed with migration steps. |
| CRC / desync | Entity IDs, tag presence, and archetype layout are never folded into `Compute_CRC`. A simulation-affecting component value is folded in by the owning leaf's `Compute_CRC` override, or the component is non-simulation by construction and reviewed as such. `tests/desync` and `tests/syncrec` must stay green without modification in Phases 1–3. |
| Network | Nothing flecs-related enters a packet. Peers assign entity IDs independently. |
| INI / mod data | Existing keys and defaults do not change. A new key is read only by the new feature's code (§7), from the type class's `Read_INI` after the `BASECLASS::Read_INI` chain, so an absent key leaves the component absent — not zero-filled and different. |
| Determinism of order | Pattern A by default; Pattern B only sorted or non-simulation (§5.5). No unordered-container iteration over entities in simulation code. |
| Class layout | The one added member (§5.2) is the only layout change to existing classes. No member moves, is reordered, or changes type. |
| Build | All four configurations (Win32/x64 × Debug/Release) build before a phase is called done. A build result is not runtime evidence; runtime claims need the play tests in §9. |

## 7. How a new feature adds a component (the only way components appear)

After Phase 3, adding a component is a checklist, executed in the
feature's own change — never as drive-by additions to `components.hh`:

1. Define a plain struct in `components.hh` (or the feature's
   definition header). Data only: no virtual functions, no behavior, no
   pointers to game objects except entity IDs or the existing object
   pointers the feature already uses. Say in one comment what the data
   means and what absence of the component means.
2. Register it in `register.cpp`. Registration is explicit and
   ordered; no static initializers.
3. Decide and record, in the feature's change record: saved or not
   (§6 save rule), simulation-affecting or not (§6 CRC rule), read from
   INI or set at runtime. "Not decided" means the component does not
   merge.
4. Type-level data: parse new INI keys in the leaf type's `Read_INI`,
   store the component on the type's entity. Instance data: copy or
   compute it in the leaf's `Create_One_Of` (explicit copy is the
   default; `IsA` only by recorded decision, §5.4).
5. Instance runtime data: set it from the existing call site that owns
   the behavior (an `AI()` override, a damage function, a placement
   function) — by name, under Pattern A unless §5.5 allows B.
6. Add a case to `tests/flecs` (or the feature's harness) covering
   add/read/remove, absence, save round-trip if saved, and CRC effect
   if simulation-affecting.
7. Update the manual page and lifecycle record for the feature, as
   [Contributing](../CONTRIBUTING.md#documentation) already requires
   for player- or modder-visible changes.

Worked shape (names are illustrative, not a proposed feature): a
mod-facing gate that lets a new INI key suppress a behavior per unit
type would be one `struct` on `UnitTypeClass` entities, parsed in
`UnitTypeClass::Read_INI`, copied in `UnitTypeClass::Create_One_Of`,
and read in `UnitClass::AI` under Pattern A. No existing member moves;
units whose type lacks the component behave exactly as before.

## 8. Rollout table (all 71 candidate classes)

Waves: **P** pilot (Phase 2) · **B** types · **C** object effects and
cells · **D** remaining abstracts · **E** locomotors · **✗** excluded by
decision · **?** no decision yet (§3.6 records each).
"Registered" = present in `RegisterClasses()` today.

| Class | Heap / storage | Registered | Wave | Note |
| --- | --- | --- | --- | --- |
| `HouseClass` | `Houses` | yes | P | Few instances, long-lived; safest pilot. |
| `UnitClass` | `Units`, `Technos` | yes | P | Pilot techno; `Create_One_Of` from `UnitTypeClass`. |
| `BuildingClass` | `Buildings`, `Technos` | yes | P | Extra side lists (factories, repair) already in ctor/dtor. |
| `InfantryClass` | `Infantry`, `Technos` | yes | P | Completes the techno pilot set with `AircraftClass`. |
| `AircraftClass` | `Aircraft`, `Technos` | yes | P | Only leaf also implementing `IFlyControl`. |
| `AITriggerTypeClass` | `AITriggerTypes` | yes | B | |
| `AircraftTypeClass` | `AircraftTypes` | yes | B | Type entity for `AircraftClass`. |
| `AnimTypeClass` | `AnimTypes` | yes | B | |
| `BuildingTypeClass` | `BuildingTypes` | yes | B | |
| `BulletTypeClass` | `BulletTypes` | yes | B | |
| `CampaignClass` | `Campaigns` | yes | B | `AbstractTypeClass` leaf, not an `ObjectTypeClass`. |
| `HouseTypeClass` | `HouseTypes` | yes | B | |
| `InfantryTypeClass` | `InfantryTypes` | yes | B | |
| `IsometricTileTypeClass` | `IsometricTileTypes` | yes | B | |
| `OverlayTypeClass` | `OverlayTypes` | yes | B | |
| `ParticleSystemTypeClass` | `ParticleSystemTypes` | yes | B | |
| `ParticleTypeClass` | `ParticleTypes` | yes | B | |
| `ScriptTypeClass` | `ScriptTypes` | yes | B | |
| `SideClass` | `Sides` | yes | B | |
| `SmudgeTypeClass` | `SmudgeTypes` | yes | B | |
| `SuperWeaponTypeClass` | `SuperWeaponTypes` | yes | B | |
| `TagTypeClass` | (type heap via `Tags` family) | yes | B | Confirm owning heap in Phase 0 audit. |
| `TaskForceClass` | `TaskForces` | yes | B | Two constructors. |
| `TeamTypeClass` | `TeamTypes` | yes | B | |
| `TerrainTypeClass` | `TerrainTypes` | yes | B | |
| `TiberiumClass` | `Tiberiums` | yes | B | Type-like leaf directly under `AbstractClass`; growth state stays in the class. |
| `TriggerTypeClass` | `TriggerTypes` | yes | B | Owns `FirstEvent`/`FirstAction` lists. |
| `UnitTypeClass` | `UnitTypes` | yes | B | |
| `VoxelAnimTypeClass` | `VoxelAnimTypes` | yes | B | |
| `WarheadTypeClass` | `Warheads` | yes | B | |
| `WeaponTypeClass` | `Weapons` | yes | B | |
| `AnimClass` | `Anims` | yes | ✗ | Excluded (§3.6): highest churn, no new-feature state. |
| `BuildingLightClass` | `BuildingLights` | yes | C | |
| `BulletClass` | `Bullets` | yes | C | Included by decision (§3.6); high churn, benchmark gate. |
| `IsometricTileClass` | `IsometricTiles` | **no** | ✗ | Excluded (§3.6). Type entity (wave B) only. |
| `OverlayClass` | `Overlays` | **no** | C | Restored through cells; ctor hook is the only common path. |
| `ParticleClass` | `Particles` | yes | C | High churn; benchmark gate. |
| `ParticleSystemClass` | `ParticleSystems` | yes | C | |
| `SmudgeClass` | `Smudges` | **no** | C | As `OverlayClass`. |
| `TerrainClass` | `Terrains` | yes | C | |
| `VeinholeMonsterClass` | heap via `Update_All` list | **no** | C | Confirm owning heap in Phase 0 audit. |
| `VoxelAnimClass` | `VoxelAnims` | yes | C | High churn; benchmark gate. |
| `WaveClass` | `Waves` | yes | C | |
| `AlphaShapeClass` | (spawned per object) | yes | D | Two constructors. |
| `CellClass` | `MapClass::Array` (262,144) | yes | C | Full attach decided (§3.6), incl. sentinel `BlubCell`; cost measurement is a wave C exit check. |
| `EMPulseClass` | `EMPulseClass::EMPulses` | yes | D | Static heap member. |
| `FactoryClass` | `Factories` | yes | D | Also `private StageClass`; attach unchanged. |
| `FoggedObjectClass` | `FoggyObjects` | yes | D | Five constructors — audit each. |
| `LightSourceClass` | `LightSources` | yes | D | |
| `LogicClass` | global `Logic` | **no** | ✗ | Excluded (§3.6): the frame driver is not a game object. |
| `NeuronClass` | brain-internal | yes | D | |
| `ScriptClass` | `Scripts` | yes | D | |
| `SuperClass` | `SuperWeapons` | yes | D | The superweapon instance (type is wave B). |
| `TActionClass` | `Actions` + trigger lists | yes | ✗ | Excluded (§3.6). |
| `TEventClass` | `Events` + trigger lists | yes | ✗ | Excluded (§3.6). |
| `Tactical` | global `TacticalMap` | yes | ? | No decision yet (§3.6, D5). Row resolved before wave D closes. |
| `TagClass` | `Tags`, `MapTags`, `LogicTags` | yes | D | One object can sit in several lists; one entity. |
| `TeamClass` | `Teams` | yes | D | |
| `TriggerClass` | `Triggers` | yes | D | |
| `TubeClass` | `Tubes` | yes | D | |
| `WaypointPathClass` | `WaypointPaths` | yes | D | |
| `DriveLocomotionClass` | `FootClass::Locomotion` | yes | E | Child entity (§3.6). |
| `DropPodLocomotionClass` | `FootClass::Locomotion` | yes | E | Registered as `ClassID_BallisticLocomotion`. |
| `FlyLocomotionClass` | `FootClass::Locomotion` | yes | E | |
| `HoverLocomotionClass` | `FootClass::Locomotion` | yes | E | |
| `JumpjetLocomotionClass` | `FootClass::Locomotion` | yes | E | |
| `LevitateLocomotionClass` | `FootClass::Locomotion` | yes | E | |
| `MechLocomotionClass` | `FootClass::Locomotion` | yes | E | |
| `TeleportLocomotionClass` | `FootClass::Locomotion` | yes | E | |
| `TunnelLocomotionClass` | `FootClass::Locomotion` | yes | E | |
| `WalkLocomotionClass` | `FootClass::Locomotion` | yes | E | |

## 9. Phases

Each phase is one or more focused changes. Mechanical changes (vendoring,
the inventory script) stay separate from behavior changes, per
[Contributing](../CONTRIBUTING.md#pull-request-workflow). No phase
changes externally visible behavior; every phase is classified
**preserved** unless its own change record says otherwise. The bridge is
behind one build-time switch (`OPENTS_EC`, default on once Phase 2
lands; off compiles the bridge to no-ops) so any phase can be backed out
without touching call sites.

### Phase 0 — Inventory and decisions (no engine code)

- Add the derivation script (classes, bases, leaves, heaps, registration,
  constructor/destructor counts) and check its output against §8.
  Record corrections in §8, not in the script alone.
- Audit, per class in §8: every constructor and destructor, every heap
  it joins, the save path (registered, owning-record, or never saved),
  and where `Class_Of()` becomes valid.
- Record D1–D5 (§4). Set the Phase 2
  performance budget (frame-time and memory) with a maintainer.
- Exit: §8 verified line by line; decisions written down; a maintainer
  has agreed the scope in the project's usual channel before Phase 1
  starts.

### Phase 1 — Vendor flecs, empty bridge, test harness

- Vendor flecs per §3.5 (submodule at a pinned v4.1.x tag, static
  library in `thirdparty/CMakeLists.txt`, notices and license).
- Add `code/flecs/` with the world created lazily and destroyed by an
  explicit shutdown called from the game's exit path; no class attaches
  yet. `OPENTS_EC` switch added, default on.
- Add `tests/flecs` using `opents_add_test`, following the existing
  harnesses: world create/reset/shutdown, entity create/destroy,
  component set/get/remove, tag query, child entities, deferred scope.
  No game assets required, as with every current test.
- Exit: all four configurations build; `ctest` passes including the new
  harness; no engine behavior change (bridge unused). Docs updated:
  BUILDING.md dependency list, THIRD_PARTY_NOTICES.md, this page's
  status line.

### Phase 2 — Pilot: five classes, one real component

- Add the `Entity` member to `AbstractClass` (§5.2), attach/destroy (§5.3),
  tags and `TypeOf` for the pilot set: `HouseClass`, `UnitClass`,
  `BuildingClass`, `InfantryClass`, `AircraftClass` (§8 wave P), with
  their type classes attached as needed for the pair.
- Implement the maintainer-chosen first feature (D4) end to end by the
  §7 checklist — INI on the type entity, copy in `Create_One_Of`,
  Pattern A read at the behavior's existing call site.
- Benchmark in the test harness: entity churn (10k spawn/destroy cycles
  of pilot classes), world memory at pilot scale, query-vs-heap
  iteration cost. Compare against the Phase 0 budget.
- Exit: save → load round trip of a game containing pilot objects is
  unchanged (old save loads, new save loads, `tests/save` green);
  `tests/desync`/`tests/syncrec` green unmodified; CRC of a fixed
  scenario identical with `OPENTS_EC` on and off; the feature works
  with the component present and is byte-identical in behavior with it
  absent.

### Phase 3 — Roll out by wave

- Wave B (26 type classes), then wave D (remaining abstracts), then
  wave C (object effects and cells) behind the Phase 2 benchmark result,
  then wave E (locomotor children). Cells attach in full in wave C; the
  262k-entity cost measurement is that wave's exit check (§3.6).
- One wave per change where practical; a wave touches constructors,
  `register.cpp`, and the §8 table only — no behavior changes ride
  along. Each wave re-runs the Phase 2 exit checks.
- Exit per wave: §8 rows for the wave verified against the Phase 0
  script (every constructor attaches exactly once; base destructor
  destroys exactly once, including the failed-load path); full `ctest`;
  a campaign mission, a skirmish, and a save/load cycle played on both
  `Win32` and `x64` builds.

### Phase 4 — Steady state for new functionality

- §7 becomes the standing route for new components. This page is
  updated to describe current behavior (the bridge as built, not as
  planned), and DIRECTION.md links here as the owner of the mechanism.
- The `OPENTS_EC=OFF` no-op build is re-evaluated: keep it only while
  a supported configuration needs it.
- Exit: documentation describes what shipped; the Phase 0 script runs
  in the test suite (`toolkitheaders`-style, compiler-free) so a new
  final class without a §8 row fails `ctest`.

### Phase 5 — Hardening (overlaps Phase 3–4)

- Load-after-load and failed-load cycles under the test harness with
  entity-leak assertions (live `LegacyObjectLink` count returns to
  zero at scenario teardown).
- Long-session churn test at wave C scale; memory and frame time within
  the Phase 0 budget on a populated map.
- LAN multiplayer session on mixed component presence is **not** a
  supported case: peers must run the same build, as today
  ([Building](BUILDING.md#save-and-network-compatibility-between-the-platforms)).
  The check is that identical builds stay in sync, via `tests/desync`
  plus a played session.

## 10. Risks

| Risk | Where it lands | Mitigation in this plan |
| --- | --- | --- |
| Entity ID leaks into a save, packet, or CRC during a later feature | Silent desync or broken old saves | §6 rules, Phase 2 CRC on/off comparison, §7 step 3 review gate. |
| Attach missed in one constructor overload (`FoggedObjectClass` has five) | Object with entity 0; feature silently skips it | Idempotent attach asserted in Debug on first component access; Phase 0 script counts constructors; rollout exit check per wave. |
| Double destroy (deferred deletion plus explicit `delete`) | flecs destroying a recycled entity owned by a new object | Destroy only from the base destructor; member cleared to 0; generation bits make a stale destroy a no-op mismatch the bridge checks in Debug. |
| Query order changes a simulation result | Desync that single-player testing misses | Pattern A default; Pattern B sorted; §3.4 CRC rule per component. |
| Cell entity population (262k) blows up memory or reset time | Scenario load regression | Full attach is decided; wave C cannot close until world size and reset time are measured within the Phase 0 budget (§3.6). |
| flecs header macros diverge between translation units | Layout mismatch, heap corruption | One including TU (§3.5); definitions exported `PUBLIC` from the target. |
| Static-init/destruction order with pre-`main` globals (`Logic`, `BlubCell`) | Crash before startup or at exit | Lazy world creation, intentional leak if shutdown is skipped, no flecs statics in engine code (§5.3). |
| flecs submodule tag breaks a supported configuration | Build failure on one of four configurations | Pin only after all four build (Phase 1 exit); amalgamation fallback (§3.5). |
| A later contributor adds a flecs system "just for this feature" | Rule 1 erodes invisibly; frame order becomes registration order | The bridge exposes no system API; §1 table is the review reference; adding one requires replacing this plan's rule, stated in the change. |

## 11. Verification summary

Per change, the narrowest checks first, per
[Contributing](../CONTRIBUTING.md):

- Build: `Win32` and `x64`, Debug and Release, from a fresh configure
  (commands in [Building](BUILDING.md)). This sandbox cannot run them —
  it has no MSVC — so every phase's build claim waits for a Windows
  build, local or CI (`engine.yml`).
- Tests: `ctest`, with `tests/flecs` added in Phase 1 and extended per
  §7; `tests/save`, `tests/desync`, `tests/syncrec`, `tests/spawner`
  and `tests/spawnhouse` are the regression net for Phases 2–3.
- Runtime: the played checks named in each phase's exit criteria
  (campaign mission, skirmish, save/load, LAN where stated). A build
  result is never presented as runtime evidence.
- Performance: the `_bench` harness and the `tests/flecs` churn
  benchmark, against the Phase 0 budget, before wave C.

## 12. Open questions for maintainers

1. D3: is the 8-byte member on `AbstractClass` acceptable, or should
   Phase 2 prototype with the side map first?
2. D5: does `Tactical` get an entity? Decide before wave D closes; the
   rollout does not depend on the answer.
3. D4: which real feature is the Phase 2 vertical slice?
4. Observers (D2): is the ban permanent, or revisited once Pattern A/B
   usage shows where callbacks would actually help?
5. Should Debug builds name entities (RTTI + ID) for a future flecs
   explorer session, accepting the string cost in Debug only?

Decided and closed: cell entities (full attach), no entities for
`AnimClass`, `IsometricTileClass`, `LogicClass`, `TActionClass`, or
`TEventClass`, `BulletClass` included, and child entities for
locomotors (§3.6, §4). Reopening any of these takes a named feature as
justification, recorded in §8.
