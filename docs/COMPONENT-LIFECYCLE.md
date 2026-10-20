# Component lifecycle and engine call sites

Status: architecture analysis and design options for adding component lifecycle
callbacks (`Ready`, `Process`, world entry and exit, reference detachment,
rendering, persistence, and checksums) on top of the `arena/ec066f46-opents`
branch. Nothing here changes engine behavior until a maintainer selects an
execution model and scopes an implementation phase under
[Contributing](../CONTRIBUTING.md#before-starting).

[Project direction](DIRECTION.md) defines the long-term move toward an
entity-component model. [Entity-component adoption plan](ENTITY-COMPONENT-PLAN.md)
owns the storage rollout across the 65 included leaf classes and its
compatibility rules. [The saved game format](SAVE-FORMAT.md) owns record framing
and pointer swizzling. This document owns the engine call sites, ordering
invariants, and dispatch options required when components expose lifecycle
functions comparable to modern entity-component engines.

## 1. Baseline findings on the current branch

Auditing the live `code/` tree against
[Entity-component adoption plan](ENTITY-COMPONENT-PLAN.md) turns up five concrete
discrepancies that affect any component lifecycle implementation. Resolve items
1 through 3 before running a full Windows build or wiring component callbacks.

| Area | Verified state in `code/` | Effect on component lifecycle |
| --- | --- | --- |
| `AbstractClass::Entity` declaration | `code/abstract.h:44` includes `flecs/id.hh`, while `AbstractClass` omits `EntityId Entity;`. `code/abstract.cpp:61,72-73` and all 55 `AbstractClass` leaf constructors already reference `Entity`. | Compilation of `code/abstract.cpp` and every included `AbstractClass` leaf fails on MSVC until `public: EntityId Entity;` is declared on `AbstractClass`. |
| Missing header in `abstract.cpp` | `code/abstract.cpp:72` calls `FlecsWorld.Destroy_Entity(Entity)` without `#include "flecs/world.h"`. | `code/abstract.cpp` fails to compile because `FlecsWorld` is undeclared in that translation unit. |
| Locomotor parenting and piggybacking | `LocomotionClass::Link_To_Object` (`code/loco.cpp:70`) assigns `LinkedTo` only; `FlecsWorld.Set_Entity_Parent` (`code/flecs/world.cpp:135`) has zero call sites in `code/`. In addition, `FootClass` can stack temporary locomotors through `IPiggyback` (`code/foot.cpp:3319,3598`, `code/aircraft.cpp:1362`, `code/building.cpp:6369`, `code/infantry.cpp:1199,4157`, `code/unit.cpp:5428`). | Locomotor entities remain unparented, and reparenting must handle temporary piggyback swap-in and restoration rather than assuming one permanent locomotor per `FootClass`. |
| Unreferenced logic routines | `LogicClass_AI_Logic_507470()` (`code/logic.cpp:490`) and `LogicClass::Environment_AI()` (`code/logic.cpp:421`) have zero callers anywhere in `code/`. Only `LogicClass::AI()` (`code/logic.cpp:231`) runs from `Main_Loop()` (`code/mainloop.cpp:325`). | Placing any component `Process` call inside `LogicClass_AI_Logic_507470` or `Environment_AI` leaves it dead at runtime. |
| Checksum coverage gaps | `Object_CRCs()` (`code/saveload.cpp:327`) walks only `OBJECT_HEAP_LIST` (`code/objheaps.hh:15`). Twenty of the 65 entity-bearing classes sit outside `OBJECT_HEAP_LIST`, and `FootClass::Compute_CRC` (`code/foot.cpp:3656`) folds nothing from `Locomotion`. | Simulation-affecting components on cells, overlays, smudges, waves, veinhole monsters, neurons, or any of the 10 locomotors cannot rely on a leaf `Compute_CRC` override alone to reach network desync detection. |

The 20 included entity classes omitted from `OBJECT_HEAP_LIST` are:
`AlphaShapeClass`, `CampaignClass`, `CellClass`, `FoggedObjectClass`,
`IsometricTileTypeClass`, `NeuronClass`, `OverlayClass`, `SmudgeClass`,
`VeinholeMonsterClass`, `WaveClass`, and all 10 `LocomotionClass` subclasses
(`DriveLocomotionClass`, `DropPodLocomotionClass`, `FlyLocomotionClass`,
`HoverLocomotionClass`, `JumpjetLocomotionClass`, `LevitateLocomotionClass`,
`MechLocomotionClass`, `TeleportLocomotionClass`, `TunnelLocomotionClass`, and
`WalkLocomotionClass`).

## 2. Canonical component lifecycle callbacks

Modern component-based engines split object lifetime into distinct stages
because allocation, configuration, world activation, fixed-step simulation,
variable-rate rendering, reference invalidation, and persistence run at
different times under different invariants. OpenTS enforces the same split for
concrete engine reasons.

| Lifecycle callback | Modern engine counterpart | When it runs in OpenTS | What the component can safely read or mutate |
| --- | --- | --- | --- |
| `On_Construct` | Godot `_init`, Unity `Awake` (allocation part), Unreal `UActorComponent` ctor | Leaf C++ constructor right after `FlecsWorld.Replace_Entity` (`code/unit.cpp:224` and peers) | Own default values only. Cross-object pointers, coordinates, rules overrides, and save data are incomplete or absent; pre-`main` globals (`BlubCell`) have no `Scen` or `Rule`. |
| `On_Read_INI` | Unreal `PostInitProperties` / data asset load | Type `Read_INI` chain (`RulesClass::Initialize`, `RulesClass::Addition`, scenario `.INI` and `MISSION.INI` passes) | `CCINIClass` keys for the owning section. Must be idempotent across repeated additive INI passes (`code/scenario.cpp:1861,1885,2103,2142`). |
| `On_Enter_World` | Godot `_enter_tree`, Unity `OnEnable` | Leaf tail of `Unlimbo()` (`ObjectClass::Unlimbo` chain) when `IsInLimbo` transitions from `true` to `false` | Valid world `Position`, cell occupancy, `House`, `Class_Of()`, `Logic` and `Map` membership. Can run multiple times across transport, carryall, or hospital cycles. |
| `On_Ready` | Godot `_ready`, Unity `Start`, Unreal `BeginPlay` | Live gameplay (`ScenarioInit == 0`): immediately after first `On_Enter_World` (or owner activation for non-spatial classes). Scenario load (`ScenarioInit > 0`): flushed in deterministic ID order right after `Read_Scenario_INI` finishes `Swizzler.Resolve()` (`code/scenario.cpp:2179`). | Full initial world state, final scenario INI overrides, resolved `Tag` and `Team` links, type entities, and locomotor child links. Runs at most once per object lifetime; skipped on save load in favor of `On_Post_Load_Resolve`. |
| `On_Post_Load_Resolve` | Unreal `PostLoad` | Inside `Post_Load_Game()` (`code/scenario.cpp:1019`), called from `Load_Game()` (`code/saveload.cpp:1195`) after `Swizzler.Resolve()` (`line 1189`) | All swizzled C++ pointers (`Class`, `House`, `TarCom`, `NavCom`, `CargoHold`, `LinkedTo`), map cells, and restored `Serialize` fields. Rebuilds non-saved links and visual dirty flags without resetting saved timers or counters. |
| `On_Exit_World` | Godot `_exit_tree`, Unity `OnDisable` | Inside `Limbo()` while `!IsInLimbo`, prior to spatial cell and `Logic` removal | World coordinates and owner pointers valid prior to limbo transition. Must distinguish temporary limbo (`IsActive == true`, such as entering `CargoClass`) from permanent removal (`Delete_Me()`). |
| `Simulation_Process` | Godot `_physics_process`, Unity `FixedUpdate`, Unreal `TickComponent` | Deterministic lockstep tick inside `LogicClass::AI()` (`code/logic.cpp:231`), once per `Frame` | Full simulation state, shared `Scen->RandomNumber` RNG, spawning, damage, orders, and deferred deletion. Never runs during `Sync_Delay()` frame pacing. |
| `Render_Process` | Godot `_process` / `_draw`, Unity `Update` / `LateUpdate` | Tactical display passes inside `GScreenClass::Render()` (`code/gscreen.cpp:386`) and `Tactical::Render()` (`code/tactical.cpp:1066`) | Presentation surfaces, local interpolation, HUD state, and local audio triggers only. Strictly forbidden from mutating simulation state, object heaps, or shared `Scen->RandomNumber`. |
| `On_Detach` | Weak-handle invalidation / Unreal actor end-play target clear | `Detach_This_From_All(target, all)` (`code/tracker.cpp:92`) | Must clear stored `AbstractClass *` or partner `EntityId` references to `target` according to `bool all` (`false` = stealth/untargetable, `true` = leaving world or dying). |
| `On_Damage` / `On_Kill` / `On_Captured` | Gameplay event hooks | `ObjectClass::Take_Damage` (`code/object.cpp:1596`), `Record_The_Kill`, `TechnoClass::Captured` (`code/techno.cpp:4853`) | Damage modifiers, attacker `TechnoClass *`, warhead, house ownership transfer. Must check `IsActive` after chained trigger `Spring` calls, which can destroy `this` mid-function (`code/object.cpp:1710-1765`). |
| `On_Serialize` | Save/load member persistence | Owning leaf's `Serialize(SaveStreamClass &)` chain (`code/abstract.cpp:195`) | Primitive values and swizzled `AbstractClass *` pointers in fixed declaration order. Raw `EntityId` values are forbidden ([Entity-component adoption plan](ENTITY-COMPONENT-PLAN.md#6-compatibility-rules-apply-to-every-phase)). |
| `On_Compute_CRC` | Lockstep state checksum | `Object_CRCs()` (`code/saveload.cpp:327`) | Deterministic fold of every simulation-affecting field into `CRCEngine`. Required across all 65 classes, including those omitted from `OBJECT_HEAP_LIST`. |
| `On_Destroy` | Unity `OnDestroy`, Unreal `EndPlay` / `BeginDestroy` | Leaf destructor (while C++ subclass members still exist) and `~AbstractClass()` / `~LocomotionClass()` (`code/abstract.cpp:72`, `code/loco.cpp:58`) | Teardown of component-owned state. Must tolerate half-loaded objects from failed `Load_Object` calls (`code/saveload.cpp:254`) and post-`Scen`/`Rule` shutdown in `Prog_End()` (`code/startup.cpp:1001`). |

## 3. Creation, configuration, `Ready`, and save restoration

### 3.1 Why leaf constructors cannot run `Ready` logic

Every included leaf constructor runs `Entity = FlecsWorld.Replace_Entity(...)`,
giving the object a valid `EntityId` before the constructor finishes. Five
reachable construction paths prevent that site from serving as a component
`Ready` callback:

1. **Pre-`main` sentinel construction.** The global `CellClass BlubCell`
   constructs during C++ static initialization before `WinMain`, before `Rule`,
   `Scen`, or `TacticalMap` exist, and potentially before `FlecsWorldClass`'s
   own constructor runs (`code/flecs/world.cpp:30-36`).
2. **Placement reconstruction without destruction.** `MapClass::Init_Cells()`
   reconstructs `CellClass` slots with placement `new` over live cells without
   calling `~CellClass()`. `Replace_Entity` destroys the prior entity and issues
   a fresh one, still prior to map INI loading (`Map.Read_INI`).
3. **Default shell construction during save loading.** `Register_Class<T>`
   (`code/classfactory.h:26`) instantiates `std::make_unique<T>()` with null
   `type` and `house` arguments (`UnitClass(NULL, NULL)` at
   `code/unit.cpp:212`). At construction time, `Class` is `NULL`, `House` is
   `NULL`, `Locomotion` is not created, and every member holds a dummy value
   about to be overwritten by `Load(stream)`.
4. **In-limbo creation before placement.** `Create_One_Of()` on
   `ObjectTypeClass` leaves constructs the object with `IsInLimbo == true`,
   `IsDown == false`, `IsSubmittedToLayer == false`, and `Position == (0,0,0)`.
   A newly built or scripted object can sit in limbo across frames (inside a
   transport, reinforcement team, or factory) or be deleted immediately if a
   subsequent `Unlimbo()` fails cell occupancy checks.
5. **Unlinked locomotor construction.** `Create_Locomotor(ClassID)`
   (`code/loco.cpp:184`) constructs the leaf locomotor while `LinkedTo` is still
   `NULL`. The owner bond exists only after `Link_To_Object(this)` runs.

### 3.2 Scenario initialization timeline and deferred `Ready`

Loading a fresh mission or skirmish map through `Start_Scenario()`
(`code/scenario.cpp:351`) calls `Read_Scenario()` -> `Read_Scenario_INI()`
(`code/scenario.cpp:1775-2210`). Tracing that function line by line shows that
neither type data nor instance cross-references are final when individual
objects construct and `Unlimbo()`:

```text
Read_Scenario_INI (code/scenario.cpp)
  1777  ScenarioInit++
  1780  Clear_Scenario()                  — deletes heaps; preserves IsometricTileTypes
  1790  TacticalMap recreated; Init_Theater()
  1861  Rule->Initialize(*RuleINI)        — creates base Type objects & reads rules.ini
  1885  Rule->Addition(ini)               — applies map-embedded rule overrides
  1903  HouseClass::Read_All(ini)         — creates scenario houses (campaign)
  1932  Assign_Start_Positions / Read_Spawn_Houses (skirmish / multiplayer)
  1938  TeamType, ScriptType, TaskForce, TriggerType, TagType, AITriggerType::Read_All
  1986  Map.Read_INI(ini)                 — placement-constructs cells & iso tiles
  1995  TubeClass::Read_INI, OverlayClass::Read_INI, TerrainClass::Read_INI
  2048  UnitClass::Read_INI(ini)          — constructs AND calls Unlimbo() on units
  2052  AircraftClass::Read_INI(ini)      — constructs AND calls Unlimbo() on aircraft
  2058  InfantryClass::Read_INI(ini)      — constructs AND calls Unlimbo() on infantry
  2068  BuildingClass::Read_INI(ini)      — constructs AND calls Unlimbo() on buildings
  2080  SmudgeClass::Read_INI(ini)        — constructs smudges
  2103  Rule->Addition(mini)              — campaign scenario .INI / MISSION.INI overrides!
  2142  Rule->Addition(mini)              — skirmish mode rules overrides (TMCJ4F.INI)!
  2154  Map.Overpass()                    — smooths tiberium and cell attributes
  2163  Multiplayer_Last_Minute_Fixups()  — spawns starting MCVs/units/crates, deletes extras
  2167  Swizzler.Resolve()                — binds attached Tag pointers onto placed objects!
  2172  Process_Deferred_Deletion()       — deletes objects culled during setup
  2179  ScenarioInit--                    — scenario state is complete here
```

Two ordering facts govern component `On_Ready` design:

- **Rules mutate after objects `Unlimbo`.** Lines 2103, 2106, and 2142 execute
  `Rule->Addition(mini)` *after* `UnitClass::Read_INI`, `InfantryClass::Read_INI`,
  and `BuildingClass::Read_INI` have already constructed and placed all map
  units and structures. A component that copies or caches derived values from
  its `Type` entity during `Unlimbo()` at line 2048 sees pre-override values.
- **Object pointers resolve at the end of `Read_Scenario_INI`.** Attached
  triggers (`ObjectClass::Tag`) read from INI entries register unresolved
  swizzle requests that remain invalid raw IDs until `Swizzler.Resolve()` at
  line 2167, and `Multiplayer_Last_Minute_Fixups()` at line 2163 spawns starting
  forces and removes unused map objects before frame 0.

**Required rule for `On_Ready`:**

- While `ScenarioInit > 0`, `Unlimbo()` and non-spatial object creation record
  the entity in a deterministic queue without invoking `On_Ready`.
- Immediately after `ScenarioInit--` in `Read_Scenario_INI()`
  (`code/scenario.cpp:2179`), flush the queued entities in creation order,
  skipping any entity destroyed by `Multiplayer_Last_Minute_Fixups()` or
  `Process_Deferred_Deletion()` (`!Entity_Is_Alive(id)` or `!obj->IsActive`),
  and invoke `On_Ready` once per live entity (types first, then non-spatial
  owners such as `HouseClass` and `TeamClass`, then spatial `ObjectClass`
  instances and their active locomotors).
- While `ScenarioInit == 0` (live gameplay), rules and swizzle tables are
  already settled; invoke `On_Ready` immediately after an entity's first
  completed world activation (`Unlimbo()` leaf tail for `ObjectClass`, or `Init`
  completion for non-spatial classes).
- Note one `Clear_Scenario()` exception (`code/scenario.cpp:1077-1085`):
  `IsometricTileTypeClass` instances are removed from `AbstractTypes` right
  before `Delete_All_Objects()` and re-inserted afterward, so their entities
  survive across scenario resets when the theater stays loaded.

### 3.3 Save-load pipeline and the `IPersistent::Post_Load` trap

Loading a saved game through `Load_Game()` (`code/saveload.cpp:1125-1215`)
passes through four distinct stages, and confusing stage 2 with stage 4 causes
immediate memory-access violations:

```text
Load_Game (code/saveload.cpp)
  1163  Get_All(stream, false)
          746  Clear_Scenario()
          796  Load_Vector<AnimTypeClass>(stream)
          800  Map.Load(stream)           — loads cells, overlays, smudges BEFORE their types!
          811  Logic.Load(stream)         — reads unswizzled object IDs into Logic array
          820  Load_Vector<UnitClass>     — loads Units BEFORE UnitTypes (line 823)!
                 └─ Load_Object() (line 211):
                      1. Create_Object(classid) -> UnitClass() shell + Attach_Entity
                      2. persist->Load(stream)  -> raw SwizzleIDType integers in pointer fields
                      3. persist->Post_Load()   -> POINTERS ARE STILL UNSWIZZLED INTEGERS!
          823  Load_Vector<UnitTypeClass>
          ...  remaining 40+ heaps ...
  1189  Swizzler.Resolve()                — converts all registered pointer fields to live C++ addresses
  1195  Post_Load_Game()                  — code/scenario.cpp:1019: ALL POINTERS ARE VALID HERE
```

Inside `IPersistent::Post_Load()` (`code/saveload.cpp:259`), `UnitClass::Class`,
`TechnoClass::House`, `FootClass::NavCom`, `TechnoClass::TarCom`,
`CargoClass::CargoHold`, and `LocomotionClass::LinkedTo` hold raw integer
swizzle identifiers rather than valid heap addresses. `Units` is loaded at line
820 before `UnitTypes` even exists on the heap at line 823. Existing engine
overrides such as `UnitClass::Post_Load()` (`code/unit.cpp:6134`) only register
`TargetTracker.Add_Index(Fetch_ID(), this)` using the object's own integer `ID`.

**Required rules for persistence callbacks:**

1. **`On_Serialize(SaveStreamClass &)`** runs inside the owning leaf's
   `Serialize` method (`code/unit.cpp:6105` and peers). Pointers serialized
   through `stream.Serialize(ptr)` register automatically with `Swizzler`
   during load.
2. **Never dereference cross-object pointers in `IPersistent::Post_Load()`.**
   Do not rebuild `TypeOf` entity pairs, parent locomotor entities, or read
   `Class_Of()` fields there.
3. **`On_Post_Load_Resolve()`** hooks into `Post_Load_Game()`
   (`code/scenario.cpp:1019`), which runs right after `Swizzler.Resolve()`
   (`code/saveload.cpp:1189`). At this point every heap object, type object,
   cell, and swizzled pointer is live:
   - Re-establish locomotor child parenting (`Set_Entity_Parent(loco->Entity, foot->Entity)`)
     including any nested piggyback locomotor;
   - Re-establish `TypeOf` pairs (`Class_Of()->Entity`);
   - Dispatch `On_Post_Load_Resolve` on restored components in deterministic
     heap order so non-saved caches, derived handles, and dirty render flags
     rebuild cleanly without clobbering saved state.
4. **Aborted load cleanup.** If any record fails validation inside
   `Load_Object()` (`code/saveload.cpp:254`), `Swizzler.Abandon(mark)` runs and
   `std::unique_ptr<IPersistent>` destroys the partially loaded object
   immediately. `~AbstractClass()` destroys its entity, and any `On_Destroy`
   hook must never dereference member pointers unless `On_Ready` or
   `On_Post_Load_Resolve` has marked the component initialized.

## 4. World entry, world exit, cargo holds, and tunnels

### 4.1 Asymmetry between `Unlimbo` and `Limbo`

In OpenTS, `ObjectClass` lifetime (`new` to `delete`) is broader than active
battlefield presence (`Unlimbo` to `Limbo`). The two virtual chains execute in
different base-to-derived orders across classes:

- **`Unlimbo` executes top-down (base first, leaf last) in every class.**
  `BuildingClass::Unlimbo` (`code/building.cpp:1938`), `UnitClass::Unlimbo`
  (`code/unit.cpp:1322`), `InfantryClass::Unlimbo` (`code/infantry.cpp:2523`),
  and `AircraftClass::Unlimbo` (`code/aircraft.cpp:302`) call
  `if (BASECLASS::Unlimbo(...))` first:
  1. `ObjectClass::Unlimbo` (`code/object.cpp:1382`) sets `IsInLimbo = false`,
     computes `PositionCoord`, marks down in `Map`, submits to `Map` display
     layers (`line 1403`), and submits sentient objects to `Logic.Submit(this)`
     (`line 1409`).
  2. `TechnoClass::Unlimbo` (`code/techno.cpp:1790`) configures facings, house
     counters, cloak state, and sight reveal.
  3. `FootClass::Unlimbo` (`code/foot.cpp:1438`) invokes `Locomotion->Unlimbo()`.
  4. The leaf `Unlimbo` body sets class-specific state (bib shapes, turret
     facings, factory/power registrations, starting animations).

  **Consequence:** Hooking `On_Enter_World` inside `ObjectClass::Unlimbo` fires
  prematurely before facing, house tracking, locomotor state, or building
  power/animations exist. Dispatch `On_Enter_World` (and live `On_Ready` on
  first entry when `ScenarioInit == 0`) at the **exit of the leaf `Unlimbo`
  chain** (or via a non-virtual `ObjectClass::Enter_World(...)` wrapper if
  refactored later).

- **`Limbo` mixes bottom-up and top-down orders across leaves.**
  `BuildingClass::Limbo` (`code/building.cpp:3478`), `FootClass::Limbo`
  (`code/foot.cpp:3490`), `InfantryClass::Limbo` (`code/infantry.cpp:2440`),
  `TechnoClass::Limbo` (`code/techno.cpp:1769`), and `TerrainClass::Limbo`
  (`code/terrain.cpp:758`) run subclass teardown *before* calling
  `BASECLASS::Limbo()`. By contrast, `UnitClass::Limbo` (`code/unit.cpp:5965`)
  and `BuildingLightClass::Limbo` (`code/blight.cpp:479`) call
  `if (BASECLASS::Limbo())` *first* — meaning `ObjectClass::Limbo`
  (`code/object.cpp:1315`) has already executed `Unselect()`, `Detach_All()`,
  `Map.Remove(this)`, `Logic.Remove(this)`, and `IsInLimbo = true` before
  `UnitClass::Limbo` runs its body.

  **Consequence:** Place `On_Exit_World` at the top of `ObjectClass::Limbo()`
  guarded by `if (GameActive && !IsInLimbo)` right before `Detach_All()`,
  `Map.Remove(this)`, and `Logic.Remove(this)`. At that single choke point,
  every object type still holds its last valid `PositionCoord`, `House`, cell,
  and `IsActive` flag (`IsActive == false` if entering `Limbo` from
  `Delete_Me()`, or `IsActive == true` if entering temporary limbo).

### 4.2 Cargo holds and map tunnels

Two in-game states remove or bypass standard per-frame processing without
destroying the object:

- **Transport, Carryall, and Hospital cargo (`CargoClass`).** Calling
  `CargoClass::Attach` (`code/cargo.cpp:200`) or `Attach_Group` (`line 74`)
  calls `object->Limbo()`, removing the passenger from `Logic` (`IsSubmittedToLayer = false`).
  `TechnoClass::AI()` calls `Cargo.AI()` (`code/techno.cpp:3040`), whose body in
  `code/cargo.h:52` is empty (`void AI(void) {};`). Passengers receive zero
  `ObjectClass::AI()` ticks until `Detach_Object()` and `Unlimbo()` place them
  back onto the map. A component that must continue ticking while aboard a
  transport (for example, ability recharge or timed status decay) cannot rely on
  `Logic`-layer membership; it requires an explicit limbo-enabled tick mode.
- **Subterranean map tubes (`TubeClass`).** Units and infantry traversing a
  cliff tunnel (`CurrentTube >= 0`) remain submitted to `Logic`, yet both
  `UnitClass::AI()` (`code/unit.cpp:480-484`) and `InfantryClass::AI()`
  (`code/infantry.cpp:1387-1391`) execute `Tunnel_AI(); Update_Radar_Position(); return;`
  at the very top of `AI()`, bypassing `FootClass::AI()`, `TechnoClass::AI()`,
  `MissionClass::AI()`, and `ObjectClass::AI()`. Any component hook placed inside
  `ObjectClass::AI()`, `TechnoClass::AI()`, or `FootClass::AI()` stops firing
  for every tunnel transit frame.

## 5. Single-frame timeline and per-class `Process` coverage

### 5.1 Complete `Main_Loop()` execution order

One iteration of `Main_Loop()` (`code/mainloop.cpp:225-430`) executes the
following sequence:

```text
Main_Loop() — one simulation frame `Frame` (code/mainloop.cpp)
  1. Check_For_Focus_Loss(), Session.Trap_Object(), FramePacer arming (lines 240-285)
  2. Pre-logic local UI & render pass (when !Session.Play, SpecialDialog == SDLG_NONE, GameInFocus):
       UIShell.Tick() -> Map.Input() -> Keyboard_Process() -> Update_Fogged_Objects() -> Map.Render()
  3. Do_Record_Playback() (lines 310-312)
  4. DisplayClass::Layer[LAYER_GROUND].Sort() (line 320 — unconditional deterministic sort)
  5. Logic.AI() (line 325 -> code/logic.cpp:231) — THE SIMULATION TICK:
       5.1  LogicTags trigger checks (lines 239-277) + scenario status flag resets (lines 283-290)
       5.2  Shroud creep, Fog creep, Ambient light step, Ice growth (lines 295-332)
       5.3  VeinholeMonsterClass::Update_All() (line 334)
       5.4  TiberiumClass::Tiberium_Growth() and Tiberium_Spread() (lines 335-336)
       5.5  Map.Ice_Solidification_AI() (line 337)
       5.6  TeamClass::AI() over snapshot vector of `Teams` (lines 339-348)
       5.7  SpotLight::Update_All, LaserDraw::Update_All, IonStorm::AI,
            LightSource::Process_Lighting(6), EMPulse::Update_All, Map.Terrain_Deformation_AI (lines 350-355)
       5.8  Logic layer objects: for (index = 0; index < Count(); index++) (*this)[index]->AI() (lines 360-375)
       5.9  MoveFlashes[index]->AI() (multiplayer only, lines 379-383)
       5.10 IonBlastClass::Update_All(), AlphaShapeClass::Update_All() (lines 385-386)
       5.11 Map.Logic(), TacticalMap->AI() (lines 391-393)
       5.12 Factories[index]->AI() (lines 398-400)
       5.13 Houses[house]->AI() (lines 405-410 — power/radar, team spawn, low-power damage,
            Super_Weapon_Handler(), multiplayer defeat check, HouseTags->Spring())
       5.14 Camera follow-object update (lines 412-414)
  6. Session.Messages.Manage() (line 329)
  7. Queue_AI() (line 340 -> code/queue.cpp:502) — LOCKSTEP SYNC & COMMANDS:
       Network frame sync / CRC exchange -> OutList to DoList -> EventClass::Execute() (code/event.cpp:701)
  8. Call_Back() + Win / Lose / Restart / Abort checks (lines 342-398)
  9. Frame++ (line 405)
 10. Sync_Delay() (line 422 -> lines 498-535) — VARIABLE-COUNT INTER-FRAME SPIN:
       While FrameTimer > 10: Call_Back() -> Map.Input() -> Keyboard_Process() -> TacticalMap->AI() -> Map.Render()
 11. Process_Deferred_Deletion() (line 423 -> code/tracker.cpp:216):
       Physically `delete`s objects in `ObjectsToDelete`, invoking destructors and `Destroy_Entity`
 12. SaveManager.Service() (line 424 — executes pending autosave at clean post-delete boundary)
```

Two structural hazards inside step 5.8 (`LogicClass::AI` object loop at
`code/logic.cpp:360-375`) directly affect any component `Process` design:

1. **Mid-loop removal skips the neighbor in `Logic`.** The index-backtrack line
   `if (obj != (*this)[index]) index--;` in `code/logic.cpp:372-374` is
   commented out. When `obj->AI()` calls `Delete_Me()` or `Limbo()`,
   `Logic.Remove(obj)` deletes slot `index` from the `LayerClass` array and
   shifts remaining pointers left by one, skipping `(*this)[index]` for that
   single tick — a behavior baked into original Tiberian Sun replay and sync
   determinism. Appending a new object via `Unlimbo()` pushes to the back of
   `Logic` and ticks later in the *same* `Frame` because `Count()` re-evaluates
   every iteration.
2. **Death inside `AI()` occurs at multiple internal points.** Within one
   `UnitClass::AI()` call, `this` can queue self-deletion via `DeathCounter`
   (`code/unit.cpp:472`), `Rocking_AI()` (`code/techno.cpp:2995`), falling C4
   impact (`code/object.cpp:295`), `Locomotion->Process()` (`code/foot.cpp:3306`),
   refinery/transport entry (`Enter_Object_Nearby`, `code/foot.cpp:3327`),
   sinking (`code/unit.cpp:518`), or `Edge_Of_World_AI()` (`code/unit.cpp:544`).
   After any of those calls, the C++ pointer still lives until step 11
   (`Process_Deferred_Deletion`), with `IsActive == false` and `IsInLimbo == true`.
   Any post-`AI` component hook must check `if (obj->IsActive && !obj->IsInLimbo)`
   before acting on the object.

### 5.2 Per-frame tick coverage across all 65 entity-bearing classes

Only 10 of the 65 entity-bearing classes participate in the `Logic` layer loop
at step 5.8. Tying component `Process` exclusively to `ObjectClass::AI()` leaves
55 entity classes unable to tick components.

| Tick mechanism today | Entity-bearing classes (count) | Exact call site | Constraints for component `Process` |
| --- | --- | --- | --- |
| `Logic` layer (`(*this)[i]->AI()`) | `AircraftClass`, `BuildingClass`, `BuildingLightClass`, `BulletClass`, `InfantryClass`, `ParticleSystemClass`, `TerrainClass`, `UnitClass`, `VoxelAnimClass`, `WaveClass` (10) | `code/logic.cpp:364` | Active only while `!IsInLimbo` and `Class_Of()->IsSentient`. `UnitClass` and `InfantryClass` return early on `CurrentTube >= 0` before base `AI()`. |
| Subsystem static batch updaters | `VeinholeMonsterClass`, `LightSourceClass`, `EMPulseClass`, `AlphaShapeClass` (4) | `code/logic.cpp:334,353,354,386` | Not submitted to `Logic` layer; `LightSourceClass::Process_Lighting(6)` processes in stride slices rather than every instance per frame. |
| Dedicated manager loops in `LogicClass::AI()` | `TeamClass`, `FactoryClass`, `HouseClass` (3) | `code/logic.cpp:347,399,408` | `Teams` iterates a temporary snapshot vector; `Factories` and `Houses` iterate live global heaps directly. |
| Owner-driven sub-objects | `ParticleClass`, `SuperClass`, `ScriptClass`, `NeuronClass` (4) | `ParticleSystemClass::AI`, `HouseClass::Super_Weapon_Handler`, `TeamClass::AI` | Ticked indirectly by owning objects when conditions hold (`SuperClass` ticks only while granted/charging). |
| Active outer locomotor | `DriveLocomotionClass`, `DropPodLocomotionClass`, `FlyLocomotionClass`, `HoverLocomotionClass`, `JumpjetLocomotionClass`, `LevitateLocomotionClass`, `MechLocomotionClass`, `TeleportLocomotionClass`, `TunnelLocomotionClass`, `WalkLocomotionClass` (10) | `FootClass::AI` (`code/foot.cpp:3306`) | Skipped when owner is `IsInLimbo`, `IsSinking`, `IsFalling`, or inside a tube (`CurrentTube >= 0`). Inner piggybacked locomotor never ticks while overridden. |
| No per-frame `AI()` loop | `CellClass` (262,144), `OverlayClass`, `SmudgeClass`, `FoggedObjectClass`, `TagClass`, `TriggerClass`, `TubeClass`, `WaypointPathClass`, all 26 `AbstractTypeClass` leaves (34) | None (`LogicTags` springs event checks only at `code/logic.cpp:239`; `TiberiumClass` steps queued cells at `lines 335-336`) | Zero per-object tick loop exists. Scanning all 262,144 `CellClass` instances per tick is prohibitively expensive; only entities holding active tickable components may be visited. |

**Architectural conclusion for `Simulation_Process`:**
Drive component `Simulation_Process` from explicit deterministic hook points in
`LogicClass::AI()` that visit **only entities holding registered tickable
components**, ordered deterministically by owner sort key (§7). Keep individual
component calls out of the 17 per-class `AI()` overrides and static
`Update_All()` helpers, and never scan passive heaps such as `MapClass::Array`.
Two clear phases inside `LogicClass::AI()` cover gameplay needs while
preserving object `AI()` timing:

1. **Object phase (`Post_Object_Process`, right after line 375 of `code/logic.cpp`):**
   runs once per `Frame` after `TeamClass::AI()`, environment effects, and all
   `Logic` layer `ObjectClass::AI()` calls have stepped movement, weapons, and
   missions, and before `Factories` and `Houses` evaluate production and defeat.
   Components opt into either **world-active mode** (skipped if owner is an
   `ObjectClass` with `IsInLimbo` or `!IsActive`) or **always-tick mode** (runs
   for active objects in cargo/limbo, `CellClass`, `SuperClass`, `HouseClass`,
   `TriggerClass`, and `Type` entities).
2. **End-of-logic phase (`Post_Logic_Process`, right after line 414 of `code/logic.cpp`):**
   runs once per `Frame` after `Factories[i]->AI()` and `Houses[h]->AI()` finish
   power, radar, superweapons, and house triggers, directly ahead of `Queue_AI()`.

## 6. Reference detachment, rendering, input, and teardown

### 6.1 Pointer and entity reference invalidation (`Detach_This_From_All`)

OpenTS does not use reference-counted smart pointers between game objects.
Whenever an object cloaks, enters limbo, or dies, `Detach_This_From_All(target, all)`
(`code/tracker.cpp:92-207`) walks up to 11 global pointer trackers
(`HousePtrTracker`, `AnimPtrTracker`, `ObjectPtrTracker`, `AbstractTypePtrTracker`,
`TagPtrTracker`, `TriggerPtrTracker`, `FactoryPtrTracker`,
`EventActionPtrTracker`, `WaypointPathPtrTracker`, `TeamPtrTracker`, and
`NeuronPtrTracker`) and calls `observer->Detach(target, all)` on every tracked
listener.

Two rules bind components that reference other game objects:

1. **Hook `Detach_This_From_All` directly.** Add one bridge call at the end of
   `Detach_This_From_All(AbstractClass const * target, bool all)` in
   `code/tracker.cpp:207`: `FlecsWorld.Detach_Target(target, all)`. Relying on
   `~AbstractClass()` (`Destroy_Entity`) or Flecs `ChildOf` / pair cleanup is
   insufficient for two reasons:
   - `Delete_Me()` (`code/object.cpp:2440`) runs `Detach_This_From_All(this, true)`
     mid-frame inside `Logic.AI()`, whereas `~AbstractClass()` does not destroy
     `this->Entity` until `Process_Deferred_Deletion()` after `Queue_AI()` and
     `Sync_Delay()`. Without `Detach_Target`, components keep targeting dead
     objects for the rest of the frame.
   - `Limbo()` and stealth cloaking (`all == false`) detach targets while
     keeping both C++ object and `EntityId` alive (`Entity_Is_Alive` stays `true`!).
2. **Honor `bool all`.**
   - When `all == false` (stealth cloak, subterranean dive, sensor loss), drop
     hostile/targeting references while keeping ownership, team, factory, or
     permanent attachment links.
   - When `all == true` (cargo limbo, scenario clear, `Delete_Me`, destructor),
     clear **all** stored `AbstractClass *` and partner `EntityId` references to
     `target` (except cargo ownership links explicitly managed by `CargoClass`).

### 6.2 Rendering and dirty-surface invalidation

Rendering runs outside deterministic simulation lockstep and splits objects
across two distinct surface pipelines inside `GScreenClass::Render()`
(`code/gscreen.cpp:386`) and `Tactical::Render()` (`code/tactical.cpp:1066`):

- **Cached background surface (`TileSurface`, `DRAW_PASS_BACKGROUND`, `code/tactical.cpp:1244-1255`):**
  `Render_Tiles`, `Render_Fogged_Objects`, `Render_Overlays`, `Render_Terrain`,
  and `Render_Buildings` draw onto a persistent off-screen cache only inside
  pan strips (`xpanrect`, `ypanrect`), `CellRedraw`, and `DirtyAreas`. A static
  `BuildingClass`, `TerrainClass`, `OverlayClass`, `SmudgeClass`, or `CellClass`
  receives **zero `Draw_It` calls** across idle frames. Any component whose state
  alters visual appearance on these classes must call `obj->Mark(MARK_CHANGE)`
  or `TacticalMap->Register_Dirty_Area(...)`, otherwise stale cached pixels stay
  on screen.
- **Dynamic viewport pass (`CompositeSurface`, `DRAW_PASS_FOREGROUND`, `code/tactical.cpp:1275-1315`):**
  `VeinholeMonsterClass::Draw_All()`, `IonBlastClass::Draw_All()`,
  `Draw_Objects(true)` (units, infantry, aircraft, bullets, animations,
  particles, waves, voxel animations, building lights), `SpotLightClass::Draw_All()`,
  `LaserDrawClass::Draw_All()`, radial indicators, and RmlUi overlays (`UIShell.Tick()`)
  run for objects intersecting local `TacticalRect`.

**Strict rendering invariants:**

- `Render_Process` / `On_Draw` executes **zero to many times per simulation `Frame`**:
  zero times when off-screen, occluded by modal dialogs, or replaying prior to
  redraw; multiple times inside `Sync_Delay()` (`code/mainloop.cpp:508`) while
  waiting on `FrameTimer`.
- Each multiplayer client holds its own camera position (`TacticalRect`), zoom,
  and FPS. Calling `Scen->RandomNumber` (`Random_Pick`, `Percent_Chance`),
  creating simulation entities, or writing simulation-affecting component fields
  from `Render_Process` causes immediate multiplayer desync. Use
  `NonCriticalRandomNumber` for local visual particles or audio variance.

### 6.3 Player input and network command execution

Local UI and input handlers (`UIShell.Tick()`, `Map.Input()`, `Keyboard_Process()`,
`Clicked_As_Target`, `Active_Click_With`) run asynchronously with respect to
network lockstep (including inside `Sync_Delay()`). They never mutate game state
directly; they construct an `EventClass` and push it to `OutList`.

All authoritative player commands execute inside `Queue_AI()`
(`code/queue.cpp:502`) -> `Execute_DoList()` -> `EventClass::Execute()`
(`code/event.cpp:701`). Any player-activated component action (for example,
triggering a custom unit ability from the UI or hotkey) must encode its subject
via `TargetClass` (or `HeapID`), transmit a new or extended `EventClass` entry,
validate ownership under `NetSemantic` (`code/event.cpp:718-729`), and invoke
the component mutation inside `EventClass::Execute()`. Never place `EntityId` on
the wire.

### 6.4 Teardown order and shutdown hazards

Component cleanup faces three distinct destruction contexts:

1. **Normal gameplay deletion (`Delete_Me` -> `Process_Deferred_Deletion` or direct `delete`):**
   Leaf destructor runs first (`~UnitClass`, `code/unit.cpp:259` — removes from
   `Team`, `House`, `Cargo`, calls `Limbo()`, `Detach_This_From_All(this)`,
   `Units.Delete(this)`, `TargetTracker.Remove_Index`, `IsActive = false`),
   followed by intermediate destructors (`~FootClass` destroys `Locomotion`,
   destroying child locomotor entity first), and finally `~AbstractClass()`
   (`code/abstract.cpp:72`) calls `FlecsWorld.Destroy_Entity(Entity)`. Notice
   that by the time `~AbstractClass()` executes, the derived subobject is
   already destroyed (calling virtual methods or casting `void * Object` back to
   `UnitClass *` inside `~AbstractClass()` is undefined behavior in C++!). If an
   `On_Destroy` callback needs access to derived class members, invoke
   `FlecsWorld.Notify_Destroying(Entity)` from leaf destructors (or restrict
   `On_Destroy` in `~AbstractClass()` to self-contained component data only).
2. **Scenario reset (`Clear_Scenario` -> `Delete_All_Objects`, `code/init.cpp:5665`):**
   Runs with `ScenarioInit > 0`. Deletes objects heap by heap. Components must
   not assume destruction order across heaps (for example, `Bullets` deletes
   before `Units`, and `Houses` deletes before `AbstractTypes`). At the end of
   `Clear_Scenario()`, `FlecsWorld.Live_Entity_Count()` equals the count of
   preserved `IsometricTileTypeClass` entities plus the static `BlubCell`
   sentinel.
3. **Process shutdown (`Prog_End`, `code/startup.cpp:968-1005`):**
   `Prog_End()` deletes `TacticalMap` (`line 969`), `Rule` (`line 993`), and
   `Scen` (`line 998`) **before** calling `FlecsWorld.Shutdown()` (`line 1001`).
   `Emergency_Exit()` (`line 1037`) calls `Prog_End()` directly without running
   `Clear_Scenario()` first. Therefore `FlecsWorld.Shutdown()` destroys live
   entities while `TacticalMap == NULL`, `Rule == NULL`, and `Scen == NULL`.
   Move `Clear_Scenario()` (or explicit component shutdown) ahead of `delete Rule`
   and `delete Scen`, and forbid component destructors from touching global
   engine pointers without null checks.

## 7. Dispatch architecture options for component functions

The maintainer requested an explicit evaluation of multiple architectural models
for invoking `Ready`, `Process`, and peer callbacks on components. Four designs
span the practical trade-off space on top of the vendored Flecs v4.1.6 store.

### Option A — Pure data components with explicit per-feature functions

Components remain plain data structs in `code/flecs/components.hh` with zero
methods or virtual tables, matching
[Entity-component adoption plan](ENTITY-COMPONENT-PLAN.md) as written. Each new
feature adds explicit helper functions (for example, `Shields_Ready`,
`Shields_AI`, `Shields_Detach`, `Shields_CRC`) called directly by name from
engine sites (`LogicClass::AI`, `Post_Load_Game`, `Detach_This_From_All`,
`Object_CRCs`).

- **Strengths:** Every call order is visible line by line in C++ callers; zero
  indirection overhead; zero header coupling outside `code/flecs/world.cpp`;
  trivial debugging.
- **Weaknesses:** Poor scaling past 5 to 10 components. Adding one component
  requires editing 6 to 8 core engine files (`logic.cpp`, `scenario.cpp`,
  `saveload.cpp`, `tracker.cpp`, leaf `Unlimbo`, leaf `Serialize`), and
  forgetting one call site silently skips `Detach` or `Compute_CRC` (causing
  dangling pointers or silent multiplayer desyncs). Authors do not get a uniform
  `Ready` / `Process` contract per component.

### Option B — Registered type-erased component lifecycle tables (`ComponentOps`)

Components stay plain C++ structs stored in Flecs tables, while
`FlecsWorld.Register_Component<T>("Name")` registers a compile-time
`ComponentOps` descriptor alongside each component type in
`code/flecs/lifecycle.hh` (populated automatically via C++20 concepts on
optional member methods `On_Ready`, `Process`, `On_Enter_World`,
`On_Exit_World`, `Render_Foreground`, `Detach`, `Compute_CRC`, `Serialize`,
`Post_Load_Resolve`, and `On_Remove`, plus optional static traits `Phase` and
`Scope`). Core engine files contain **one single hook per lifecycle event**
(`FlecsWorld.Flush_Scenario_Ready()`, `FlecsWorld.Run_Sim_Phase(...)`,
`FlecsWorld.Detach_Target(target, all)`, `FlecsWorld.Fold_CRCs(crc)`), and
`FlecsWorldClass` iterates registered component types in deterministic
registration order.

```cpp
// Author-facing component definition (plain struct, C++20 trait detection):
struct RegenerationShieldComponent {
	static constexpr SimPhase Phase = SimPhase::PostObjects;
	static constexpr TickScope Scope = TickScope::WorldActive;

	int CurrentShield = 0;
	int MaxShield = 0;
	int RechargeTimer = 0;
	AbstractClass const * LastAttacker = nullptr;

	void On_Ready(EntityContext const & context);
	void Process(EntityContext const & context);
	void Detach(EntityContext const & context, AbstractClass const * target, bool all);
	void Compute_CRC(EntityContext const & context, CRCEngine & crc) const;
	void Serialize(SaveStreamClass & stream);
	void Post_Load_Resolve(EntityContext const & context);
};
```

Inside `code/flecs/world.cpp`, `FlecsWorldClass` binds each registered struct
directly through Flecs's C storage API (`ecs_component_init` and
`ecs_set_hooks_id` with full constructor, destructor, copy, move, and `on_remove`
hooks) and caches an `ecs_query_t *` per component type without leaking
`flecs.h` outside `world.cpp`. On `Run_Sim_Phase(phase)`, for each registered
component matching `phase`, the bridge gathers active entities into a scratch
vector, sorts by a deterministic composite key
`(Kind, FetchId, AttachSequence)` (inheriting activity and limbo flags from the
parent entity when attached to a `LocomotionClass` child entity), and invokes
`Process(context)` within a `defer_begin()` / `defer_end()` scope before
draining any queued `On_Ready` items. Components without `Process` (such as type
data on the 26 `Type` classes or passive markers on the 262,144 `CellClass`
instances) run no simulation query and incur **zero per-frame cost**.

