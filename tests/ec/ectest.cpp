/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Holds the static C++20 entity-component layer to its contract over the real
// flecs storage: 1:1 flecs::entity root lifetime across ObjectClass, CellClass,
// and LocomotionClass, native flecs paradigm accessors and observers, compile-time
// ComponentRegistry<Ts...> dispatch across all 12 global hooks (G01..G12) and
// 27 entity/component hooks (E01..E27), automatic flecs::Disabled suspension,
// two-subpass EC01 save trailer framing with pointer swizzle stability, and
// per-component / per-entity / per-type / global Compute_CRC calculation.
//
// Needs no game data.

#include "crc.h"
#include "ec/entity.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

// Stand-in engine types matching the signatures exercised by EC concepts and hooks,
// keeping the harness independent of Win32 and artwork subsystems (like housesettest).
using SwizzleIDType = std::uint32_t;
using Dir256 = int;
using PCPType = int;
using FacingType = int;
using FireErrorType = int;
using RadioMessageType = int;
using ResultType = int;

struct Coord { int X = 0; int Y = 0; int Z = 0; };
struct Point2D { int X = 0; int Y = 0; };
struct Rect { int X = 0; int Y = 0; int Width = 0; int Height = 0; };
struct Matrix3D { int Scale = 1; };

class WarheadTypeClass {};
class BulletClass { public: int Marker = 7; };
class HouseClass { public: int HeapID = 0; };
class RadioClass {};

class AbstractClass
{
	public:
		virtual ~AbstractClass(void) = default;
		int ID = 1;
		int Fetch_ID(void) const { return ID; }
};

class ObjectClass : public AbstractClass
{
	public:
		explicit ObjectClass(int id = 1, bool start_in_limbo = false)
			: Entity(EC::Create_Entity(this))
		{
			ID = id;
			if (start_in_limbo) {
				Entity.add(flecs::Disabled);
			}
		}

		~ObjectClass(void) override
		{
			EC::Destroy_Entity(Entity);
		}

		int Strength = 100;
		flecs::entity Entity;
};

class TechnoClass : public ObjectClass
{
	public:
		explicit TechnoClass(int id = 1, bool start_in_limbo = false)
			: ObjectClass(id, start_in_limbo)
		{
		}
};

class FootClass : public TechnoClass
{
	public:
		explicit FootClass(int id = 1, bool start_in_limbo = false)
			: TechnoClass(id, start_in_limbo)
		{
		}
};

class CellClass : public AbstractClass
{
	public:
		explicit CellClass(int cell_index = 0)
			: CellIndex(cell_index), Entity(EC::Create_Entity(this))
		{
			ID = cell_index;
		}

		~CellClass(void) override
		{
			EC::Destroy_Entity(Entity);
		}

		int CellIndex = 0;
		int Tiberium = 10;
		flecs::entity Entity;
};

struct ILocomotion
{
	virtual ~ILocomotion(void) = default;
	virtual flecs::entity Get_Entity(void) const noexcept = 0;
};

class LocomotionClass : public ILocomotion
{
	public:
		explicit LocomotionClass(FootClass * linked = nullptr)
			: Entity(EC::Create_Entity(this)), LinkedTo(linked)
		{
		}

		~LocomotionClass(void) override
		{
			EC::Destroy_Entity(Entity);
		}

		flecs::entity Get_Entity(void) const noexcept override { return Entity; }

		flecs::entity Entity;
		FootClass * LinkedTo = nullptr;
		bool IsPowered = true;
};

// Stand-in swizzle and save stream matching SaveStreamClass's contract, including
// BoundScope framing and delayed pointer patching at Resolve() time.
namespace {

struct TestSwizzler
{
	struct PendingPatch
	{
		void ** Slot;
		SwizzleIDType Id;
	};

	std::unordered_map<void const *, SwizzleIDType> ToId;
	std::unordered_map<SwizzleIDType, void *> ToPtr;
	std::vector<PendingPatch> Patches;
	SwizzleIDType NextId = 1;

	void Reset(void)
	{
		ToId.clear();
		ToPtr.clear();
		Patches.clear();
		NextId = 1;
	}

	SwizzleIDType ID_Of(void const * ptr)
	{
		if (ptr == nullptr) return 0;
		auto it = ToId.find(ptr);
		if (it != ToId.end()) return it->second;
		SwizzleIDType const id = NextId++;
		ToId.emplace(ptr, id);
		return id;
	}

	void Here_I_Am(SwizzleIDType id, void * ptr)
	{
		ToPtr[id] = ptr;
	}

	void * Address_Of(SwizzleIDType id) const
	{
		auto it = ToPtr.find(id);
		return it != ToPtr.end() ? it->second : nullptr;
	}

	void Request(void ** slot, SwizzleIDType id)
	{
		if (id == 0) {
			*slot = nullptr;
			return;
		}
		*slot = nullptr;
		Patches.push_back(PendingPatch{slot, id});
	}

	void Resolve(void)
	{
		for (PendingPatch const & patch : Patches) {
			*patch.Slot = Address_Of(patch.Id);
		}
		Patches.clear();
	}
} GSwizzler;

} // namespace

class SaveStreamClass
{
	public:
		enum ModeType { MODE_SAVE, MODE_LOAD };

		SaveStreamClass(std::vector<unsigned char> & buffer, ModeType mode) :
			Buffer(buffer),
			Mode(mode),
			Cursor(0),
			Limit(mode == MODE_LOAD ? static_cast<unsigned int>(buffer.size()) : 0xFFFFFFFFu),
			Failed(false)
		{
		}

		bool Is_Saving(void) const { return Mode == MODE_SAVE; }
		bool Is_Loading(void) const { return Mode == MODE_LOAD; }
		bool Was_Error(void) const { return Failed; }
		void Fail(void) { Failed = true; }
		unsigned int Offset(void) const { return Cursor; }
		unsigned int Size(void) const { return static_cast<unsigned int>(Buffer.size()); }

