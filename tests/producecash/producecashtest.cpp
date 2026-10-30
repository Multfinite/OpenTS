/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Standalone C++20 contract test for EC::ProduceCashComponent registered in the
// production EC::GameComponents list over real flecs storage with 1:1 flecs::entity
// root lifetime, real FrameTimerClass countdown timers, binary EC01 save/load streams,
// and CRCEngine.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

#include "crc.h"
#include "ec/entity.h"
#include "ftimer.h"
#include "mission.hh"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include "timer.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

int Frame = 0;

// ---------------------------------------------------------------------------
// Minimal Engine Harness Matching OpenTS Building / House / SaveStream Surface
// ---------------------------------------------------------------------------

class AbstractClass
{
	public:
		virtual ~AbstractClass(void) = default;
		int ID = 1;
		int Fetch_ID(void) const noexcept { return ID; }
};

class HouseTypeClass
{
	public:
		bool IsMultiplayPassive = false;
};

class HouseClass : public AbstractClass
{
	public:
		explicit HouseClass(HouseTypeClass * type, int credits = 0)
			: Class(type), Credits(credits)
		{
		}

		HouseTypeClass * Class = nullptr;
		int Credits = 0;
		int TiberiumCredits = 0;
		int TotalSpent = 0;
		double PowerFraction = 1.0;

		double Power_Fraction(void) const noexcept { return PowerFraction; }

		void Refund_Money(int addition)
		{
			if (addition > 0) {
				Credits += addition;
			}
		}

		void Spend_Money(int remove)
		{
			if (remove <= 0) {
				return;
			}
			TotalSpent += remove;
			if (remove > Credits) {
				int const from_tib = std::min(TiberiumCredits, remove - Credits);
				TiberiumCredits -= from_tib;
				Credits = 0;
			} else {
				Credits -= remove;
			}
		}
};

class BuildingTypeClass
{
	public:
		bool IsPowered = false;
		int ProduceCashStartup = 0;
		bool IsProduceCashStartupOneTime = false;
		int ProduceCashAmount = 0;
		int ProduceCashDelay = 0;
		int ProduceCashBudget = 0;
		bool IsProduceCashResetOnCapture = false;

		bool Has_Produce_Cash(void) const noexcept
		{
			return (ProduceCashDelay > 0 && ProduceCashAmount != 0 && ProduceCashAmount != std::numeric_limits<int>::min())
				|| ProduceCashStartup > 0;
		}
};

class ObjectClass : public AbstractClass
{
	public:
		explicit ObjectClass(int id = 1)
			: Entity(EC::Create_Entity(this))
		{
			ID = id;
			Entity.add(flecs::Disabled); // Objects start in Limbo.
		}

		~ObjectClass(void) override
		{
			EC::Destroy_Entity(Entity);
		}

		bool IsActive = true;
		bool IsInLimbo = true;
		int Strength = 200;
		flecs::entity Entity;

		void Unlimbo(void)
		{
			IsInLimbo = false;
			Entity.remove(flecs::Disabled);
		}

		void Limbo(void)
		{
			IsInLimbo = true;
			Entity.add(flecs::Disabled);
		}
};

class TechnoClass : public ObjectClass
{
	public:
		TechnoClass(HouseClass * house, int id)
			: ObjectClass(id), House(house)
		{
		}

		HouseClass * House = nullptr;
		MissionType Mission = MISSION_GUARD;
		MissionType MissionQueue = MISSION_NONE;
};

class BuildingClass : public TechnoClass
{
	public:
		BuildingClass(BuildingTypeClass * type, HouseClass * house, int id = 1)
			: TechnoClass(house, id), Class(type)
		{
		}

		BuildingTypeClass * Class = nullptr;
		bool HasOpened = false;
		bool IsPoweredOn = true;
		bool IsStunned = false;

		bool Is_Powered_On(void) const noexcept
		{
			return IsPoweredOn && !IsStunned;
		}
};