- **Strengths:** Gives component authors self-contained methods (`On_Ready`,
  `Process`, `On_Enter_World`, `On_Exit_World`, `Render_Foreground`, `Detach`,
  `Compute_CRC`, `Serialize`, `Post_Load_Resolve`, `On_Remove`) inside plain C++
  structs (including non-trivial containers and move-only handles); catches
  mistyped hook signatures or missing `const` qualifiers at compile time via
  two-base name probes (`Validate_Component_Hooks<T>()`); wires core engine
  files once rather than per feature; keeps `flecs.h` confined to
  `code/flecs/world.cpp`; zero per-cell overhead when cells hold no ticking
  component.
- **Weaknesses:** Gathering and sorting active component matches per type costs
  `O(K log K)` where `K` is the count of active entities carrying `T` (negligible
  for hundreds or a few thousand units, measurable if `K` reaches 262,144 cells
  — where an ordered active list or chunked heap walk avoids sorting).

### Option C — Polymorphic `ComponentClass` objects (Godot `Node` / Unity `MonoBehaviour` style)

Each entity owns a small vector of heap-allocated polymorphic `ComponentClass *`
instances (stored either on `AbstractClass` or inside one Flecs container
component `ComponentBag`), with virtual methods `Ready()`, `Enter_World()`,
`Exit_World()`, `Process()`, `Render()`, `Detach()`, `Serialize()`, and
`Compute_CRC()`.