		class BoundScope
		{
			public:
				BoundScope(SaveStreamClass & s, unsigned int end) : Stream(s), Prev(s.Limit)
				{
					if (end <= Stream.Limit) {
						Stream.Limit = end;
					} else {
						Stream.Fail();
					}
				}
				~BoundScope(void) { Stream.Limit = Prev; }
			private:
				SaveStreamClass & Stream;
				unsigned int Prev;
		};

		void Overwrite_Bytes(unsigned int offset, void const * data, int length)
		{
			if (Failed || offset + static_cast<unsigned int>(length) > Buffer.size()) {
				Fail();
				return;
			}
			std::memcpy(Buffer.data() + offset, data, static_cast<std::size_t>(length));
		}

		void Serialize_Bytes(void * data, int length)
		{
			if (Failed || length < 0) {
				Fail();
				return;
			}
			if (Is_Saving()) {
				unsigned char const * raw = static_cast<unsigned char const *>(data);
				Buffer.insert(Buffer.end(), raw, raw + length);
				Cursor += static_cast<unsigned int>(length);
			} else {
				if (Cursor + static_cast<unsigned int>(length) > Limit) {
					Fail();
					return;
				}
				std::memcpy(data, Buffer.data() + Cursor, static_cast<std::size_t>(length));
				Cursor += static_cast<unsigned int>(length);
			}
		}

		bool Fits(int count, std::size_t each)
		{
			if (Is_Loading()) {
				std::size_t const room = static_cast<std::size_t>(Limit - Cursor) / (each > 0 ? each : 1);
				if (count < 0 || static_cast<std::size_t>(count) > room) {
					Fail();
					return false;
				}
			}
			return true;
		}

		template<typename T> requires (std::is_arithmetic_v<T> || std::is_enum_v<T>)
		void Serialize(T & value)
		{
			Serialize_Bytes(&value, static_cast<int>(sizeof(value)));
		}

		template<typename T> requires (std::is_base_of_v<AbstractClass, T> || std::is_base_of_v<LocomotionClass, T>)
		void Serialize(T * & ptr)
		{
			SwizzleIDType id = Is_Loading() ? 0 : GSwizzler.ID_Of(ptr);
			Serialize(id);
			if (Is_Loading() && !Was_Error()) {
				GSwizzler.Request(reinterpret_cast<void **>(&ptr), id);
			}
		}

	private:
		std::vector<unsigned char> & Buffer;
		ModeType Mode;
		unsigned int Cursor;
		unsigned int Limit;
		bool Failed;
};

// Global sentinel constructed BEFORE main() AND before Set_World_Init_Hook,
// reproducing code/map.cpp's global `CellClass BlubCell;` static init order!
static CellClass StaticBlubCellSentinel(9999);

namespace {

int GScenarioClears = 0;
std::vector<std::string> GOrderLog;

// 1. Generic component (works on any root, exercises void / flecs::entity global & observer hooks)
struct GenericPulseComp
{
	static constexpr std::string_view SaveName = "Test.GenericPulse";

	int Ticks = 0;
	int PostTicks = 0;
	int EnvTicks = 0;
	int FrameBegins = 0;
	int FrameEnds = 0;
	int PreRenders = 0;
	int PostRenders = 0;
	int ScenInits = 0;
	int PostLoads = 0;
	int Attached = 0;
	static inline int Removed = 0;

	void On_Attach(flecs::entity) { ++Attached; }
	void On_Remove(flecs::entity) { ++Removed; }

	void AI(void) { ++Ticks; GOrderLog.push_back("Generic.AI"); }
	void PostAI(flecs::entity) { ++PostTicks; GOrderLog.push_back("Generic.PostAI"); }
	void Environment_AI(void) { ++EnvTicks; }
	void Frame_Begin(void) { ++FrameBegins; }
	void Frame_End(void) { ++FrameEnds; }
	void Pre_Render(void) const { const_cast<GenericPulseComp *>(this)->PreRenders++; }
	void Post_Render(void) const { const_cast<GenericPulseComp *>(this)->PostRenders++; }
	void Scenario_Init(void) { ++ScenInits; }
	static void Scenario_Clear(void) { ++GScenarioClears; }

	void Serialize(SaveStreamClass & stream)
	{
		stream.Serialize(Ticks);
		stream.Serialize(PostTicks);
	}

	void Post_Load(void) { ++PostLoads; }

	void Compute_CRC(CRCEngine & crc) const
	{
		crc(Ticks);
		crc(PostTicks);
	}
};

// 2. Object-rooted component exercising all Object / Techno / Foot hooks + SwizzleTarget pointer
struct ObjectShieldComp
{
	using OwnerType = ObjectClass;
	static constexpr std::string_view SaveName = "Test.ObjectShield";

	int Shield = 20;
	ObjectClass * LinkedPeer = nullptr;
	int EventsMask = 0;
	bool BlockFire = false;
	static inline int DestroyedOwners = 0;

	void On_Attach(ObjectClass & self) { self.Strength += 5; }
	void On_Remove(ObjectClass & self) { self.Strength -= 5; ++DestroyedOwners; }

	void AI(ObjectClass & self)
	{
		self.Strength += 1;
		GOrderLog.push_back("Object.AI");
	}

	void PostAI(ObjectClass & self)
	{
		self.Strength += 2;
		GOrderLog.push_back("Object.PostAI");
	}

	void Detach(AbstractClass const * target, bool)
	{
		if (LinkedPeer == target) {
			LinkedPeer = nullptr;
		}
	}

	void Limbo(ObjectClass &) { EventsMask |= (1 << 0); }
	void Unlimbo(ObjectClass &, Coord const &, Dir256) { EventsMask |= (1 << 1); }
	void Delete_Me(ObjectClass &) { EventsMask |= (1 << 2); }

	void Take_Damage(ObjectClass &, int & damage, int, WarheadTypeClass const *, TechnoClass *, bool)
	{
		int const absorbed = damage < Shield ? damage : Shield;
		Shield -= absorbed;
		damage -= absorbed;
		EventsMask |= (1 << 3);
	}

