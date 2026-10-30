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

bool GComponentsRegistered = false;

void Ensure_Registered(void)
{
	if (!GComponentsRegistered) {
		GameComponents::Register_All(World());
		GComponentsRegistered = true;
	}
}

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
				if (cur->Components().Owner() == loco) {
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

} // namespace

// ---------------------------------------------------------------------------
// Component Attachment Helpers
// ---------------------------------------------------------------------------

void Ensure_Produce_Cash(BuildingClass & self)
{
	Ensure_Registered();
	if (!self.Components.Has<ProduceCashComponent>()) {
		self.Components.Emplace<ProduceCashComponent>();
	}
}

// ---------------------------------------------------------------------------
// Global Hooks (G01..G12)
// ---------------------------------------------------------------------------

void AI_All(void)
{
	if constexpr (GameComponents::count > 0) {
		if (Live_Entity_Count() == 0) return;
		Ensure_Registered();
		GameComponents::AI_All(World());
	}
}

void PostAI_All(void)
{
	if constexpr (GameComponents::count > 0) {
		if (Live_Entity_Count() == 0) return;
		Ensure_Registered();
		GameComponents::PostAI_All(World());
	}
}

void Environment_AI_All(void)
{
	if constexpr (GameComponents::count > 0) {
		if (Live_Entity_Count() == 0) return;
		Ensure_Registered();
		GameComponents::Environment_AI_All(World());
	}
}

void Frame_Begin_All(void)
{
	if constexpr (GameComponents::count > 0) {
		if (Live_Entity_Count() == 0) return;
		Ensure_Registered();
		GameComponents::Frame_Begin_All(World());
	}
}

void Frame_End_All(void)
{
	if constexpr (GameComponents::count > 0) {
		if (Live_Entity_Count() == 0) return;
		Ensure_Registered();
		GameComponents::Frame_End_All(World());
	}
}

void Pre_Render_All(void)
{
	if constexpr (GameComponents::count > 0) {
		if (Live_Entity_Count() == 0) return;
		Ensure_Registered();
		GameComponents::Pre_Render_All(World());
	}
}

void Post_Render_All(void)
{
	if constexpr (GameComponents::count > 0) {
		if (Live_Entity_Count() == 0) return;
		Ensure_Registered();
		GameComponents::Post_Render_All(World());
	}
}

void Scenario_Init_All(void)
{
	Ensure_Registered();
	if constexpr (GameComponents::count > 0) {
		if (Live_Entity_Count() == 0) return;
		GameComponents::Scenario_Init_All(World());
	}
}

void Scenario_Clear_All(void)
{
	GameComponents::Scenario_Clear_All();
	World_Reset();
	GComponentsRegistered = false;
	Ensure_Registered();
}

bool Save_Trailer(SaveStreamClass & stream)
{
	if (Live_Entity_Count() == 0) {
		return !stream.Was_Error();
	}
	Ensure_Registered();
	return GameComponents::Save_Trailer(
		World(),
		stream,
		[](RootKind, void const * owner_ptr) -> SwizzleIDType {
			return Swizzler.Registered_ID_Of(owner_ptr);
		});
}

bool Load_Trailer(SaveStreamClass & stream)
{
	if (stream.Offset() >= stream.Size()) {
		return !stream.Was_Error();
	}
	Ensure_Registered();
	Swizzler.Prepare_Address_Lookup();
	return GameComponents::Load_Trailer(
		World(),
		stream,
		[](RootKind, SwizzleIDType swizzle_id) -> void * {
			return Swizzler.Address_Of(swizzle_id);
		});
}

void Post_Load_All(void)
{
	if constexpr (GameComponents::count > 0) {
		if (Live_Entity_Count() == 0) return;
		Ensure_Registered();
		GameComponents::Post_Load_All(World());
	}
}

void Compute_Cell_CRCs(CRCEngine & crc)
{
	if constexpr (GameComponents::count > 0) {
		if (Live_Entity_Count() == 0) return;
		Ensure_Registered();
		GameComponents::Compute_Root_CRC_All<CellClass>(World(), crc, Build_Stable_Owner_Key);
	} else {
		(void)crc;
	}
}

void Print_Component_CRCs(FILE * fp)
{
	if (fp == nullptr || Live_Entity_Count() == 0) {
		return;
	}
	if constexpr (GameComponents::count > 0) {
		Ensure_Registered();
		std::fprintf(fp, "\n----- Component checksums -----\n");
		GameComponents::Print_Component_CRCs(World(), fp, Build_Stable_Owner_Key);
	}
}

void Accumulate_Game_CRC(unsigned int & game_crc)
{
	if constexpr (GameComponents::count > 0) {
		if (Live_Entity_Count() == 0) return;
		Ensure_Registered();
		CRCEngine comp_crc;
		GameComponents::Compute_CRC_All(World(), comp_crc, Build_Stable_Owner_Key);
		int const digest = comp_crc();
		if (digest != 0) {
			Add_CRC(&game_crc, static_cast<unsigned int>(digest));
		}
	} else {
		(void)game_crc;
	}
}

void Shutdown_World(void)
{
	GComponentsRegistered = false;
	World_Shutdown();
}

// ---------------------------------------------------------------------------
// Out-of-line Entity Hook Slow Paths (E02..E27)
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

void Dispatch_Loco_Limbo(LocomotionClass & self)
{
	GameComponents::Dispatch_Limbo(self);
}

void Dispatch_Loco_Unlimbo(LocomotionClass & self)
{
	GameComponents::Dispatch_Unlimbo(self);
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

void Dispatch_Loco_Process(LocomotionClass & self)
{
	GameComponents::Dispatch_Loco_Process(self);
}

void Dispatch_Loco_PostProcess(LocomotionClass & self, bool moving)
{
	GameComponents::Dispatch_Loco_PostProcess(self, moving);
}

void Dispatch_Move_To(LocomotionClass & self, Coord to)
{
	GameComponents::Dispatch_Move_To(self, to);
}

void Dispatch_Stop_Moving(LocomotionClass & self)
{
	GameComponents::Dispatch_Stop_Moving(self);
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

void Compute_Object_CRC(ObjectClass const & self, CRCEngine & crc)
{
	GameComponents::Compute_Entity_CRC(self.Components, crc);
}

void Compute_Cell_CRC(CellClass const & self, CRCEngine & crc)
{
	GameComponents::Compute_Entity_CRC(self.Components, crc);
}

void Compute_Loco_CRC(LocomotionClass const & self, CRCEngine & crc)
{
	GameComponents::Compute_Entity_CRC(self.Components(), crc);
}

} // namespace Detail

} // namespace EC::Hooks