- **Strengths:** Familiar OOP inheritance model; component subclasses can share
  base helpers; cross-component lookups on a single entity are fast linear scans
  over a small vector; independent of Flecs archetype layout.
- **Weaknesses:** Defeats Flecs storage almost completely (turning Flecs into an
  unused ID table while every component causes separate heap allocations and
  pointer chasing); virtual dispatch on every callback across every entity;
  requires a parallel class-ID registry for polymorphic component serialization;
  to avoid walking 262,144 cells each frame, still requires global active-bag
  registration identical to Option B.

### Option D — Flecs native pipelines (`world.progress()`) and observers (`OnAdd` / `OnSet` / `OnRemove`)

Replaces rule 1 of [Entity-component adoption plan](ENTITY-COMPONENT-PLAN.md) by
enabling Flecs built-in systems (`world.system<A, B>().kind(flecs::OnUpdate)`),
`order_by` queries, and observers (`world.observer<T>().event(flecs::OnAdd)`).

- **Strengths:** Multi-component join queries (`<Transform, Shield, BurnEffect>`)
  use Flecs native archetype tables directly; `order_by<SortKey>` caches sorted
  table slices inside Flecs and re-sorts only dirty tables.
- **Weaknesses and traps specific to OpenTS:**
  1. **Unordered archetype iteration breaks lockstep sync.** Without mandatory
     `order_by` on a deterministic key (`RTTI` + `Fetch_ID()`), adding or
     removing a local UI/visual tag on one peer moves an entity between Flecs
     archetypes, altering iteration order of simulation systems and causing
     immediate multiplayer desync whenever `Random_Pick` or damage runs. Even
     *with* `order_by`, local-only presentation components must **never** be
     added to simulation entities in `FlecsWorld` if archetype transitions fragment
     tables unless `order_by` spans all matching tables identically across peers
     (or presentation components live in a separate local visual store).
  2. **Observers introduce hidden synchronous re-entrancy.** Firing `OnSet` or
     `OnRemove` callbacks immediately when `entity.set<T>()` runs mid-`AI()`
     executes gameplay code at arbitrary stack depths while parent objects are
     mid-mutation.
  3. **Header leakage or boilerplate.** Calling `world.system<...>()` across
     feature files exposes `flecs.h` to the whole build (violating the single-TU
     compile isolation in §3.5 of the adoption plan) unless wrapped inside
     `code/flecs/`.

