/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "crc.h"
#include "flecs/world.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

// Minimal standalone definitions of engine types forward-declared by the
// lifecycle header so this harness runs cross-platform without Win32 headers.
class AbstractClass
{
	public:
		int Kind = 0;
		int Id = -1;
		bool IsActive = true;
		bool IsInLimbo = false;
};


class SaveStreamClass
{
	public:
		enum ModeType {
			MODE_SAVE,
			MODE_LOAD,
		};

		SaveStreamClass(std::vector<unsigned char> & buffer, ModeType mode) :
			Buffer(buffer),
			Mode(mode),
			Cursor(0),
			Failed(false)
		{
		}

		bool Is_Saving(void) const
		{
			return(Mode == MODE_SAVE);
		}

		bool Is_Loading(void) const
		{
			return(Mode == MODE_LOAD);
		}

		bool Was_Error(void) const
		{
			return(Failed);
		}

		void Fail(void)
		{
			Failed = true;
		}

		unsigned int Offset(void) const
		{
			return(Cursor);
		}

		unsigned int Size(void) const
		{
			return(static_cast<unsigned int>(Buffer.size()));
		}

		void Overwrite_Bytes(unsigned int offset, void const * data, int length)
		{
			if (Failed || length < 0 || offset + static_cast<unsigned int>(length) > Buffer.size()) {
				Failed = true;
				return;
			}
			if (length > 0) {
				std::memcpy(Buffer.data() + offset, data, static_cast<std::size_t>(length));
			}
		}

		void Serialize_Bytes(void * data, int length)
		{
			if (Failed || length < 0) {
				Failed = true;
				return;
			}
			if (length == 0) {
				return;
			}
			std::size_t const count = static_cast<std::size_t>(length);
			if (Mode == MODE_SAVE) {
				std::size_t const pos = Buffer.size();
				Buffer.resize(pos + count);
				std::memcpy(Buffer.data() + pos, data, count);
				Cursor += static_cast<unsigned int>(count);
				return;
			}
			if (Cursor + count > Buffer.size()) {
				Failed = true;
				return;
			}
			std::memcpy(data, Buffer.data() + Cursor, count);
			Cursor += static_cast<unsigned int>(count);
		}

		template<typename T>
		void Write_Pod(T const & value)
		{
			static_assert(std::is_trivially_copyable_v<T>);
			T copy = value;
			Serialize_Bytes(&copy, static_cast<int>(sizeof(T)));
		}

		template<typename T>
		void Read_Pod(T & value)
		{
			static_assert(std::is_trivially_copyable_v<T>);
			Serialize_Bytes(&value, static_cast<int>(sizeof(T)));
		}

	private:
		std::vector<unsigned char> & Buffer;
		ModeType Mode;
		unsigned int Cursor;
		bool Failed;
};


namespace {

int Failures = 0;

void Check(bool condition, char const * what)
{
	if (!condition) {
		std::printf("FAIL: %s\n", what);
		Failures++;
	}
}


ObjectStateInfo Inspect_Mock_Object(void const * raw, bool is_locomotor)
{
	if (raw == nullptr || is_locomotor) {
		return(ObjectStateInfo{});
	}
	AbstractClass const * obj = static_cast<AbstractClass const *>(raw);
	ObjectStateInfo info;
	info.Kind = obj->Kind;
	info.FetchId = obj->Id;
	info.IsActive = obj->IsActive;
	info.IsInLimbo = obj->IsInLimbo;
	return(info);
}


std::vector<std::string> EventLog;
std::uint32_t SharedPrng = 0x12345678U;

std::uint32_t Next_Prng(void)
{
	SharedPrng ^= SharedPrng << 13;
	SharedPrng ^= SharedPrng >> 17;
	SharedPrng ^= SharedPrng << 5;
	return(SharedPrng);
}


struct NonTrivialPayloadComponent {
	static inline int LiveInstances = 0;

	std::vector<int> Samples;
	std::string Label;

	NonTrivialPayloadComponent(void)
	{
		LiveInstances++;
	}

	explicit NonTrivialPayloadComponent(std::string label, std::initializer_list<int> values) :
		Samples(values),
		Label(std::move(label))
	{
		LiveInstances++;
	}

	NonTrivialPayloadComponent(NonTrivialPayloadComponent const & other) :
		Samples(other.Samples),
		Label(other.Label)
	{
		LiveInstances++;
	}