	void Can_Fire(TechnoClass const &, AbstractClass *, int, FireErrorType & err) const
	{
		if (BlockFire) err = 99;
	}

	void Fire_At(TechnoClass &, AbstractClass *, int, BulletClass * bullet)
	{
		if (bullet != nullptr) bullet->Marker = 42;
		EventsMask |= (1 << 4);
	}

	void Record_The_Kill(TechnoClass &, TechnoClass *) { EventsMask |= (1 << 5); }
	void Captured(TechnoClass &, HouseClass *) { EventsMask |= (1 << 6); }
	void Assign_Target(TechnoClass &, AbstractClass *) { EventsMask |= (1 << 7); }
	void Assign_Destination(TechnoClass &, AbstractClass *) { EventsMask |= (1 << 8); }
	void Set_Coord(ObjectClass &, Coord const &, Coord const &) { EventsMask |= (1 << 9); }
	void Per_Cell_Process(FootClass &, PCPType) { EventsMask |= (1 << 10); }
	void Receive_Message(ObjectClass &, RadioClass *, RadioMessageType, std::intptr_t & param)
	{
		param = 777;
		EventsMask |= (1 << 11);
	}
	void Do_Cloak(TechnoClass &) { EventsMask |= (1 << 12); }
	void Do_Uncloak(TechnoClass &) { EventsMask |= (1 << 13); }
	void Stun(TechnoClass &) { EventsMask |= (1 << 14); }
	void Select(ObjectClass &) { EventsMask |= (1 << 15); }
	void Unselect(ObjectClass &) { EventsMask |= (1 << 16); }
	void Hidden(ObjectClass &) { EventsMask |= (1 << 17); }
	void Revealed(ObjectClass &, HouseClass *) { EventsMask |= (1 << 18); }
	void Draw_Pre_Render(ObjectClass const &, Point2D const &, Rect const &) const
	{
		const_cast<ObjectShieldComp *>(this)->EventsMask |= (1 << 19);
	}
	void Draw_It(ObjectClass const &, Point2D const &, Rect const &) const
	{
		const_cast<ObjectShieldComp *>(this)->EventsMask |= (1 << 20);
	}
	void Draw_Post_Render(ObjectClass const &, Point2D const &, Rect const &) const
	{
		const_cast<ObjectShieldComp *>(this)->EventsMask |= (1 << 21);
	}

	void Serialize(SaveStreamClass & stream)
	{
		stream.Serialize(Shield);
		stream.Serialize(LinkedPeer);
	}

	void Post_Load(ObjectClass &) { EventsMask |= (1 << 22); }

	void Compute_CRC(CRCEngine & crc) const
	{
		crc(Shield);
		crc(LinkedPeer != nullptr ? LinkedPeer->Fetch_ID() : -1);
	}
};

// 3. Cell-rooted component exercising all CellClass hooks
struct CellHazardComp
{
	using OwnerType = CellClass;
	static constexpr std::string_view SaveName = "Test.CellHazard";

	int Level = 3;
	int CellEvents = 0;

	void AI(CellClass & cell) { cell.Tiberium += Level; }
	void PostAI(CellClass & cell) { cell.Tiberium += 1; }

	void Occupy_Down(CellClass &, ObjectClass * obj, bool)
	{
		if (obj != nullptr) obj->Strength -= Level;
		CellEvents |= (1 << 0);
	}
	void Occupy_Up(CellClass &, ObjectClass *, bool) { CellEvents |= (1 << 1); }
	void Recalc_Passability(CellClass &) { CellEvents |= (1 << 2); }
	void Recalc_Attributes(CellClass &) { CellEvents |= (1 << 3); }
	void Grow_Tiberium(CellClass &) { CellEvents |= (1 << 4); }
	void Spread_Tiberium(CellClass &, bool) { CellEvents |= (1 << 5); }
	void Reduce_Tiberium(CellClass &, int) { CellEvents |= (1 << 6); }
	void Goodie_Check(CellClass &, FootClass *) { CellEvents |= (1 << 7); }
	void Cell_Draw_It(CellClass const &, Point2D const &, Rect const &) const
	{
		const_cast<CellHazardComp *>(this)->CellEvents |= (1 << 8);
	}
	void Draw_Overlay(CellClass &, Point2D const &, Rect const &) { CellEvents |= (1 << 9); }

	void Serialize(SaveStreamClass & stream) { stream.Serialize(Level); }
	void Compute_CRC(CRCEngine & crc) const { crc(Level); }
};

// 4. Locomotor-rooted component exercising all LocomotionClass hooks
struct LocoOverdriveComp
{
	using OwnerType = LocomotionClass;
	static constexpr std::string_view SaveName = "Test.LocoOverdrive";

	int Boost = 2;
	int LocoEvents = 0;

	void AI(LocomotionClass & loco)
	{
		if (loco.IsPowered && loco.LinkedTo != nullptr) {
			loco.LinkedTo->Strength += Boost;
		}
	}

	void Link_To_Object(LocomotionClass &, FootClass *) { LocoEvents |= (1 << 0); }
	void Loco_Process(LocomotionClass &) { LocoEvents |= (1 << 1); }
	void Loco_PostProcess(LocomotionClass &, bool) { LocoEvents |= (1 << 2); }
	void Move_To(LocomotionClass &, Coord) { LocoEvents |= (1 << 3); }
	void Stop_Moving(LocomotionClass &) { LocoEvents |= (1 << 4); }
	void Power_On(LocomotionClass &) { LocoEvents |= (1 << 5); }
	void Power_Off(LocomotionClass &) { LocoEvents |= (1 << 6); }
	void Begin_Piggyback(LocomotionClass &, ILocomotion *) { LocoEvents |= (1 << 7); }
	void End_Piggyback(LocomotionClass &, ILocomotion *) { LocoEvents |= (1 << 8); }
	void Draw_Matrix(LocomotionClass &, Matrix3D & mat, int *) { mat.Scale += Boost; LocoEvents |= (1 << 9); }