### Summary comparison of the four options

| Criterion | Option A: Free functions | Option B: `ComponentOps` traits | Option C: Virtual `ComponentClass` | Option D: Native `world.progress` + observers |
| --- | --- | --- | --- | --- |
| Modern `Ready` / `Process` ergonomics | Low (manual functions) | High (methods on struct) | High (virtual overrides) | Medium-High (lambdas / observers) |
| Core engine edits per new component | 6 to 8 files every time | **Zero** after initial wiring | **Zero** after initial wiring | **Zero** if `flecs.h` exposed globally |
| Lockstep & CRC safety | Manual discipline | **Compile-time enforced** via C++20 concepts + sorted dispatch | Manual override discipline | Fragile unless `order_by` + zero local archetype fragmentation |
| Cost on 262k passive `CellClass` entities | Zero | **Zero** | 24 bytes per object if inline vector | Zero |
| Re-entrancy / mid-loop safety | Explicit | Explicit (`defer` scope) | Re-entrant vector mutation risk | High re-entrancy risk with observers |
| Keeps `flecs.h` inside `code/flecs/` | Yes | **Yes** | Yes | No (unless wrapped into Option B) |
| Multi-component query joins | Explicit query | Supported for helper queries | Poor | Native |

### Selected architecture: Option B with local visual isolation