class CellClass : public AbstractClass
{
	public:
		CellClass(void) : Entity(EC::Create_Entity(this)) {}
		~CellClass(void) override { EC::Destroy_Entity(Entity); }
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
		LocomotionClass(void) : Entity(EC::Create_Entity(this)) {}
		~LocomotionClass(void) override { EC::Destroy_Entity(Entity); }
		flecs::entity Get_Entity(void) const noexcept override { return Entity; }
		flecs::entity Entity;
};

// Include production EC::GameComponents (with ProduceCashComponent!) and registry.h.
#include "ec/registry.h"

namespace {

int Failures = 0;
int Checks = 0;

void Check(bool condition, char const * what)
{
	++Checks;
	std::printf("%-68s %s\n", what, condition ? "ok" : "FAILED");
	if (!condition) {
		++Failures;
	}
}

class MemorySaveStream
{
	public:
		struct BoundScope
		{
			MemorySaveStream & Stream;
			unsigned int PreviousBound;

			BoundScope(MemorySaveStream & s, unsigned int next_bound)
				: Stream(s), PreviousBound(s.Bound)
			{
				s.Bound = next_bound;
			}

			~BoundScope(void)
			{
				Stream.Bound = PreviousBound;
			}
		};

		explicit MemorySaveStream(bool writing)
			: Writing(writing)
		{
		}

		bool Is_Writing(void) const noexcept { return Writing; }
		bool Was_Error(void) const noexcept { return Error; }
		void Fail(void) noexcept { Error = true; }

		unsigned int Offset(void) const noexcept
		{
			return Writing ? static_cast<unsigned int>(Buffer.size()) : ReadPos;
		}

		unsigned int Size(void) const noexcept
		{
			return static_cast<unsigned int>(Buffer.size());
		}

		unsigned int Effective_Limit(void) const noexcept
		{
			unsigned int const total = static_cast<unsigned int>(Buffer.size());
			return Bound < total ? Bound : total;
		}

		bool Fits(int count, std::size_t elem_size) noexcept
		{
			if (count < 0) {
				Error = true;
				return false;
			}
			if (Writing) {
				return !Error;
			}
			std::size_t const rem = Effective_Limit() - ReadPos;
			if (elem_size != 0 && static_cast<std::size_t>(count) > rem / elem_size) {
				Error = true;
				return false;
			}
			return !Error;
		}

		void Overwrite_Bytes(unsigned int offset, void const * src, int bytes)
		{
			if (!Writing || bytes < 0 || offset + static_cast<std::size_t>(bytes) > Buffer.size()) {
				Error = true;
				return;
			}
			std::memcpy(Buffer.data() + offset, src, static_cast<std::size_t>(bytes));
		}

		void Serialize_Bytes(void * data, int bytes)
		{
			if (bytes < 0 || Error) {
				Error = true;
				return;
			}
			if (bytes == 0) {
				return;
			}
			if (Writing) {
				auto const * p = static_cast<unsigned char const *>(data);
				Buffer.insert(Buffer.end(), p, p + bytes);
			} else {
				if (!Fits(bytes, 1)) {
					return;
				}
				std::memcpy(data, Buffer.data() + ReadPos, static_cast<std::size_t>(bytes));
				ReadPos += static_cast<unsigned int>(bytes);
			}
		}

		template<typename T>
		void Serialize(T & value)
		{
			if constexpr (requires(T & v, MemorySaveStream & s) { v.Serialize(s); }) {
				value.Serialize(*this);
			} else {
				static_assert(std::is_trivially_copyable_v<T>);
				Serialize_Bytes(&value, static_cast<int>(sizeof(T)));
			}
		}