	void Serialize(SaveStreamClass & stream) { stream.Serialize(Boost); }
	void Compute_CRC(CRCEngine & crc) const { crc(Boost); }
};

// 5. Passive data-only component: defines NO hooks at all (must emit zero queries/calls)
struct PassiveDataComp
{
	int TagValue = 123;
};

struct UnregisteredComp
{
	int Dummy = 0;
};

} // namespace

#define EC_CUSTOM_REGISTRY_COMPONENTS \
	GenericPulseComp, \
	ObjectShieldComp, \
	CellHazardComp, \
	LocoOverdriveComp, \
	PassiveDataComp

#include "ec/registry.h"

namespace {

using TestRegistry = EC::GameComponents;

void Register_Test_Components(flecs::world & world)
{
	TestRegistry::Register_All(world);
}

// Compile-time static typing guarantees:
static_assert(TestRegistry::count == 5);
static_assert(TestRegistry::contains<GenericPulseComp>);
static_assert(!TestRegistry::contains<UnregisteredComp>);
static_assert(EC::AllowedOnRoot<GenericPulseComp, ObjectClass>);
static_assert(EC::AllowedOnRoot<GenericPulseComp, CellClass>);
static_assert(EC::AllowedOnRoot<GenericPulseComp, LocomotionClass>);
static_assert(EC::AllowedOnRoot<ObjectShieldComp, ObjectClass>);
static_assert(!EC::AllowedOnRoot<ObjectShieldComp, CellClass>);
static_assert(!EC::AllowedOnRoot<ObjectShieldComp, LocomotionClass>);
static_assert(EC::AllowedOnRoot<CellHazardComp, CellClass>);
static_assert(!EC::AllowedOnRoot<CellHazardComp, ObjectClass>);
static_assert(EC::AllowedOnRoot<LocoOverdriveComp, LocomotionClass>);
static_assert(!EC::AllowedOnRoot<LocoOverdriveComp, ObjectClass>);

int Failures = 0;
int Checked = 0;

void Check(bool passed, char const * what)
{
	++Checked;
	if (!passed) {
		std::printf("FAILED: %s\n", what);
		++Failures;
	}
}

std::size_t Count_Root_Entities(void)
{
	std::size_t total = 0;
	EC::World().query_builder<EC::OwnerLink<ObjectClass> const>().term_at(0).flags(flecs::Self).with(flecs::Disabled).optional().each([&](EC::OwnerLink<ObjectClass> const &) { ++total; });
	EC::World().query_builder<EC::OwnerLink<CellClass> const>().term_at(0).flags(flecs::Self).with(flecs::Disabled).optional().each([&](EC::OwnerLink<CellClass> const &) { ++total; });
	EC::World().query_builder<EC::OwnerLink<LocomotionClass> const>().term_at(0).flags(flecs::Self).with(flecs::Disabled).optional().each([&](EC::OwnerLink<LocomotionClass> const &) { ++total; });
	return total;
}

void Test_Eager_1To1_Lifecycle_And_Cell_Operations(void)
{
	// 1. Static sentinel constructed BEFORE Set_World_Init_Hook already owns its 1:1 flecs::entity!
	Check(StaticBlubCellSentinel.Entity.id() != 0 && StaticBlubCellSentinel.Entity.is_alive(),
		"static BlubCell constructed before main() owns a live 1:1 flecs::entity");
	Check(StaticBlubCellSentinel.Entity.get<EC::OwnerLink<CellClass>>().ptr == &StaticBlubCellSentinel,
		"static BlubCell OwnerLink points to StaticBlubCellSentinel");
	Check(!EC::Has_User_Components(StaticBlubCellSentinel.Entity),
		"plain BlubCell has Has_User_Components == false");
	Check(Count_Root_Entities() == 1, "exactly 1 root entity (StaticBlubCellSentinel) before test roots");

	// 2. 1:1 constructor/destructor lifecycle + native flecs add/emplace/set/get/remove + observers.
	ObjectShieldComp::DestroyedOwners = 0;
	flecs::entity_t destroyed_unit_id = 0;
	{
		FootClass unit(10, /*start_in_limbo=*/true);
		destroyed_unit_id = unit.Entity.id();
		Check(unit.Entity.is_alive() && unit.Entity.has(flecs::Disabled),
			"ObjectClass constructor creates live flecs::entity immediately (with Disabled in limbo)");
		Check(!EC::Has_User_Components(unit.Entity),
			"Has_User_Components ignores OwnerLink + flecs::Disabled on plain limbo object");
		Check(Count_Root_Entities() == 2, "two live root entities after FootClass construction");

		unit.Entity.add<ObjectShieldComp>();
		Check(EC::Has_User_Components(unit.Entity), "Has_User_Components becomes true when component added");
		Check(unit.Strength == 105, "native flecs on_add observer ran On_Attach(ObjectClass&)");

		auto & shield = unit.Entity.get_mut<ObjectShieldComp>();
		shield.Shield = 35;
		Check(unit.Entity.get<ObjectShieldComp>().Shield == 35, "native flecs::entity::get<C>() reads live slot");

		unit.Entity.set<PassiveDataComp>({77});
		Check(unit.Entity.has<PassiveDataComp>() && unit.Entity.try_get<PassiveDataComp>()->TagValue == 77,
			"native flecs::entity::set<C> / has<C> / try_get<C> work directly");

		unit.Entity.remove<ObjectShieldComp>();
		Check(!unit.Entity.has<ObjectShieldComp>(), "native flecs::entity::remove<C> detaches component");
		Check(unit.Strength == 100 && ObjectShieldComp::DestroyedOwners == 1,
			"native flecs on_remove observer ran On_Remove(ObjectClass&) on remove<C>");

		unit.Entity.remove<PassiveDataComp>();
		Check(!EC::Has_User_Components(unit.Entity),
			"Has_User_Components returns to false once all user components are removed");

		// Re-add ObjectShieldComp so ~FootClass() exercises destructor 1:1 teardown + on_remove!
		unit.Entity.add<ObjectShieldComp>();
		Check(unit.Strength == 105, "re-added ObjectShieldComp before ~FootClass()");
	}
	Check(!EC::World().entity(destroyed_unit_id).is_alive(),
		"~ObjectClass() destroyed flecs::entity 1:1 immediately on scope exit");
	Check(ObjectShieldComp::DestroyedOwners == 2,
		"destroying root entity fired native flecs on_remove observer");
	Check(Count_Root_Entities() == 1, "root entity count returned to 1 after ~FootClass()");

	// 3. Cell placement-new (Destroy_Entity + new (ptr) CellClass) and Set_Map_Dimensions shift.
	{
		CellClass cell_a(101);
		cell_a.Entity.set<CellHazardComp>({4, 0});
		Check(Count_Root_Entities() == 2, "two root entities with StaticBlubCell + cell_a");

		// Simulate Set_Map_Dimensions(!reset_cells) shift from cell_a to cell_b:
		flecs::entity shifted = cell_a.Entity;
		cell_a.Entity = flecs::entity::null();
		EC::Destroy_Entity(cell_a.Entity);
		new (&cell_a) CellClass(101);

		CellClass cell_b(202);
		Check(Count_Root_Entities() == 4, "cell_a (rebuilt) + shifted + cell_b + BlubCell before adoption");

		EC::Destroy_Entity(cell_b.Entity);
		cell_b = cell_a;
		cell_b.CellIndex = 202;
		cell_b.Entity = shifted;
		shifted = flecs::entity::null();
		cell_b.Entity.set<EC::OwnerLink<CellClass>>({&cell_b});
		// Give cell_a a fresh entity since *cell_b = cell_a copied cell_a.Entity if we hadn't overwritten it —
		// wait: in map.cpp *c = *cptr copies from the raw buffer where cptr->Entity was nulled!
		// Reproduce map.cpp's exact raw-buffer copy sequence:
		EC::Destroy_Entity(cell_a.Entity);
		new (&cell_a) CellClass(101);

		Check(Count_Root_Entities() == 3, "zero leaked entities after Set_Map_Dimensions shift");

		TestRegistry::AI_All(EC::World());
		Check(cell_b.Tiberium == 14, "OwnerLink<CellClass> rebound to shifted cell_b address");
		Check(cell_a.Tiberium == 10, "rebuilt cell_a untouched after shift");

		// Simulate Scenario_Clear_All: StaticBlubCellSentinel.Entity must remain alive!
		TestRegistry::Scenario_Clear_All();
		Check(StaticBlubCellSentinel.Entity.is_alive(),
			"Scenario_Clear_All preserves live 1:1 entities such as BlubCell");
	}
	Check(Count_Root_Entities() == 1, "all local CellClass entities destroyed 1:1 at scope exit");
}

void Test_All_39_Global_And_Entity_Hooks_And_Disabled_State(void)
{
	GOrderLog.clear();
	GScenarioClears = 0;

	FootClass foot(50, /*start_in_limbo=*/false);
	ObjectClass peer(51, /*start_in_limbo=*/false);
	CellClass cell(7);
	LocomotionClass active_loco(&foot);
	LocomotionClass piggy_loco(&foot);
	ILocomotion * loco_iface = &active_loco;

	// Add component directly through ILocomotion::Get_Entity() in native flecs paradigm!
	loco_iface->Get_Entity().set<LocoOverdriveComp>({3, 0});

	// Piggybacked locomotor receives flecs::Disabled + component: must NOT tick in AI_All!
	piggy_loco.Entity.add(flecs::Disabled);
	piggy_loco.Entity.set<LocoOverdriveComp>({100, 0});
	Check(piggy_loco.Entity.has(flecs::Disabled), "piggybacked locomotor carries flecs::Disabled");

	foot.Entity.add<GenericPulseComp>();
	foot.Entity.set<ObjectShieldComp>({20, &peer, 0, false});
	cell.Entity.set<CellHazardComp>({5, 0});

	// Downstream ad-hoc native flecs query works directly over EC::World()!
	int custom_query_hits = 0;
	EC::World().each([&](ObjectShieldComp const & sh, EC::OwnerLink<ObjectClass> const & link) {
		if (link.ptr == &foot && sh.LinkedPeer == &peer) {
			++custom_query_hits;
		}
	});
	Check(custom_query_hits == 1, "downstream native flecs::world::each query inspects components directly");

	// Global hooks G01..G09
	TestRegistry::Frame_Begin_All(EC::World());
	TestRegistry::Pre_Render_All(EC::World());
	TestRegistry::Post_Render_All(EC::World());
	TestRegistry::AI_All(EC::World());
	TestRegistry::PostAI_All(EC::World());
	TestRegistry::Environment_AI_All(EC::World());
	TestRegistry::Frame_End_All(EC::World());
	TestRegistry::Scenario_Init_All(EC::World());
	TestRegistry::Scenario_Clear_All();

	{
		auto const & pulse = foot.Entity.get<GenericPulseComp>();
		Check(pulse.Ticks == 1 && pulse.PostTicks == 1 && pulse.EnvTicks == 1, "AI/PostAI/Environment_AI ran");
		Check(pulse.FrameBegins == 1 && pulse.FrameEnds == 1, "Frame_Begin/Frame_End ran");
		Check(pulse.PreRenders == 1 && pulse.PostRenders == 1, "Pre_Render/Post_Render ran");
		Check(pulse.ScenInits == 1 && GScenarioClears == 1, "Scenario_Init/Scenario_Clear ran");
	}
	// Strength: 100 + 5 (On_Attach) + 1 (ObjectShield.AI) + 3 (active_loco.AI; piggy_loco skipped!) + 2 (PostAI) = 111
	Check(foot.Strength == 111, "active_loco ticked while piggy_loco was skipped via flecs::Disabled");
	Check(GOrderLog.size() == 4
		&& GOrderLog[0] == "Generic.AI"
		&& GOrderLog[1] == "Object.AI"
		&& GOrderLog[2] == "Generic.PostAI"
		&& GOrderLog[3] == "Object.PostAI", "deterministic registration-order AI and PostAI dispatch");

	// Limbo suspension via flecs::Disabled (moves entity between enabled and Disabled tables)
	TestRegistry::Dispatch_Limbo(foot);
	foot.Entity.add(flecs::Disabled);
	TestRegistry::AI_All(EC::World());
	Check(foot.Entity.get<GenericPulseComp>().Ticks == 1, "Limbo entity skipped by cached AI_All query");
	foot.Entity.remove(flecs::Disabled);
	TestRegistry::Dispatch_Unlimbo(foot, Coord{}, 0);
	TestRegistry::AI_All(EC::World());
	Check(foot.Entity.get<GenericPulseComp>().Ticks == 2, "Unlimbo re-enables entity for cached AI_All query");

	// Remaining Object / Techno / Foot entity hooks (E02..E16)
	auto & shield = foot.Entity.get_mut<ObjectShieldComp>();
	int dmg = 25;
	WarheadTypeClass wh;
	TestRegistry::Dispatch_Take_Damage(foot, dmg, 0, &wh, &foot, false);
	Check(dmg == 5 && shield.Shield == 0, "Take_Damage absorbed 20 shield points");

	shield.BlockFire = true;
	FireErrorType ferr = 0;
	TestRegistry::Dispatch_Can_Fire(foot, &peer, 0, ferr);
	Check(ferr == 99, "Can_Fire intercepted verdict");

	BulletClass bullet;
	TestRegistry::Dispatch_Fire_At(foot, &peer, 0, &bullet);
	Check(bullet.Marker == 42, "Fire_At modified bullet");

	TestRegistry::Dispatch_Record_The_Kill(foot, &foot);
	HouseClass house;
	TestRegistry::Dispatch_Captured(foot, &house);
	TestRegistry::Dispatch_Assign_Target(foot, &peer);
	TestRegistry::Dispatch_Assign_Destination(foot, &peer);
	TestRegistry::Dispatch_Set_Coord(foot, Coord{0, 0, 0}, Coord{1, 2, 3});
	TestRegistry::Dispatch_Per_Cell_Process(foot, 1);
	std::intptr_t msg_param = 0;
	TestRegistry::Dispatch_Receive_Message(foot, nullptr, 0, msg_param);
	Check(msg_param == 777, "Receive_Message updated param");
	TestRegistry::Dispatch_Do_Cloak(foot);
	TestRegistry::Dispatch_Do_Uncloak(foot);
	TestRegistry::Dispatch_Stun(foot);
	TestRegistry::Dispatch_Select(foot);
	TestRegistry::Dispatch_Unselect(foot);
	TestRegistry::Dispatch_Hidden(foot);
	TestRegistry::Dispatch_Revealed(foot, &house);
	TestRegistry::Dispatch_Draw_Pre_Render(foot, Point2D{}, Rect{});
	TestRegistry::Dispatch_Draw_It(foot, Point2D{}, Rect{});
	TestRegistry::Dispatch_Draw_Post_Render(foot, Point2D{}, Rect{});
	TestRegistry::Dispatch_Delete_Me(foot);
	Check((shield.EventsMask & ((1 << 22) - 1)) == ((1 << 22) - 1), "all 22 Object/Techno/Foot event hooks fired");

	TestRegistry::Dispatch_Detach(foot, &peer, true);
	Check(shield.LinkedPeer == nullptr, "Detach cleared dangling peer pointer");

	// Cell entity hooks (E17..E21)
	int const prev_strength = foot.Strength;
	TestRegistry::Dispatch_Occupy_Down(cell, &foot, false);
	Check(foot.Strength == prev_strength - 5, "Cell Occupy_Down applied hazard");
	TestRegistry::Dispatch_Occupy_Up(cell, &foot, false);
	TestRegistry::Dispatch_Recalc_Passability(cell);
	TestRegistry::Dispatch_Recalc_Attributes(cell);
	TestRegistry::Dispatch_Grow_Tiberium(cell);
	TestRegistry::Dispatch_Spread_Tiberium(cell, true);
	TestRegistry::Dispatch_Reduce_Tiberium(cell, 2);
	TestRegistry::Dispatch_Goodie_Check(cell, &foot);
	TestRegistry::Dispatch_Cell_Draw_It(cell, Point2D{}, Rect{});
	TestRegistry::Dispatch_Draw_Overlay(cell, Point2D{}, Rect{});
	Check(cell.Entity.get<CellHazardComp>().CellEvents == ((1 << 10) - 1), "all 10 CellClass event hooks fired");

	// Locomotor entity hooks (E22..E26)
	Matrix3D mat{1};
	TestRegistry::Dispatch_Link_To_Object(active_loco, &foot);
	TestRegistry::Dispatch_Loco_Process(active_loco);
	TestRegistry::Dispatch_Loco_PostProcess(active_loco, true);
	TestRegistry::Dispatch_Move_To(active_loco, Coord{4, 5, 6});
	TestRegistry::Dispatch_Stop_Moving(active_loco);
	TestRegistry::Dispatch_Power_Off(active_loco);
	TestRegistry::Dispatch_Power_On(active_loco);
	TestRegistry::Dispatch_Begin_Piggyback(active_loco, &piggy_loco);
	Check(piggy_loco.Entity.has(flecs::Disabled), "Begin_Piggyback sets Disabled on carried locomotor");
	TestRegistry::Dispatch_End_Piggyback(active_loco, &piggy_loco);
	Check(!piggy_loco.Entity.has(flecs::Disabled), "End_Piggyback syncs Disabled from active carrier locomotor");
	TestRegistry::Dispatch_Draw_Matrix(active_loco, mat, nullptr);
	Check(mat.Scale == 4, "Draw_Matrix adjusted voxel Matrix3D");
	Check(active_loco.Entity.get<LocoOverdriveComp>().LocoEvents == ((1 << 10) - 1), "all 10 LocomotionClass event hooks fired");
}

void Test_Two_Pass_Save_Trailer_And_Swizzle_Stability(void)
{
	GSwizzler.Reset();

	// 1. Legacy .SAV without EC01 trailer (Offset == Size) loads cleanly as no-op.
	{
		std::vector<unsigned char> legacy_bytes = {1, 2, 3, 4};
		SaveStreamClass legacy_load(legacy_bytes, SaveStreamClass::MODE_LOAD);
		int header = 0;
		legacy_load.Serialize(header);
		bool const ok = TestRegistry::Load_Trailer(
			EC::World(),
			legacy_load,
			[](EC::RootKind, SwizzleIDType) -> flecs::entity { return flecs::entity::null(); });
		Check(ok && !legacy_load.Was_Error(), "legacy .SAV with no EC01 trailer succeeds cleanly");
	}

	// 2. Stress test >128 entities with multiple components + SwizzleTarget pointers
	// (plus thousands of plain componentless cells!) to prove:
	// - Save_Trailer iterates only serializable component archetypes (ignoring plain cells),
	// - Load_Trailer Subpass 1 freezes flecs tables before Subpass 2 registers Swizzle slots!
	constexpr int N = 140;
	std::vector<CellClass *> plain_cells;
	plain_cells.reserve(500);
	for (int i = 0; i < 500; ++i) {
		plain_cells.push_back(new CellClass(5000 + i));
	}

	std::vector<FootClass *> saved_units;
	saved_units.reserve(N);
	for (int i = 0; i < N; ++i) {
		saved_units.push_back(new FootClass(1000 + i, /*start_in_limbo=*/(i % 3) == 0));
	}

	std::vector<unsigned char> archive;
	{
		CellClass saved_cell(77);
		LocomotionClass saved_loco(saved_units[0]);

		for (int i = 0; i < N; ++i) {
			saved_units[i]->Entity.set<ObjectShieldComp>({10 + i, saved_units[(i + 1) % N], 0, false});
			auto & gp = saved_units[i]->Entity.ensure<GenericPulseComp>();
			gp.Ticks = 500 + i;
			gp.PostTicks = 600 + i;
		}
		saved_cell.Entity.set<CellHazardComp>({9, 0});
		saved_loco.Entity.set<LocoOverdriveComp>({7, 0});

		for (FootClass * u : saved_units) {
			(void)GSwizzler.ID_Of(static_cast<ObjectClass *>(u));
		}
		(void)GSwizzler.ID_Of(&saved_cell);
		(void)GSwizzler.ID_Of(&saved_loco);

		SaveStreamClass save_stream(archive, SaveStreamClass::MODE_SAVE);
		int base_sentinel = 0x12345678;
		save_stream.Serialize(base_sentinel);
		bool const saved_ok = TestRegistry::Save_Trailer(
			EC::World(),
			save_stream,
			[](EC::RootKind, void const * ptr) -> SwizzleIDType {
				return GSwizzler.ID_Of(ptr);
			});
		Check(saved_ok && !save_stream.Was_Error(), "EC01 trailer saved cleanly");

		// Destroy saved scenario objects 1:1 (just as Clear_Scenario does before Load_Game!).
		for (FootClass * u : saved_units) delete u;
		saved_units.clear();
		for (CellClass * c : plain_cells) delete c;
		plain_cells.clear();
	}
	Check(Count_Root_Entities() == 1, "1:1 scenario teardown left only StaticBlubCellSentinel alive");

	// Reconstruct fresh target objects at DIFFERENT memory addresses (with their own 1:1 entities!).
	std::vector<FootClass *> loaded_units;
	loaded_units.reserve(N);
	GSwizzler.ToPtr.clear();
	GSwizzler.Patches.clear();
	for (int i = 0; i < N; ++i) {
		FootClass * u = new FootClass(1000 + i, /*start_in_limbo=*/(i % 3) == 0);
		loaded_units.push_back(u);
		GSwizzler.Here_I_Am(static_cast<SwizzleIDType>(i + 1), static_cast<ObjectClass *>(u));
	}
	CellClass loaded_cell(77);
	LocomotionClass loaded_loco(loaded_units[0]);
	GSwizzler.Here_I_Am(static_cast<SwizzleIDType>(N + 1), &loaded_cell);
	GSwizzler.Here_I_Am(static_cast<SwizzleIDType>(N + 2), &loaded_loco);

	{
		SaveStreamClass load_stream(archive, SaveStreamClass::MODE_LOAD);
		int base_sentinel = 0;
		load_stream.Serialize(base_sentinel);
		Check(base_sentinel == 0x12345678, "base stream prefix intact before EC01 trailer");

		bool const loaded_ok = TestRegistry::Load_Trailer(
			EC::World(),
			load_stream,
			[](EC::RootKind kind, SwizzleIDType id) -> flecs::entity {
				void * raw = GSwizzler.Address_Of(id);
				if (raw == nullptr) return flecs::entity::null();
				if (kind == EC::RootKind::Object) return static_cast<ObjectClass *>(raw)->Entity;
				if (kind == EC::RootKind::Cell) return static_cast<CellClass *>(raw)->Entity;
				return static_cast<LocomotionClass *>(raw)->Entity;
			});
		Check(loaded_ok && !load_stream.Was_Error(), "EC01 trailer loaded onto existing 1:1 entities cleanly");
		Check(load_stream.Offset() == load_stream.Size(), "EC01 trailer consumed exact archive length");

		// Resolve all delayed swizzle pointers AFTER Load_Trailer (matching Load_Game).
		GSwizzler.Resolve();
		TestRegistry::Post_Load_All(EC::World());
	}

	bool all_restored = true;
	for (int i = 0; i < N; ++i) {
		auto const * sh = loaded_units[i]->Entity.try_get<ObjectShieldComp>();
		auto const * gp = loaded_units[i]->Entity.try_get<GenericPulseComp>();
		if (sh == nullptr || gp == nullptr) {
			all_restored = false;
			break;
		}
		if (sh->Shield != 10 + i
			|| sh->LinkedPeer != loaded_units[(i + 1) % N]
			|| (sh->EventsMask & (1 << 22)) == 0
			|| gp->Ticks != 500 + i
			|| gp->PostTicks != 600 + i
			|| gp->PostLoads != 1
			|| loaded_units[i]->Entity.has(flecs::Disabled) != ((i % 3) == 0)) {
			all_restored = false;
			break;
		}
	}
	Check(all_restored, "all 140 entities restored exact fields, Disabled flags, Post_Load, and swizzled pointers");
	Check(loaded_cell.Entity.get<CellHazardComp>().Level == 9, "CellHazardComp restored from EC01 trailer");
	Check(loaded_loco.Entity.get<LocoOverdriveComp>().Boost == 7, "LocoOverdriveComp restored from EC01 trailer");

	for (FootClass * u : loaded_units) delete u;
}

void Test_Four_Level_Per_Component_CRC(void)
{
	FootClass u1(10);
	FootClass u2(20);
	CellClass c1(300);
	LocomotionClass l1(&u1);

	u1.Entity.set<ObjectShieldComp>({50, &u2, 0, false});
	auto & p1 = u1.Entity.ensure<GenericPulseComp>();
	p1.Ticks = 9;

	u2.Entity.set<ObjectShieldComp>({50, &u2, 0, false});

	c1.Entity.set<CellHazardComp>({11, 0});
	l1.Entity.set<LocoOverdriveComp>({4, 0});

	auto const & s1 = u1.Entity.get<ObjectShieldComp>();
	auto & s2 = u2.Entity.get_mut<ObjectShieldComp>();

	// Level 1 (E27a): Single component CRC in isolation
	int const single_s1 = EC::Single_Component_CRC(s1);
	int const single_s2 = EC::Single_Component_CRC(s2);
	Check(single_s1 == single_s2 && single_s1 != 0, "identical component instances produce identical individual CRC");

	CRCEngine crc_only_shield_u1;
	bool const has_s1 = TestRegistry::Compute_Entity_Component_CRC<ObjectShieldComp>(u1.Entity, crc_only_shield_u1);
	CRCEngine crc_only_shield_u2;
	bool const has_s2 = TestRegistry::Compute_Entity_Component_CRC<ObjectShieldComp>(u2.Entity, crc_only_shield_u2);
	Check(has_s1 && has_s2 && crc_only_shield_u1() == crc_only_shield_u2(),
		"Compute_Entity_Component_CRC<C> isolates single component from sibling GenericPulseComp");

	// Level 2 (E27b): Per-entity CRC across all attached components
	CRCEngine entity_u1_crc;
	TestRegistry::Compute_Entity_CRC(u1.Entity, entity_u1_crc);
	CRCEngine entity_u2_crc;
	TestRegistry::Compute_Entity_CRC(u2.Entity, entity_u2_crc);
	Check(entity_u1_crc() != entity_u2_crc(), "per-entity CRC differs when u1 carries extra GenericPulseComp");

	auto owner_key_fn = [](EC::RootKind kind, void const * ptr) -> EC::StableOwnerKey {
		if (kind == EC::RootKind::Object) {
			return EC::StableOwnerKey{0, static_cast<ObjectClass const *>(ptr)->Fetch_ID(), 0};
		}
		if (kind == EC::RootKind::Cell) {
			return EC::StableOwnerKey{1, static_cast<CellClass const *>(ptr)->CellIndex, 0};
		}
		auto const * loco = static_cast<LocomotionClass const *>(ptr);
		return EC::StableOwnerKey{2, loco->LinkedTo ? loco->LinkedTo->Fetch_ID() : -1, 0};
	};

	CRCEngine global_before;
	TestRegistry::Compute_CRC_All(EC::World(), global_before, owner_key_fn);

	s2.Shield = 51;
	Check(EC::Single_Component_CRC(s2) != single_s1, "mutating component field changes its individual CRC");

	CRCEngine global_after;
	TestRegistry::Compute_CRC_All(EC::World(), global_after, owner_key_fn);
	Check(global_before() != global_after(), "mutating component field changes global CRC");

	// Level 3 (G12a): Per-component-type summary & per-instance dump (Print_Component_CRCs)
	FILE * tmp = std::tmpfile();
	Check(tmp != nullptr, "opened tmpfile for Print_Component_CRCs");
	if (tmp != nullptr) {
		TestRegistry::Print_Component_CRCs(EC::World(), tmp, owner_key_fn);
		std::fflush(tmp);
		long const sz = std::ftell(tmp);
		std::rewind(tmp);
		std::string report(static_cast<std::size_t>(sz > 0 ? sz : 0), '\0');
		if (sz > 0) {
			(void)std::fread(report.data(), 1, report.size(), tmp);
		}
		std::fclose(tmp);

		Check(report.find("Test.GenericPulse") != std::string::npos, "report lists GenericPulseComp summary");
		Check(report.find("Test.ObjectShield") != std::string::npos, "report lists ObjectShieldComp summary");
		Check(report.find("Test.CellHazard") != std::string::npos, "report lists CellHazardComp summary");
		Check(report.find("Test.LocoOverdrive") != std::string::npos, "report lists LocoOverdriveComp summary");
		Check(report.find("Owner:Obj:10") != std::string::npos && report.find("Owner:Obj:20") != std::string::npos,
			"report prints individual per-component rows keyed by stable owner ID");
	}
}

} // namespace

int main(void)
{
	EC::Set_World_Init_Hook(&Register_Test_Components);

	Test_Eager_1To1_Lifecycle_And_Cell_Operations();
	Test_All_39_Global_And_Entity_Hooks_And_Disabled_State();
	Test_Two_Pass_Save_Trailer_And_Swizzle_Stability();
	Test_Four_Level_Per_Component_CRC();

	EC::World_Shutdown();

	std::printf("EC tests: %d checked, %d failed\n", Checked, Failures);
	return Failures == 0 ? 0 : 1;
}