**Option B (`ComponentOps` trait table inside `code/flecs/`)** is the chosen
architecture on this branch. Its core bridge (`code/flecs/lifecycle.hh`,
`code/flecs/components.hh`, `code/flecs/world.h`, `code/flecs/world.cpp`, and
`code/_flecs.cpp`), baseline attachment fixes (`code/abstract.h`,
`code/abstract.cpp`, `code/loco.h`, `code/loco.cpp`), and standalone C++20 test
suite (`tests/flecs/main.cpp`) are implemented and verified across Debug
(ASan + UBSan + LSan), Release, and `OPENTS_EC_DISABLED` builds. Wiring hooks #2
through #9 into the engine simulation loop files remains staged for the next
step alongside the first gameplay component feature.

Key guarantees provided by the implemented bridge:

1. **Compile-time safety contracts (`code/flecs/lifecycle.hh`):**
   - Two-base name lookup probes (`detail::Validate_Component_Hooks<T>()`) fail
     compilation immediately if a component author declares any hook name with a
     mismatched parameter list, wrong return type, or missing `const` qualifier
     on `Compute_CRC` or `Render_Foreground`.
   - Every component type uses a linker-ICF-immune `ComponentTypeKey` and binds
     full C++ default-construction, move, copy, destruction, and `On_Remove`
     hooks onto Flecs storage without exposing `flecs.h` in public headers.
   - Canonical sorting by `(Kind, FetchId, AttachSequence)` isolates simulation
     `Run_Sim_Phase` and `Fold_CRCs` execution order from Flecs table layout,
     archetype fragmentation, and recycled entity generation handles.