		std::vector<unsigned char> Buffer;
		unsigned int ReadPos = 0;
		unsigned int Bound = std::numeric_limits<unsigned int>::max();
		bool Writing = true;
		bool Error = false;
};

void Attach_If_Needed(BuildingClass & b)
{
	if (b.Class != nullptr && b.Class->Has_Produce_Cash()) {
		b.Entity.add<EC::ProduceCashComponent>();
	}
}

void Do_Grand_Opening(BuildingClass & b, bool captured = false)
{
	if (!b.HasOpened || captured) {
		Attach_If_Needed(b);
		EC::GameComponents::Dispatch_Grand_Opening(b, captured);
		b.HasOpened = true;
	}
}

void Do_Capture(BuildingClass & b, HouseClass * newowner)
{
	if (b.House == newowner) {
		return;
	}
	HouseClass * oldowner = b.House;
	b.House = newowner;
	Do_Grand_Opening(b, true);
	EC::GameComponents::Dispatch_Captured(b, oldowner, newowner);
}

void Advance_Frames(int count)
{
	for (int i = 0; i < count; ++i) {
		++Frame;
		EC::GameComponents::PostAI_All(EC::World());
	}
}

void Register_Game_Components_Hook(flecs::world & world)
{
	EC::GameComponents::Register_All(world);
}

} // namespace