	NonTrivialPayloadComponent(NonTrivialPayloadComponent && other) noexcept :
		Samples(std::move(other.Samples)),
		Label(std::move(other.Label))
	{
		LiveInstances++;
	}

	NonTrivialPayloadComponent & operator=(NonTrivialPayloadComponent const & other) = default;
	NonTrivialPayloadComponent & operator=(NonTrivialPayloadComponent && other) noexcept = default;

	~NonTrivialPayloadComponent(void)
	{
		LiveInstances--;
	}

	void Serialize(SaveStreamClass & stream)
	{
		if (stream.Is_Saving()) {
			int const count = static_cast<int>(Samples.size());
			stream.Write_Pod(count);
			for (int sample : Samples) {
				stream.Write_Pod(sample);
			}
		} else {
			int count = 0;
			stream.Read_Pod(count);
			if (count < 0 || count > 1024) {
				stream.Fail();
				return;
			}
			Samples.resize(static_cast<std::size_t>(count));
			for (int i = 0; i < count; ++i) {
				stream.Read_Pod(Samples[static_cast<std::size_t>(i)]);
			}
		}
	}
};


struct MoveOnlyResourceComponent {
	std::unique_ptr<int> Handle;

	MoveOnlyResourceComponent(void) = default;

	explicit MoveOnlyResourceComponent(int value) :
		Handle(std::make_unique<int>(value))
	{
	}

	MoveOnlyResourceComponent(MoveOnlyResourceComponent const &) = delete;
	MoveOnlyResourceComponent & operator=(MoveOnlyResourceComponent const &) = delete;
	MoveOnlyResourceComponent(MoveOnlyResourceComponent &&) noexcept = default;
	MoveOnlyResourceComponent & operator=(MoveOnlyResourceComponent &&) noexcept = default;
};


struct SecondaryFollowupComponent {
	int InitializedFromOwner = -1;

	void On_Ready(EntityContext const & context)
	{
		InitializedFromOwner = context.FetchId;
		EventLog.push_back("followup_ready:" + std::to_string(context.FetchId));
	}
};


struct PrimaryLifecycleComponent {
	int ReadyCount = 0;
	int EnterCount = 0;
	int ExitCount = 0;
	bool SpawnFollowupOnReady = false;

	void On_Ready(EntityContext const & context)
	{
		ReadyCount++;
		EventLog.push_back("primary_ready:" + std::to_string(context.FetchId));
		if (SpawnFollowupOnReady) {
			context.World->Set_Component<SecondaryFollowupComponent>(context.Entity, {});
		}
	}

	void On_Enter_World(EntityContext const & context)
	{
		EnterCount++;
		EventLog.push_back("primary_enter:" + std::to_string(context.FetchId));
	}

	void On_Exit_World(EntityContext const & context)
	{
		ExitCount++;
		EventLog.push_back("primary_exit:" + std::to_string(context.FetchId));
	}
};


struct OverlayLifecycleComponent {
	void On_Enter_World(EntityContext const & context)
	{
		EventLog.push_back("overlay_enter:" + std::to_string(context.FetchId));
	}

	void On_Exit_World(EntityContext const & context)
	{
		EventLog.push_back("overlay_exit:" + std::to_string(context.FetchId));
	}
};


struct WorldActiveBurnComponent {
	int Ticks = 0;
	int Accumulator = 0;
	EntityId VictimToDestroy = 0;
	bool SpawnFollowupMidTick = false;

	void Process(EntityContext const & context)
	{
		Ticks++;
		Accumulator = static_cast<int>((static_cast<std::uint32_t>(Accumulator) * 33U) ^ (Next_Prng() & 0xFFFFU));
		EventLog.push_back("post_objects:" + std::to_string(context.Kind) + ":" + std::to_string(context.FetchId));
		if (VictimToDestroy != 0) {
			context.World->Destroy_Entity(VictimToDestroy);
			VictimToDestroy = 0;
		}
		if (SpawnFollowupMidTick) {
			context.World->Set_Component<SecondaryFollowupComponent>(context.Entity, {});
			SpawnFollowupMidTick = false;
		}
	}

	void Compute_CRC(EntityContext const &, CRCEngine & crc) const
	{
		crc(Ticks);
		crc(Accumulator);
	}