2. **Persistence relocation safety (`Serialize_Entity` + `Post_Load_Resolve_All`):**
   - `Serialize_Entity` writes a length-framed component directory ahead of
     component payloads so unknown components skip cleanly on load, truncated
     streams fail safely, and all known components are added to the entity's
     final archetype before any `Serialize` call executes; `On_Ready` is
     suppressed during load so saved counters are preserved.
   - Because adding later entities to a Flecs table can grow and reallocate its
     backing array, components must never pass raw field addresses inside table
     storage to `SwizzleManager`; persist object references as integer IDs or
     heap handles and resolve pointers inside `Post_Load_Resolve`.
3. **Engine integration checklist (hook #1 implemented; hooks #2–#9 staged):**

| # | Status | File and function | Exact insertion point | Bridge call |
| --- | --- | --- | --- | --- |
| 1 | Done | `code/abstract.h`, `code/abstract.cpp`, `code/loco.h`, `code/loco.cpp` | `AbstractClass::Entity` declaration & copy safety; `~AbstractClass()` / `~LocomotionClass()`; `LocomotionClass::Link_To_Object` and `LocomotionClass::Post_Load` | `Attach_Entity` / `Replace_Entity`, `Set_Entity_Parent`, `Destroy_Entity` |
| 2 | Staged | Leaf `Unlimbo()` tails (`unit.cpp`, `infantry.cpp`, `building.cpp`, `aircraft.cpp`, `bullet.cpp`, `terrain.cpp`, `blight.cpp`, `partsys.cpp`, `vanim.cpp`, `wave.cpp`) | After leaf initialization succeeds and returns `true` | `FlecsWorld.Notify_Enter_World(Entity)` |
| 3 | Staged | `code/object.cpp` — `ObjectClass::Limbo()` (`line 1318`) | Inside `if (GameActive && !IsInLimbo)`, right before `Unselect()` and `Detach_All()` | `FlecsWorld.Notify_Exit_World(Entity)` |
| 4 | Staged | `code/scenario.cpp` — `Read_Scenario_INI()` (`line 2180`) | Wrap scenario loading with `Begin_Scenario_Load()` and flush immediately after `ScenarioInit--` | `FlecsWorld.Begin_Scenario_Load()` / `FlecsWorld.Flush_Scenario_Ready()` |
| 5 | Staged | `code/scenario.cpp` — `Post_Load_Game()` (`line 1020`) and `AbstractClass::Serialize` | Deserialize per-entity component blocks during load and resolve references at start of `Post_Load_Game()` | `FlecsWorld.Serialize_Entity(Entity, stream)` and `FlecsWorld.Post_Load_Resolve_All()` |
| 6 | Staged | `code/logic.cpp` — `LogicClass::AI()` (`lines 376` and `415`) | Once after the `Logic` object loop (`line 376`), and once after `Houses[house]->AI()` (`line 415`) | `FlecsWorld.Run_Sim_Phase(SimPhase::PostObjects)` and `FlecsWorld.Run_Sim_Phase(SimPhase::PostHouses)` |
| 7 | Staged | `code/tracker.cpp` — `Detach_This_From_All()` (`line 207`) | End of `Detach_This_From_All(target, all)` across all RTTI branches (unify early `return`s on `RTTI_HOUSE`, `RTTI_ANIM`, `As_ObjectClass()`, `As_AbstractTypeClass()` into a single exit) | `FlecsWorld.Detach_Target(target, all)` |
| 8 | Staged | `code/saveload.cpp` — `Object_CRCs()` (`line 340`) | Right after `OBJECT_HEAP_LIST(DO_OBJ_CRC)` and ahead of `crc((int)Frame)` | `FlecsWorld.Fold_CRCs(crc)` |
| 9 | Staged | `code/tactical.cpp` — `Tactical::Render()` (`line 1295`) and `code/startup.cpp` — `Prog_End()` (`line 967`) | Foreground pass after `Draw_Objects(true)` for viewport visuals; before `delete TacticalMap` / `delete Rule` / `delete Scen` at exit | `FlecsWorld.Render_Foreground(&TacticalRect)` and `FlecsWorld.Shutdown()` |

Notice the trap caught in hook #7 (`code/tracker.cpp:117-159`):
`Detach_This_From_All` executes early `return;` statements at lines 124
(`RTTI_HOUSE`), 134 (`RTTI_ANIM`), 148 (`As_ObjectClass()`), and 158
(`As_AbstractTypeClass()`). Placing `FlecsWorld.Detach_Target(target, all)` at
the bottom of `Detach_This_From_All` without replacing those four early
`return;` statements with structured `if / else if` branches would silently skip
component reference detachment for every `HouseClass`, `AnimClass`,
`ObjectClass`, and `AbstractTypeClass` in the engine.