int main(void)
{
	EC::Set_World_Init_Hook(&Register_Game_Components_Hook);

	// 1. Compile-time registration & root affinity contracts
	static_assert(EC::GameComponents::count == 1);
	static_assert(EC::IsRegisteredComponent<EC::ProduceCashComponent>);
	static_assert(EC::AllowedOnRoot<EC::ProduceCashComponent, ObjectClass>);
	static_assert(!EC::AllowedOnRoot<EC::ProduceCashComponent, CellClass>);
	static_assert(!EC::AllowedOnRoot<EC::ProduceCashComponent, LocomotionClass>);

	// 2. Non-cash buildings own a 1:1 flecs::entity without attaching ProduceCashComponent
	{
		Frame = 100;
		HouseTypeClass active_country{false};
		HouseClass player(&active_country, 0);
		BuildingTypeClass power_plant{};
		flecs::entity_t tracked_id = 0;
		{
			BuildingClass b1(&power_plant, &player, 1);
			tracked_id = b1.Entity.id();
			Attach_If_Needed(b1);
			b1.Unlimbo();
			Do_Grand_Opening(b1);
			Advance_Frames(20);

			Check(b1.Entity.is_alive() && !b1.Entity.has<EC::ProduceCashComponent>() && !EC::Has_User_Components(b1.Entity),
				"non-cash BuildingClass owns 1:1 entity without ProduceCashComponent");
		}
		Check(!EC::World().entity(tracked_id).is_alive(),
			"~BuildingClass() destroys 1:1 flecs::entity on scope exit");
	}

	// 3. Exact ProduceCashDelay interval, unopened shroud suppression, Limbo suspension,
	//    and independent per-structure phase
	{
		Frame = 100;
		HouseTypeClass active_country{false};
		HouseClass player(&active_country, 0);
		BuildingTypeClass derrick{};
		derrick.ProduceCashAmount = 100;
		derrick.ProduceCashDelay = 10;

		BuildingClass b1(&derrick, &player, 10);
		BuildingClass b2(&derrick, &player, 11);
		Attach_If_Needed(b1);
		Attach_If_Needed(b2);

		// While in Limbo or unopened, pays nothing.
		Advance_Frames(25);
		b1.Unlimbo();
		Advance_Frames(25);
		Check(player.Credits == 0, "unopened or limbo structure pays nothing");

		// Open b1 at Frame, then open b2 3 frames later.
		Do_Grand_Opening(b1);
		Advance_Frames(3);
		b2.Unlimbo();
		Do_Grand_Opening(b2);

		Advance_Frames(6); // b1 at +9 frames, b2 at +6 frames
		Check(player.Credits == 0, "no payment before full ProduceCashDelay elapses");

		Advance_Frames(1); // b1 at +10 frames -> pays 100!
		Check(player.Credits == 100, "first structure pays on exact ProduceCashDelay frame");

		Advance_Frames(2); // b2 at +9 frames
		Check(player.Credits == 100, "second structure keeps independent timer offset");

		Advance_Frames(1); // b2 at +10 frames -> pays 100!
		Check(player.Credits == 200, "second structure pays 3 frames after first");

		// Limbo suspends b2 via flecs::Disabled.
		b2.Limbo();
		Advance_Frames(10);
		Check(player.Credits == 300, "limbo structure is skipped by flecs Disabled filter");
	}

	// 4. Selling (MISSION_DECONSTRUCTION), Strength == 0 dying state, and MultiplayPassive owner
	{
		Frame = 100;
		HouseTypeClass active_country{false};
		HouseTypeClass neutral_country{true};
		HouseClass player(&active_country, 0);
		HouseClass neutral(&neutral_country, 0);

		BuildingTypeClass derrick{};
		derrick.ProduceCashAmount = 50;
		derrick.ProduceCashDelay = 5;

		BuildingClass b_player(&derrick, &player, 20);
		BuildingClass b_neutral(&derrick, &neutral, 21);
		Attach_If_Needed(b_player);
		Attach_If_Needed(b_neutral);
		b_player.Unlimbo();
		b_neutral.Unlimbo();
		Do_Grand_Opening(b_player);
		Do_Grand_Opening(b_neutral);

		b_player.MissionQueue = MISSION_DECONSTRUCTION;
		Advance_Frames(10);
		Check(player.Credits == 0 && neutral.Credits == 0,
			"selling queue and MultiplayPassive owner both suppress payouts");

		b_player.MissionQueue = MISSION_NONE;
		b_player.Strength = 0; // Dying explosion countdown before Limbo
		Advance_Frames(10);
		Check(player.Credits == 0, "dying building with Strength == 0 pays nothing");
	}

	// 5. Finite ProduceCashBudget & clamped final installment
	{
		Frame = 100;
		HouseTypeClass active_country{false};
		HouseClass player(&active_country, 0);

		BuildingTypeClass well{};
		well.ProduceCashAmount = 100;
		well.ProduceCashDelay = 5;
		well.ProduceCashBudget = 250;

		BuildingClass b(&well, &player, 30);
		Attach_If_Needed(b);
		b.Unlimbo();
		Do_Grand_Opening(b);

		Advance_Frames(5);
		int const after_1 = player.Credits;
		Advance_Frames(5);
		int const after_2 = player.Credits;
		Advance_Frames(5);
		int const after_3 = player.Credits;
		Advance_Frames(20);

		Check(after_1 == 100 && after_2 == 200 && after_3 == 250 && player.Credits == 250 &&
			b.Entity.get<EC::ProduceCashComponent>().Remaining == 0,
			"ProduceCashBudget=250 with Amount=100 pays 100, 100, 50 and stops");
	}

	// 6. Negative ProduceCashAmount (upkeep drain), broke house budget accounting, and INT_MIN guard
	{
		Frame = 100;
		HouseTypeClass active_country{false};
		HouseClass player(&active_country, 80);
		player.TiberiumCredits = 20; // Total liquid = 100

		BuildingTypeClass upkeep{};
		upkeep.ProduceCashAmount = -60;
		upkeep.ProduceCashDelay = 4;
		upkeep.ProduceCashBudget = 150;

		BuildingClass b(&upkeep, &player, 40);
		Attach_If_Needed(b);
		b.Unlimbo();
		Do_Grand_Opening(b);

		Advance_Frames(4); // Drains 60 (Credits: 20, Tib: 20, Remaining: 90)
		bool const step1_ok = (player.Credits == 20 && player.TiberiumCredits == 20 &&
			b.Entity.get<EC::ProduceCashComponent>().Remaining == 90);

		Advance_Frames(4); // Drains 60 -> exhausts 20 Credits + 20 Tib (0 left, Remaining: 30)
		bool const step2_ok = (player.Credits == 0 && player.TiberiumCredits == 0 &&
			b.Entity.get<EC::ProduceCashComponent>().Remaining == 30);

		Advance_Frames(4); // Clamped to -30, house has 0, Remaining still reaches 0!
		bool const step3_ok = (b.Entity.get<EC::ProduceCashComponent>().Remaining == 0 &&
			player.TotalSpent == 150);

		upkeep.ProduceCashAmount = std::numeric_limits<int>::min();
		upkeep.ProduceCashBudget = 0;
		BuildingClass b_min(&upkeep, &player, 41);
		Attach_If_Needed(b_min);
		b_min.Unlimbo();
		Do_Grand_Opening(b_min);
		Advance_Frames(10);

		Check(step1_ok && step2_ok && step3_ok && !b_min.Entity.has<EC::ProduceCashComponent>(),
			"negative upkeep drains credits/tiberium, clamps budget, and ignores INT_MIN");
	}

	// 7. Credit overflow refuses payment without deducting budget
	{
		Frame = 100;
		HouseTypeClass active_country{false};
		HouseClass rich(&active_country, std::numeric_limits<int>::max() - 50);

		BuildingTypeClass mint{};
		mint.ProduceCashAmount = 100;
		mint.ProduceCashDelay = 5;
		mint.ProduceCashBudget = 200;

		BuildingClass b(&mint, &rich, 50);
		Attach_If_Needed(b);
		b.Unlimbo();
		Do_Grand_Opening(b);

		Advance_Frames(5); // Overflow refused; Remaining must stay 200!
		bool const refused_intact = (rich.Credits == std::numeric_limits<int>::max() - 50 &&
			b.Entity.get<EC::ProduceCashComponent>().Remaining == 200);

		rich.Credits = 1000;
		Advance_Frames(5); // Next interval succeeds!
		Check(refused_intact && rich.Credits == 1100 &&
			b.Entity.get<EC::ProduceCashComponent>().Remaining == 100,
			"credit overflow refuses payout without charging budget");
	}

	// 8. Powered=yes pause & resume with exact remaining frames
	{
		Frame = 100;
		HouseTypeClass active_country{false};
		HouseClass player(&active_country, 0);

		BuildingTypeClass powered_type{};
		powered_type.IsPowered = true;
		powered_type.ProduceCashAmount = 100;
		powered_type.ProduceCashDelay = 10;

		BuildingTypeClass unpowered_type{};
		unpowered_type.IsPowered = false;
		unpowered_type.ProduceCashAmount = 10;
		unpowered_type.ProduceCashDelay = 10;

		BuildingClass b_pow(&powered_type, &player, 60);
		BuildingClass b_unpow(&unpowered_type, &player, 61);
		Attach_If_Needed(b_pow);
		Attach_If_Needed(b_unpow);
		b_pow.Unlimbo();
		b_unpow.Unlimbo();
		Do_Grand_Opening(b_pow);
		Do_Grand_Opening(b_unpow);

		Advance_Frames(4); // 6 frames remaining on b_pow
		player.PowerFraction = 0.5; // Low power!
		Advance_Frames(20); // Unpowered building pays twice (20 credits); powered stays paused!
		bool const paused_ok = (player.Credits == 20);

		player.PowerFraction = 1.0; // Power restored -> 6 powered frames remaining on b_pow!
		Advance_Frames(5);          // Frame 125..129 (b_unpow still at 20 credits, b_pow at 1 left)
		bool const five_after_ok = (player.Credits == 20);
		Advance_Frames(1);          // Frame 130: b_unpow pays 3rd 10 (=30) AND b_pow pays 100 (=130)!
		bool const six_after_ok = (player.Credits == 130);

		// EMP stun also pauses Powered=yes structure (3 powered frames before stun + 7 after = 10).
		Advance_Frames(3);
		b_pow.IsStunned = true;
		Advance_Frames(15);
		b_pow.IsStunned = false;
		Advance_Frames(6);
		int const before_stun_payout = player.Credits;
		Advance_Frames(1);
		bool const stun_resume_ok = (player.Credits >= before_stun_payout + 100);

		Check(paused_ok && five_after_ok && six_after_ok && stun_resume_ok,
			"Powered=yes pauses on low power or EMP and resumes with exact frames left");
	}

	// 9. Capture: timer restart, ProduceCashResetOnCapture, Startup bonus, and StartupOneTime
	{
		Frame = 100;
		HouseTypeClass neutral_country{true};
		HouseTypeClass active_country{false};
		HouseClass neutral(&neutral_country, 0);
		HouseClass gdi(&active_country, 0);
		HouseClass nod(&active_country, 0);

		BuildingTypeClass derrick{};
		derrick.ProduceCashStartup = 1000;
		derrick.IsProduceCashStartupOneTime = true;
		derrick.ProduceCashAmount = 100;
		derrick.ProduceCashDelay = 10;
		derrick.ProduceCashBudget = 100;
		derrick.IsProduceCashResetOnCapture = false;

		BuildingClass b(&derrick, &neutral, 70);
		Attach_If_Needed(b);
		b.Unlimbo();
		Do_Grand_Opening(b);

		// Neutral -> GDI capture: pays 1000 startup bonus; budget stays 100 (startup doesn't count against budget)
		Do_Capture(b, &gdi);
		bool const first_capture_ok = (gdi.Credits == 1000 &&
			b.Entity.get<EC::ProduceCashComponent>().Remaining == 100);

		Advance_Frames(10); // GDI earns 100 recurring -> budget spent (Remaining == 0)
		bool const spent_ok = (gdi.Credits == 1100 &&
			b.Entity.get<EC::ProduceCashComponent>().Remaining == 0);

		// GDI -> Nod capture (player-to-player, ResetOnCapture=false): no startup, budget stays 0
		Do_Capture(b, &nod);
		Advance_Frames(15);
		bool const nod_no_reset_ok = (nod.Credits == 0);

		// Nod -> Neutral -> Nod capture with StartupOneTime=true: still no second startup bonus!
		Do_Capture(b, &neutral);
		Do_Capture(b, &nod);
		bool const one_time_enforced = (nod.Credits == 0);

		// Enable ResetOnCapture=yes & StartupOneTime=no: player-to-player capture refills budget,
		// and capture from neutral pays startup again!
		derrick.IsProduceCashResetOnCapture = true;
		derrick.IsProduceCashStartupOneTime = false;
		Do_Capture(b, &gdi); // From Nod (non-neutral): no startup, but budget refills to 100!
		Advance_Frames(10);
		bool const refill_ok = (gdi.Credits == 1200 &&
			b.Entity.get<EC::ProduceCashComponent>().Remaining == 0);

		Do_Capture(b, &neutral);
		Do_Capture(b, &nod); // From Neutral with OneTime=false: pays 1000 startup + refills 100!
		Advance_Frames(10);
		bool const repeatable_ok = (nod.Credits == 1100);

		Check(first_capture_ok && spent_ok && nod_no_reset_ok && one_time_enforced && refill_ok && repeatable_ok,
			"capture handles startup bonus, one-time flag, neutral check, and budget refill");
	}

	// 10. EC01 two-subpass Save/Load round-trip & 4-level deterministic CRC
	{
		Frame = 100;
		HouseTypeClass neutral_country{true};
		HouseTypeClass active_country{false};
		HouseClass neutral(&neutral_country, 0);
		HouseClass player(&active_country, 0);

		BuildingTypeClass derrick{};
		derrick.IsPowered = true;
		derrick.ProduceCashStartup = 500;
		derrick.IsProduceCashStartupOneTime = true;
		derrick.ProduceCashAmount = 100;
		derrick.ProduceCashDelay = 10;
		derrick.ProduceCashBudget = 250;

		auto owner_id_fn = [](EC::RootKind, void const * ptr) -> std::uint32_t {
			return static_cast<std::uint32_t>(static_cast<ObjectClass const *>(ptr)->Fetch_ID());
		};
		auto owner_key_fn = [](EC::RootKind kind, void const * ptr) -> EC::StableOwnerKey {
			return EC::StableOwnerKey{
				static_cast<std::uint8_t>(kind),
				static_cast<ObjectClass const *>(ptr)->Fetch_ID(),
				0};
		};

		MemorySaveStream writer(true);
		int saved_ticks_left = 0;
		int single_crc_before = 0;
		int obj_crc_val_before = 0;
		int world_crc_val_before = 0;
		bool save_ok = false;

		{
			auto b = std::make_unique<BuildingClass>(&derrick, &neutral, 88);
			Attach_If_Needed(*b);
			b->Unlimbo();
			Do_Grand_Opening(*b);
			Do_Capture(*b, &player); // Startup 500 paid, IsStartupPaid = true, Timer = 10, Remaining = 250

			Advance_Frames(4); // 6 frames left on Timer
			player.PowerFraction = 0.25;
			Advance_Frames(1); // Pauses Timer with 5 frames left (Frame advanced by 1 before Stop)
			saved_ticks_left = b->Entity.get<EC::ProduceCashComponent>().Timer.Value();

			CRCEngine obj_crc_before;
			EC::GameComponents::Compute_Entity_CRC(b->Entity, obj_crc_before);
			obj_crc_val_before = obj_crc_before();
			single_crc_before = EC::Single_Component_CRC(b->Entity.get<EC::ProduceCashComponent>());

			CRCEngine world_crc_before;
			EC::GameComponents::Compute_CRC_All(EC::World(), world_crc_before, owner_key_fn);
			world_crc_val_before = world_crc_before();

			save_ok = EC::GameComponents::Save_Trailer(EC::World(), writer, owner_id_fn);
			// ~BuildingClass() runs here -> destroys saved building's flecs::entity 1:1 (as Clear_Scenario does!)
		}

		// Reconstruct loaded BuildingClass with its own 1:1 flecs::entity (as ClassFactory does on Load_Game).
		BuildingClass loaded_b(&derrick, &player, 88);
		loaded_b.HasOpened = true;
		loaded_b.Unlimbo();

		MemorySaveStream reader(false);
		reader.Buffer = writer.Buffer;
		auto resolve_owner = [&](EC::RootKind kind, std::uint32_t id) -> flecs::entity {
			if (kind == EC::RootKind::Object && id == 88u) {
				return loaded_b.Entity;
			}
			return flecs::entity::null();
		};
		bool const load_ok = EC::GameComponents::Load_Trailer(EC::World(), reader, resolve_owner);

		CRCEngine obj_crc_after;
		EC::GameComponents::Compute_Entity_CRC(loaded_b.Entity, obj_crc_after);
		int const single_crc_after = EC::Single_Component_CRC(loaded_b.Entity.get<EC::ProduceCashComponent>());

		CRCEngine world_crc_after;
		EC::GameComponents::Compute_CRC_All(EC::World(), world_crc_after, owner_key_fn);

		auto const & restored = loaded_b.Entity.get<EC::ProduceCashComponent>();
		bool const state_restored =
			save_ok && load_ok &&
			restored.Timer.Value() == saved_ticks_left &&
			restored.Remaining == 250 &&
			restored.IsStartupPaid &&
			single_crc_before == single_crc_after &&
			obj_crc_val_before == obj_crc_after() &&
			world_crc_val_before == world_crc_after();

		// Resume power and verify restored countdown fires on exact remaining frame
		// (1 frame for Start() to anchor Started = Frame + saved_ticks_left frames = 6 frames)
		// AND IsStartupPaid stays enforced across save/load!
		player.PowerFraction = 1.0;
		Advance_Frames(saved_ticks_left);
		bool const pre_fire_ok = (player.Credits == 500);
		Advance_Frames(1);
		bool const post_fire_ok = (player.Credits == 600 &&
			loaded_b.Entity.get<EC::ProduceCashComponent>().Remaining == 150);

		Do_Capture(loaded_b, &neutral);
		Do_Capture(loaded_b, &player);
		bool const startup_still_locked = (player.Credits == 600);

		Check(state_restored && pre_fire_ok && post_fire_ok && startup_still_locked,
			"EC01 save/load and 4-level CRC preserve paused timer, budget, and one-time state");
	}

	EC::World_Shutdown();
	std::printf("\n%d checks run, %d failed\n", Checks, Failures);
	return Failures == 0 ? 0 : 1;
}