	void Render_Foreground(EntityContext const & context, void * surface) const
	{
		std::vector<int> * drawn = static_cast<std::vector<int> *>(surface);
		if (drawn != nullptr) {
			drawn->push_back(context.FetchId * 100 + Ticks);
		}
	}
};


struct AlwaysHousePowerComponent {
	static constexpr SimPhase Phase = SimPhase::PostHouses;
	static constexpr TickScope Scope = TickScope::Always;

	int Ticks = 0;

	void Process(EntityContext const & context)
	{
		Ticks++;
		EventLog.push_back("post_houses:" + std::to_string(context.FetchId));
	}
};


struct HomingLinkComponent {
	AbstractClass const * Target = nullptr;
	int TargetSavedId = -1;
	bool RemoveSelfOnDetach = false;
	bool ResolvedAfterLoad = false;

	void Detach(EntityContext const & context, AbstractClass const * target, bool)
	{
		if (Target == target) {
			Target = nullptr;
			if (RemoveSelfOnDetach) {
				context.World->Remove_Component<HomingLinkComponent>(context.Entity);
			}
		}
	}

	void Serialize(SaveStreamClass & stream)
	{
		if (stream.Is_Saving()) {
			int const id = Target != nullptr ? Target->Id : TargetSavedId;
			stream.Write_Pod(id);
		} else {
			stream.Read_Pod(TargetSavedId);
		}
	}

	void Post_Load_Resolve(EntityContext const &)
	{
		ResolvedAfterLoad = true;
	}
};


struct TeardownAuditComponent {
	static inline int RemoveCalls = 0;
	int TagValue = 0;

	void On_Remove(EntityContext const &)
	{
		RemoveCalls += TagValue;
	}
};


struct FragmentMarkerA {
	int Dummy = 1;
};


struct FragmentMarkerB {
	int Dummy = 2;
};


std::uint32_t Run_Peer_Simulation(bool scramble_peer_b, int & out_crc)
{
	FlecsWorldClass peer_world;
	peer_world.Set_Object_Inspector(Inspect_Mock_Object);
	peer_world.Register_Component<WorldActiveBurnComponent>("WorldActiveBurnComponent");
	peer_world.Register_Component<FragmentMarkerA>("FragmentMarkerA");
	peer_world.Register_Component<FragmentMarkerB>("FragmentMarkerB");

	AbstractClass u10{ 2, 10, true, false };
	AbstractClass u20{ 2, 20, true, false };
	AbstractClass b05{ 1, 5, true, false };
	AbstractClass decoy{ 9, 99, true, false };

	EntityId e10 = 0;
	EntityId e20 = 0;
	EntityId eb05 = 0;

	if (!scramble_peer_b) {
		// Shift entity handles with extra allocations and attach in order A.
		for (int i = 0; i < 7; ++i) {
			EntityId const temp = peer_world.Attach_Entity(&decoy, "BulletClass");
			peer_world.Destroy_Entity(temp);
		}
		e20 = peer_world.Attach_Entity(&u20, "UnitClass");
		e10 = peer_world.Attach_Entity(&u10, "UnitClass");
		eb05 = peer_world.Attach_Entity(&b05, "BuildingClass");

		peer_world.Set_Component<WorldActiveBurnComponent>(e20, { 1, 100, 0, false });
		peer_world.Set_Component<WorldActiveBurnComponent>(e10, { 2, 200, 0, false });
		peer_world.Set_Component<WorldActiveBurnComponent>(eb05, { 3, 300, 0, false });

		// Split the three entities across distinct archetype tables.
		peer_world.Set_Component<FragmentMarkerA>(e10, {});
		peer_world.Set_Component<FragmentMarkerB>(eb05, {});
	} else {
		// Opposite creation order, different entity IDs, opposite archetypes.
		eb05 = peer_world.Attach_Entity(&b05, "BuildingClass");
		e10 = peer_world.Attach_Entity(&u10, "UnitClass");
		e20 = peer_world.Attach_Entity(&u20, "UnitClass");

		peer_world.Set_Component<WorldActiveBurnComponent>(eb05, { 3, 300, 0, false });
		peer_world.Set_Component<WorldActiveBurnComponent>(e10, { 2, 200, 0, false });
		peer_world.Set_Component<WorldActiveBurnComponent>(e20, { 1, 100, 0, false });

		peer_world.Set_Component<FragmentMarkerB>(e20, {});
		peer_world.Set_Component<FragmentMarkerA>(eb05, {});
	}

	SharedPrng = 0xCAFEBABEU;
	for (int tick = 0; tick < 5; ++tick) {
		peer_world.Run_Sim_Phase(SimPhase::PostObjects);
	}

	CRCEngine crc;
	peer_world.Fold_CRCs(crc);
	out_crc = crc();
	return(SharedPrng);
}

}	// namespace


