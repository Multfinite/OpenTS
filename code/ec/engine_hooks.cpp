/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "building.h"
#include "builtype.h"
#include "cell.h"
#include "crc.h"
#include "foot.h"
#include "house.h"
#include "houstype.h"
#include "ipiggy.h"
#include "loco.h"
#include "object.h"
#include "queue.h"
#include "savestream.h"
#include "swizzle.h"
#include "techno.h"

#include "ec/engine_hooks.h"
#include "ec/registry.h"

namespace EC::Hooks {

namespace {

void Register_Game_Components(flecs::world & world)
{
	GameComponents::Register_All(world);
}

struct AutoWorldRegistrationHook
{
	AutoWorldRegistrationHook(void) noexcept
	{
		Set_World_Init_Hook(&Register_Game_Components);
	}
} const GAutoWorldRegistrationHook;

StableOwnerKey Build_Stable_Owner_Key(RootKind kind, void const * ptr)
{
	if (ptr == nullptr) {
		return StableOwnerKey{static_cast<std::uint8_t>(kind), -1, 0};
	}

	switch (kind) {
		case RootKind::Object: {
			ObjectClass const * obj = static_cast<ObjectClass const *>(ptr);
			return StableOwnerKey{0, obj->Fetch_ID(), 0};
		}
		case RootKind::Cell: {
			CellClass const * cell = static_cast<CellClass const *>(ptr);
			int const packed_cell =
				(static_cast<int>(cell->CellID.X) << 16) |
				(static_cast<int>(cell->CellID.Y) & 0xFFFF);
			return StableOwnerKey{1, packed_cell, 0};
		}
		case RootKind::Locomotor: {
			LocomotionClass const * loco = static_cast<LocomotionClass const *>(ptr);
			FootClass * foot = loco->Linked_Object();
			if (foot == nullptr) {
				return StableOwnerKey{2, -1, 0};
			}
			int subslot = 0;
			for (ILocomotion * cur = foot->Locomotion.get(); cur != nullptr; ++subslot) {
				if (cur->Get_Entity().id() == loco->Entity.id()) {
					break;
				}
				IPiggyback * piggy = Piggyback_Of(cur);
				cur = piggy != nullptr ? piggy->Carried_Locomotor() : nullptr;
			}
			return StableOwnerKey{2, foot->Fetch_ID(), subslot};
		}
	}
	return StableOwnerKey{static_cast<std::uint8_t>(kind), -1, 0};
}

LocomotionClass * As_Locomotion_Root(ILocomotion & loco) noexcept
{
	flecs::entity fe = loco.Get_Entity();
	if (fe.id() != 0 && fe.is_alive()) {
		if (auto const * link = fe.try_get<OwnerLink<LocomotionClass>>(); link != nullptr) {
			return link->ptr;
		}
	}
	return nullptr;
}

} // namespace

// ---------------------------------------------------------------------------
// Global Hooks (G01..G12)
// ---------------------------------------------------------------------------

void AI_All(void)
{
	if constexpr (GameComponents::count > 0) {
		GameComponents::AI_All(World());
	}
}

void PostAI_All(void)
{
	if constexpr (GameComponents::count > 0) {
		GameComponents::PostAI_All(World());
	}
}

void Environment_AI_All(void)
{
	if constexpr (GameComponents::count > 0) {
		GameComponents::Environment_AI_All(World());
	}
}

void Frame_Begin_All(void)
{
	if constexpr (GameComponents::count > 0) {
		GameComponents::Frame_Begin_All(World());
	}
}

void Frame_End_All(void)
{
	if constexpr (GameComponents::count > 0) {
		GameComponents::Frame_End_All(World());
	}
}

void Pre_Render_All(void)
{
	if constexpr (GameComponents::count > 0) {
		GameComponents::Pre_Render_All(World());
	}
}

void Post_Render_All(void)
{
	if constexpr (GameComponents::count > 0) {
		GameComponents::Post_Render_All(World());
	}
}

void Scenario_Init_All(void)
{
	if constexpr (GameComponents::count > 0) {
		GameComponents::Scenario_Init_All(World());
	}
}

void Scenario_Clear_All(void)
{
	GameComponents::Scenario_Clear_All();
}

bool Save_Trailer(SaveStreamClass & stream)
{
	auto id_of = [](RootKind kind, void const * ptr) -> std::uint32_t {
		void const * swizzle_key = ptr;
		if (kind == RootKind::Locomotor && ptr != nullptr) {
			swizzle_key = static_cast<IPersistent const *>(static_cast<LocomotionClass const *>(ptr));
		}
		return static_cast<std::uint32_t>(Swizzler.Registered_ID_Of(swizzle_key));
	};
	return GameComponents::Save_Trailer(World(), stream, id_of);
}

bool Load_Trailer(SaveStreamClass & stream)
{
	Swizzler.Prepare_Address_Lookup();
	auto resolve_owner_entity = [](RootKind kind, std::uint32_t owner_id) -> flecs::entity {
		void * raw = Swizzler.Address_Of(static_cast<SwizzleIDType>(owner_id));
		if (raw == nullptr) {
			return flecs::entity::null();
		}
		switch (kind) {
			case RootKind::Object:
				return static_cast<ObjectClass *>(raw)->Entity;
			case RootKind::Cell:
				return static_cast<CellClass *>(raw)->Entity;
			case RootKind::Locomotor:
				return static_cast<LocomotionClass *>(static_cast<IPersistent *>(raw))->Entity;
		}
		return flecs::entity::null();
	};
	return GameComponents::Load_Trailer(World(), stream, resolve_owner_entity);
}

void Post_Load_All(void)
{
	if constexpr (GameComponents::count > 0) {
		GameComponents::Post_Load_All(World());
	}
}

void Compute_Cell_CRCs(CRCEngine & crc)
{
	if constexpr (GameComponents::count > 0) {
		GameComponents::Compute_Root_CRC_All<CellClass>(World(), crc, Build_Stable_Owner_Key);
	}
}

void Print_Component_CRCs(FILE * fp)
{
	if constexpr (GameComponents::count > 0) {
		GameComponents::Print_Component_CRCs(World(), fp, Build_Stable_Owner_Key);
	}
}

void Accumulate_Game_CRC(unsigned int & game_crc)
{
	if constexpr (GameComponents::count > 0) {
		CRCEngine crc;
		GameComponents::Compute_CRC_All(World(), crc, Build_Stable_Owner_Key);
		Add_CRC(&game_crc, static_cast<unsigned int>(crc()));
	}
}

void Shutdown_World(void)
{
	World_Shutdown();
}

// ---------------------------------------------------------------------------
// Out-of-line Entity Dispatchers (E02..E27)
// ---------------------------------------------------------------------------

namespace Detail {

void Dispatch_Object_Detach(ObjectClass & self, AbstractClass const * target, bool all)
{
	GameComponents::Dispatch_Detach(self, target, all);
}

void Dispatch_Cell_Detach(CellClass & self, AbstractClass const * target)
{
	GameComponents::Dispatch_Detach(self, target, true);
}

void Dispatch_Loco_Detach(LocomotionClass & self, AbstractClass const * target, bool all)
{
	GameComponents::Dispatch_Detach(self, target, all);
}

void Dispatch_Object_Limbo(ObjectClass & self)
{
	GameComponents::Dispatch_Limbo(self);
}

void Dispatch_Object_Unlimbo(ObjectClass & self, Coord const & coord, Dir256 dir)
{
	GameComponents::Dispatch_Unlimbo(self, coord, dir);
}

void Dispatch_Loco_Limbo(ILocomotion & loco)
{
	if (LocomotionClass * root = As_Locomotion_Root(loco); root != nullptr) {
		GameComponents::Dispatch_Limbo(*root);
	}
}

void Dispatch_Loco_Unlimbo(ILocomotion & loco)
{
	if (LocomotionClass * root = As_Locomotion_Root(loco); root != nullptr) {
		GameComponents::Dispatch_Unlimbo(*root);
	}
}

void Dispatch_Delete_Me(ObjectClass & self)
{
	GameComponents::Dispatch_Delete_Me(self);
}

void Dispatch_Take_Damage(ObjectClass & self, int & damage, int distance, WarheadTypeClass const * warhead, TechnoClass * source, bool forced)
{
	GameComponents::Dispatch_Take_Damage(self, damage, distance, warhead, source, forced);
}

void Dispatch_Can_Fire(TechnoClass const & self, AbstractClass * target, int which, FireErrorType & verdict)
{
	GameComponents::Dispatch_Can_Fire(self, target, which, verdict);
}

void Dispatch_Fire_At(TechnoClass & self, AbstractClass * target, int which, BulletClass * bullet)
{
	GameComponents::Dispatch_Fire_At(self, target, which, bullet);
}

void Dispatch_Record_The_Kill(TechnoClass & self, TechnoClass * source)
{
	GameComponents::Dispatch_Record_The_Kill(self, source);
}

void Dispatch_Captured(TechnoClass & self, HouseClass * oldowner, HouseClass * newowner)
{
	GameComponents::Dispatch_Captured(self, oldowner, newowner);
}

void Dispatch_Captured(BuildingClass & self, HouseClass * oldowner, HouseClass * newowner)
{
	GameComponents::Dispatch_Captured(self, oldowner, newowner);
}

void Dispatch_Assign_Target(TechnoClass & self, AbstractClass * target)
{
	GameComponents::Dispatch_Assign_Target(self, target);
}

void Dispatch_Assign_Destination(TechnoClass & self, AbstractClass * dest)
{
	GameComponents::Dispatch_Assign_Destination(self, dest);
}

void Dispatch_Set_Coord(ObjectClass & self, Coord const & old_coord, Coord const & new_coord)
{
	GameComponents::Dispatch_Set_Coord(self, old_coord, new_coord);
}

void Dispatch_Per_Cell_Process(FootClass & self, PCPType why)
{
	GameComponents::Dispatch_Per_Cell_Process(self, why);
}

void Dispatch_Receive_Message(ObjectClass & self, RadioClass * from, RadioMessageType message, std::intptr_t & param)
{
	GameComponents::Dispatch_Receive_Message(self, from, message, param);
}

void Dispatch_Do_Cloak(TechnoClass & self)
{
	GameComponents::Dispatch_Do_Cloak(self);
}

void Dispatch_Do_Uncloak(TechnoClass & self)
{
	GameComponents::Dispatch_Do_Uncloak(self);
}

void Dispatch_Stun(TechnoClass & self)
{
	GameComponents::Dispatch_Stun(self);
}

void Dispatch_Select(ObjectClass & self)
{
	GameComponents::Dispatch_Select(self);
}

void Dispatch_Unselect(ObjectClass & self)
{
	GameComponents::Dispatch_Unselect(self);
}

void Dispatch_Hidden(ObjectClass & self)
{
	GameComponents::Dispatch_Hidden(self);
}

void Dispatch_Revealed(ObjectClass & self, HouseClass * house)
{
	GameComponents::Dispatch_Revealed(self, house);
}

void Dispatch_Grand_Opening(BuildingClass & self, bool captured)
{
	GameComponents::Dispatch_Grand_Opening(self, captured);
}

void Dispatch_Draw_Pre_Render(ObjectClass const & self, Point2D const & point, Rect const & cliprect)
{
	GameComponents::Dispatch_Draw_Pre_Render(self, point, cliprect);
}

void Dispatch_Draw_It(ObjectClass const & self, Point2D const & point, Rect const & cliprect)
{
	GameComponents::Dispatch_Draw_It(self, point, cliprect);
}

void Dispatch_Draw_Post_Render(ObjectClass const & self, Point2D const & point, Rect const & cliprect)
{
	GameComponents::Dispatch_Draw_Post_Render(self, point, cliprect);
}

void Dispatch_Occupy_Down(CellClass & self, ObjectClass * object, bool bridge)
{
	GameComponents::Dispatch_Occupy_Down(self, object, bridge);
}

void Dispatch_Occupy_Up(CellClass & self, ObjectClass * object, bool bridge)
{
	GameComponents::Dispatch_Occupy_Up(self, object, bridge);
}

void Dispatch_Recalc_Passability(CellClass & self)
{
	GameComponents::Dispatch_Recalc_Passability(self);
}

void Dispatch_Recalc_Attributes(CellClass & self)
{
	GameComponents::Dispatch_Recalc_Attributes(self);
}

void Dispatch_Grow_Tiberium(CellClass & self)
{
	GameComponents::Dispatch_Grow_Tiberium(self);
}

void Dispatch_Spread_Tiberium(CellClass & self, bool forced)
{
	GameComponents::Dispatch_Spread_Tiberium(self, forced);
}

void Dispatch_Reduce_Tiberium(CellClass & self, int levels)
{
	GameComponents::Dispatch_Reduce_Tiberium(self, levels);
}

void Dispatch_Goodie_Check(CellClass & self, FootClass * object)
{
	GameComponents::Dispatch_Goodie_Check(self, object);
}

void Dispatch_Cell_Draw_It(CellClass const & self, Point2D const & point, Rect const & cliprect)
{
	GameComponents::Dispatch_Cell_Draw_It(self, point, cliprect);
}

void Dispatch_Draw_Overlay(CellClass & self, Point2D const & point, Rect const & cliprect)
{
	GameComponents::Dispatch_Draw_Overlay(self, point, cliprect);
}

void Dispatch_Link_To_Object(LocomotionClass & self, FootClass * object)
{
	GameComponents::Dispatch_Link_To_Object(self, object);
}

void Dispatch_Loco_Process(ILocomotion & loco)
{
	if (LocomotionClass * root = As_Locomotion_Root(loco); root != nullptr) {
		GameComponents::Dispatch_Loco_Process(*root);
	}
}

void Dispatch_Loco_PostProcess(ILocomotion & loco, bool moving)
{
	if (LocomotionClass * root = As_Locomotion_Root(loco); root != nullptr) {
		GameComponents::Dispatch_Loco_PostProcess(*root, moving);
	}
}

void Dispatch_Move_To(ILocomotion & loco, Coord to)
{
	if (LocomotionClass * root = As_Locomotion_Root(loco); root != nullptr) {
		GameComponents::Dispatch_Move_To(*root, to);
	}
}

void Dispatch_Stop_Moving(ILocomotion & loco)
{
	if (LocomotionClass * root = As_Locomotion_Root(loco); root != nullptr) {
		GameComponents::Dispatch_Stop_Moving(*root);
	}
}

void Dispatch_Power_On(LocomotionClass & self)
{
	GameComponents::Dispatch_Power_On(self);
}

void Dispatch_Power_Off(LocomotionClass & self)
{
	GameComponents::Dispatch_Power_Off(self);
}

void Dispatch_Begin_Piggyback(LocomotionClass & carrier, ILocomotion * carried)
{
	GameComponents::Dispatch_Begin_Piggyback(carrier, carried);
}

void Dispatch_End_Piggyback(LocomotionClass & carrier, ILocomotion * restored)
{
	GameComponents::Dispatch_End_Piggyback(carrier, restored);
}

void Dispatch_Draw_Matrix(LocomotionClass & self, Matrix3D & matrix, int * key)
{
	GameComponents::Dispatch_Draw_Matrix(self, matrix, key);
}

void Compute_Entity_CRC(flecs::entity fe, CRCEngine & crc)
{
	GameComponents::Compute_Entity_CRC(fe, crc);
}

} // namespace Detail

} // namespace EC::Hooks