int main(void)
{
	FlecsWorld.Set_Object_Inspector(Inspect_Mock_Object);

	// 1. Baseline attach, tags, parent cascade count accuracy, and reset.
	AbstractClass unit_obj{ 2, 1, true, false };
	AbstractClass building_obj{ 1, 2, true, false };
	int locomotor_obj = 0;

	EntityId const unit = FlecsWorld.Attach_Entity(&unit_obj, "UnitClass");
	Check(unit != 0, "attach returns an entity");
	Check(FlecsWorld.Entity_Is_Alive(unit), "attached entity is alive");
	Check(FlecsWorld.Get_Entity_Object(unit) == &unit_obj, "entity object pointer resolves");
	Check(FlecsWorld.Entity_Has_Tag(unit, "UnitClass"), "entity carries its class tag");
	Check(!FlecsWorld.Entity_Has_Tag(unit, "BuildingClass"), "entity does not carry another class tag");
	Check(FlecsWorld.Live_Entity_Count() == 1, "one live entity after one attach");

	EntityId const building = FlecsWorld.Attach_Entity(&building_obj, "BuildingClass");
	Check(building != 0 && building != unit, "second attach is a distinct entity");
	Check(FlecsWorld.Live_Entity_Count() == 2, "two live entities after two attaches");

	EntityId const locomotor = FlecsWorld.Attach_Locomotor_Entity(&locomotor_obj, "DriveLocomotionClass");
	Check(FlecsWorld.Entity_Has_Tag(locomotor, "DriveLocomotionClass"), "locomotor carries its class tag");
	FlecsWorld.Set_Entity_Parent(locomotor, unit);
	Check(FlecsWorld.Get_Entity_Parent(locomotor) == unit, "locomotor parent is recorded");
	Check(FlecsWorld.Live_Entity_Count() == 3, "locomotor counted");

	FlecsWorld.Destroy_Entity(building);
	Check(!FlecsWorld.Entity_Is_Alive(building), "destroyed entity is dead");
	Check(FlecsWorld.Live_Entity_Count() == 2, "destroy decrements the count");
	FlecsWorld.Destroy_Entity(building);
	FlecsWorld.Destroy_Entity(0);
	Check(FlecsWorld.Live_Entity_Count() == 2, "repeated and zero destroys are no-ops");

	// Replacing a parent destroys both the old entity and its child locomotor,
	// leaving only the fresh replacement entity alive.
	EntityId const replaced = FlecsWorld.Replace_Entity(unit, &unit_obj, "UnitClass");
	Check(replaced != 0 && replaced != unit, "replace returns a fresh entity");
	Check(!FlecsWorld.Entity_Is_Alive(unit), "replaced entity is dead");
	Check(!FlecsWorld.Entity_Is_Alive(locomotor), "child locomotor destroyed with replaced parent");
	Check(FlecsWorld.Entity_Has_Tag(replaced, "UnitClass"), "replacement carries the tag");
	Check(FlecsWorld.Live_Entity_Count() == 1, "cascade delete keeps live count exact");

	static char const * const classes[] = {
		"HouseClass", "CellClass", "BulletClass", "WeaponTypeClass",
		"WalkLocomotionClass", "WaypointPathClass",
	};
	for (char const * const classname : classes) {
		AbstractClass temp_obj;
		EntityId const entity = FlecsWorld.Attach_Entity(&temp_obj, classname);
		Check(FlecsWorld.Entity_Has_Tag(entity, classname), "registered class tag attaches");
		FlecsWorld.Destroy_Entity(entity);
	}
	Check(FlecsWorld.Live_Entity_Count() == 1, "tag sweep leaves the count unchanged");

	FlecsWorld.Reset();
	Check(FlecsWorld.Live_Entity_Count() == 0, "reset clears the count");
	Check(!FlecsWorld.Entity_Is_Alive(replaced), "reset kills remaining entities");

	// 2. Non-trivial heap payloads and move-only component storage.
	FlecsWorld.Register_Component<NonTrivialPayloadComponent>("NonTrivialPayloadComponent");
	FlecsWorld.Register_Component<MoveOnlyResourceComponent>("MoveOnlyResourceComponent");
	FlecsWorld.Register_Component<FragmentMarkerA>("FragmentMarkerA");

	std::vector<EntityId> storage_entities;
	for (int i = 0; i < 12; ++i) {
		EntityId const e = FlecsWorld.Attach_Entity(&unit_obj, "UnitClass");
		FlecsWorld.Emplace_Component<NonTrivialPayloadComponent>(e, "payload", std::initializer_list<int>{ i, i * 10, i * 100 });
		FlecsWorld.Emplace_Component<MoveOnlyResourceComponent>(e, 500 + i);
		FlecsWorld.Set_Component<FragmentMarkerA>(e, {});
		storage_entities.push_back(e);
	}
	Check(NonTrivialPayloadComponent::LiveInstances == 12, "exact live C++ instances across table moves");
	MoveOnlyResourceComponent * moved_slot = FlecsWorld.Get_Component<MoveOnlyResourceComponent>(storage_entities[7]);
	Check(moved_slot != nullptr && moved_slot->Handle != nullptr && *moved_slot->Handle == 507,
		"move-only component survives table transitions intact");
	FlecsWorld.Reset();
	Check(NonTrivialPayloadComponent::LiveInstances == 0, "all non-trivial component destructors ran on reset");

	// 3. Deferred scenario On_Ready staging, runtime On_Ready, and enter/exit order.
	FlecsWorld.Register_Component<PrimaryLifecycleComponent>("PrimaryLifecycleComponent");
	FlecsWorld.Register_Component<OverlayLifecycleComponent>("OverlayLifecycleComponent");
	FlecsWorld.Register_Component<SecondaryFollowupComponent>("SecondaryFollowupComponent");

	EventLog.clear();
	AbstractClass scen_late{ 2, 50, true, false };
	AbstractClass scen_early{ 2, 12, true, false };
	AbstractClass scen_aborted{ 2, 30, true, false };

	FlecsWorld.Begin_Scenario_Load();
	Check(FlecsWorld.Is_Scenario_Loading(), "scenario load flag active");
	EntityId const e_late = FlecsWorld.Attach_Entity(&scen_late, "UnitClass");
	EntityId const e_early = FlecsWorld.Attach_Entity(&scen_early, "UnitClass");
	EntityId const e_aborted = FlecsWorld.Attach_Entity(&scen_aborted, "UnitClass");

	FlecsWorld.Set_Component<PrimaryLifecycleComponent>(e_late, { 0, 0, 0, false });
	FlecsWorld.Set_Component<PrimaryLifecycleComponent>(e_early, { 0, 0, 0, true });
	FlecsWorld.Set_Component<PrimaryLifecycleComponent>(e_aborted, { 0, 0, 0, false });
	FlecsWorld.Destroy_Entity(e_aborted);
	Check(EventLog.empty(), "no On_Ready fired before scenario flush");

	FlecsWorld.Flush_Scenario_Ready();
	Check(!FlecsWorld.Is_Scenario_Loading(), "scenario load flag cleared after flush");
	Check(EventLog.size() == 3
		&& EventLog[0] == "primary_ready:12"
		&& EventLog[1] == "primary_ready:50"
		&& EventLog[2] == "followup_ready:12",
		"scenario flush orders On_Ready deterministically and drains chained additions");

	EventLog.clear();
	FlecsWorld.Set_Component<OverlayLifecycleComponent>(e_early, {});
	FlecsWorld.Notify_Enter_World(e_early);
	FlecsWorld.Notify_Exit_World(e_early);
	Check(EventLog.size() == 4
		&& EventLog[0] == "primary_enter:12"
		&& EventLog[1] == "overlay_enter:12"
		&& EventLog[2] == "overlay_exit:12"
		&& EventLog[3] == "primary_exit:12",
		"enter runs in registration order and exit runs in reverse order");
	FlecsWorld.Reset();

	// 4. Simulation phases, limbo scoping, locomotor parent inheritance, and mid-tick structural ops.
	FlecsWorld.Register_Component<WorldActiveBurnComponent>("WorldActiveBurnComponent");
	FlecsWorld.Register_Component<AlwaysHousePowerComponent>("AlwaysHousePowerComponent");

	AbstractClass killer_unit{ 2, 1, true, false };
	AbstractClass doomed_unit{ 2, 2, true, false };
	AbstractClass limbo_unit{ 2, 3, true, true };
	AbstractClass house_obj{ 5, 0, true, true };
	int killer_loco_raw = 0;

	EntityId const e_killer = FlecsWorld.Attach_Entity(&killer_unit, "UnitClass");
	EntityId const e_doomed = FlecsWorld.Attach_Entity(&doomed_unit, "UnitClass");
	EntityId const e_limbo = FlecsWorld.Attach_Entity(&limbo_unit, "UnitClass");
	EntityId const e_house = FlecsWorld.Attach_Entity(&house_obj, "HouseClass");
	EntityId const e_child_loco = FlecsWorld.Attach_Locomotor_Entity(&killer_loco_raw, "DriveLocomotionClass");
	FlecsWorld.Set_Entity_Parent(e_child_loco, e_limbo);

	FlecsWorld.Set_Component<WorldActiveBurnComponent>(e_killer, { 0, 0, e_doomed, true });
	FlecsWorld.Set_Component<WorldActiveBurnComponent>(e_doomed, { 0, 0, 0, false });
	FlecsWorld.Set_Component<WorldActiveBurnComponent>(e_limbo, { 0, 0, 0, false });
	FlecsWorld.Set_Component<WorldActiveBurnComponent>(e_child_loco, { 0, 0, 0, false });
	FlecsWorld.Set_Component<AlwaysHousePowerComponent>(e_house, { 0 });

	EventLog.clear();
	FlecsWorld.Run_Sim_Phase(SimPhase::PostObjects);
	Check(!FlecsWorld.Entity_Is_Alive(e_doomed), "mid-tick victim destroyed on defer flush");
	Check(FlecsWorld.Has_Component<SecondaryFollowupComponent>(e_killer), "mid-tick added component committed");
	Check(EventLog.size() == 2
		&& EventLog[0] == "post_objects:2:1"
		&& EventLog[1] == "followup_ready:1",
		"limbo unit, limbo locomotor child, and mid-tick killed victim skipped cleanly");

	// Unlimbo resumes both the unit and its child locomotor entity.
	limbo_unit.IsInLimbo = false;
	EventLog.clear();
	FlecsWorld.Run_Sim_Phase(SimPhase::PostObjects);
	FlecsWorld.Run_Sim_Phase(SimPhase::PostHouses);
	Check(FlecsWorld.Get_Component<WorldActiveBurnComponent>(e_limbo)->Ticks == 1, "unlimboed unit ticks");
	Check(FlecsWorld.Get_Component<WorldActiveBurnComponent>(e_child_loco)->Ticks == 1, "child locomotor inherits unlimbo");
	Check(FlecsWorld.Get_Component<AlwaysHousePowerComponent>(e_house)->Ticks == 1, "Always scope ticks despite limbo flag");

	// 5. Read-only foreground render pass.
	std::vector<int> rendered_ids;
	limbo_unit.IsInLimbo = true;
	FlecsWorld.Render_Foreground(&rendered_ids);
	Check(rendered_ids.size() == 1 && rendered_ids[0] == 102, "foreground render skips limbo entities and stays read-only");
	FlecsWorld.Reset();

	// 6. Lockstep canonical ordering and real Westwood CRCEngine equivalence.
	int crc_peer_a = 0;
	int crc_peer_b = 0;
	std::uint32_t const prng_a = Run_Peer_Simulation(false, crc_peer_a);
	std::uint32_t const prng_b = Run_Peer_Simulation(true, crc_peer_b);
	Check(prng_a == prng_b, "scrambled peers consume identical PRNG sequence");
	Check(crc_peer_a != 0 && crc_peer_a == crc_peer_b, "scrambled peers fold identical Westwood CRCs");

	// 7. Target detachment and self-removing detach hook.
	FlecsWorld.Register_Component<HomingLinkComponent>("HomingLinkComponent");
	AbstractClass target_dead{ 2, 77, true, false };
	AbstractClass target_alive{ 2, 88, true, false };
	EntityId const missile_keep = FlecsWorld.Attach_Entity(&killer_unit, "BulletClass");
	EntityId const missile_drop = FlecsWorld.Attach_Entity(&doomed_unit, "BulletClass");
	EntityId const missile_other = FlecsWorld.Attach_Entity(&limbo_unit, "BulletClass");

	FlecsWorld.Set_Component<HomingLinkComponent>(missile_keep, { &target_dead, 77, false, false });
	FlecsWorld.Set_Component<HomingLinkComponent>(missile_drop, { &target_dead, 77, true, false });
	FlecsWorld.Set_Component<HomingLinkComponent>(missile_other, { &target_alive, 88, false, false });

	FlecsWorld.Detach_Target(&target_dead, true);
	Check(FlecsWorld.Get_Component<HomingLinkComponent>(missile_keep)->Target == nullptr, "matching target nulled");
	Check(!FlecsWorld.Has_Component<HomingLinkComponent>(missile_drop), "component self-removal during Detach succeeded");
	Check(FlecsWorld.Get_Component<HomingLinkComponent>(missile_other)->Target == &target_alive, "unrelated target preserved");
	FlecsWorld.Reset();

	// 8. Directory-framed save/load round-trip, forward compatibility, corruption check, and Post_Load_Resolve.
	EntityId const src_entity = FlecsWorld.Attach_Entity(&killer_unit, "UnitClass");
	FlecsWorld.Set_Component<PrimaryLifecycleComponent>(src_entity, { 5, 0, 0, false });
	FlecsWorld.Emplace_Component<NonTrivialPayloadComponent>(src_entity, "saved", std::initializer_list<int>{ 7, 14, 21, 28 });
	FlecsWorld.Set_Component<HomingLinkComponent>(src_entity, { &target_alive, 88, false, false });

	std::vector<unsigned char> save_bytes;
	SaveStreamClass writer(save_bytes, SaveStreamClass::MODE_SAVE);
	Check(FlecsWorld.Serialize_Entity(src_entity, writer), "entity component save succeeded");

	// Inject a synthetic unknown component record alongside valid ones to test forward compatibility.
	std::vector<unsigned char> compat_bytes;
	{
		SaveStreamClass custom_writer(compat_bytes, SaveStreamClass::MODE_SAVE);
		std::uint32_t const count = 2;
		custom_writer.Write_Pod(count);

		std::string const unknown_name = "RemovedFutureComponent";
		std::uint32_t const unknown_name_len = static_cast<std::uint32_t>(unknown_name.size());
		std::uint32_t const unknown_payload_len = 8;
		custom_writer.Write_Pod(unknown_name_len);
		custom_writer.Serialize_Bytes(const_cast<char *>(unknown_name.data()), static_cast<int>(unknown_name_len));
		custom_writer.Write_Pod(unknown_payload_len);

		std::string const known_name = "HomingLinkComponent";
		std::uint32_t const known_name_len = static_cast<std::uint32_t>(known_name.size());
		std::uint32_t const known_payload_len = sizeof(int);
		custom_writer.Write_Pod(known_name_len);
		custom_writer.Serialize_Bytes(const_cast<char *>(known_name.data()), static_cast<int>(known_name_len));
		custom_writer.Write_Pod(known_payload_len);

		std::uint64_t const dummy_payload = 0xDEADBEEFCAFEBABEULL;
		int const saved_target_id = 42;
		custom_writer.Write_Pod(dummy_payload);
		custom_writer.Write_Pod(saved_target_id);
	}

	FlecsWorld.Reset();
	EventLog.clear();

	EntityId const dst_entity = FlecsWorld.Attach_Entity(&killer_unit, "UnitClass");
	SaveStreamClass reader(save_bytes, SaveStreamClass::MODE_LOAD);
	Check(FlecsWorld.Serialize_Entity(dst_entity, reader), "entity component load succeeded");
	Check(EventLog.empty(), "load suppressed On_Ready re-execution");

	NonTrivialPayloadComponent const * loaded_vec = FlecsWorld.Try_Get_Component<NonTrivialPayloadComponent>(dst_entity);
	HomingLinkComponent const * loaded_link = FlecsWorld.Try_Get_Component<HomingLinkComponent>(dst_entity);
	Check(loaded_vec != nullptr && loaded_vec->Samples.size() == 4 && loaded_vec->Samples[3] == 28,
		"non-trivial vector payload restored intact");
	Check(loaded_link != nullptr && loaded_link->TargetSavedId == 88 && !loaded_link->ResolvedAfterLoad,
		"link payload restored before Post_Load_Resolve");

	FlecsWorld.Post_Load_Resolve_All();
	Check(FlecsWorld.Get_Component<HomingLinkComponent>(dst_entity)->ResolvedAfterLoad,
		"Post_Load_Resolve ran after load");

	EntityId const compat_entity = FlecsWorld.Attach_Entity(&doomed_unit, "UnitClass");
	SaveStreamClass compat_reader(compat_bytes, SaveStreamClass::MODE_LOAD);
	Check(FlecsWorld.Serialize_Entity(compat_entity, compat_reader), "unknown component skipped cleanly");
	Check(FlecsWorld.Get_Component<HomingLinkComponent>(compat_entity)->TargetSavedId == 42,
		"known component after skipped unknown component loaded accurately");

	// Truncated buffer is rejected cleanly without out-of-bounds reads.
	std::vector<unsigned char> truncated(save_bytes.begin(), save_bytes.begin() + save_bytes.size() - 3);
	EntityId const corrupt_entity = FlecsWorld.Attach_Entity(&limbo_unit, "UnitClass");
	SaveStreamClass corrupt_reader(truncated, SaveStreamClass::MODE_LOAD);
	Check(!FlecsWorld.Serialize_Entity(corrupt_entity, corrupt_reader) && corrupt_reader.Was_Error(),
		"truncated save stream fails cleanly");
	FlecsWorld.Reset();

	// 9. On_Remove across all removal paths plus Shutdown and world auto-recreation.
	FlecsWorld.Register_Component<TeardownAuditComponent>("TeardownAuditComponent");
	TeardownAuditComponent::RemoveCalls = 0;

	EntityId const r1 = FlecsWorld.Attach_Entity(&killer_unit, "UnitClass");
	EntityId const r2 = FlecsWorld.Attach_Entity(&doomed_unit, "UnitClass");
	EntityId const r3_parent = FlecsWorld.Attach_Entity(&limbo_unit, "UnitClass");
	EntityId const r3_child = FlecsWorld.Attach_Locomotor_Entity(&killer_loco_raw, "DriveLocomotionClass");
	EntityId const r4 = FlecsWorld.Attach_Entity(&house_obj, "HouseClass");
	FlecsWorld.Set_Entity_Parent(r3_child, r3_parent);

	FlecsWorld.Set_Component<TeardownAuditComponent>(r1, { 1 });
	FlecsWorld.Set_Component<TeardownAuditComponent>(r2, { 10 });
	FlecsWorld.Set_Component<TeardownAuditComponent>(r3_child, { 100 });
	FlecsWorld.Set_Component<TeardownAuditComponent>(r4, { 1000 });

	FlecsWorld.Remove_Component<TeardownAuditComponent>(r1);
	Check(TeardownAuditComponent::RemoveCalls == 1, "explicit Remove_Component runs On_Remove");
	FlecsWorld.Destroy_Entity(r2);
	Check(TeardownAuditComponent::RemoveCalls == 11, "Destroy_Entity runs On_Remove");
	FlecsWorld.Destroy_Entity(r3_parent);
	Check(TeardownAuditComponent::RemoveCalls == 111, "parent cascade delete runs child On_Remove");
	FlecsWorld.Reset();
	Check(TeardownAuditComponent::RemoveCalls == 1111, "Reset runs On_Remove on remaining entities");

	// Shutdown tears down the flecs world and re-attaching re-binds components automatically.
	FlecsWorld.Shutdown();
	EntityId const post_shutdown = FlecsWorld.Attach_Entity(&unit_obj, "UnitClass");
	Check(FlecsWorld.Entity_Is_Alive(post_shutdown), "attach recreates world after shutdown");
	Check(FlecsWorld.Is_Component_Registered("TeardownAuditComponent"), "component registrations persist across world recreation");
	FlecsWorld.Set_Component<TeardownAuditComponent>(post_shutdown, { 7000 });
	FlecsWorld.Shutdown();
	Check(TeardownAuditComponent::RemoveCalls == 8111, "shutdown runs On_Remove and cleans up without leaks");

	if (Failures == 0) {
		std::printf("flecs: all checks passed\n");
		return(0);
	}
	std::printf("flecs: %d check(s) failed\n", Failures);
	return(1);
}
