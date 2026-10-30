/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

// Engine integration facade for global simulation/render/save/sync passes
// (G01..G12) and entity-scoped events (E02..E27) operating directly on the
// flecs::entity handle stored on ObjectClass, CellClass, and LocomotionClass.

#include "coord.h"
#include "ec/entity.h"
#include "face.h"
#include "point.h"

#include "fire.hh"
#include "pcp.hh"
#include "radio.hh"

#include <cstdint>
#include <cstdio>

template<class T> class TRect;
typedef TRect<int> Rect;

class AbstractClass;
class ObjectClass;
class TechnoClass;
class FootClass;
class BuildingClass;
class CellClass;
struct ILocomotion;
class LocomotionClass;
class WarheadTypeClass;
class BulletClass;
class HouseClass;
class RadioClass;
class Matrix3D;
class SaveStreamClass;
class CRCEngine;

namespace EC::Hooks {

// ---------------------------------------------------------------------------
// Global Lifecycle, Frame, Render, Save/Load, and Sync Hooks (G01..G12)
// ---------------------------------------------------------------------------

void AI_All(void);                                     // G01 (Pre-AI)
void PostAI_All(void);                                 // G02 (Post-AI)
void Environment_AI_All(void);                         // G03
void Frame_Begin_All(void);                            // G04
void Frame_End_All(void);                              // G05
void Pre_Render_All(void);                             // G06
void Post_Render_All(void);                            // G07
void Scenario_Init_All(void);                          // G08
void Scenario_Clear_All(void);                         // G09 (component static cleanup; live roots keep 1:1 entities)
bool Save_Trailer(SaveStreamClass & stream);           // G10a (EC01 trailer write)
bool Load_Trailer(SaveStreamClass & stream);           // G10b (EC01 two-subpass load)
void Post_Load_All(void);                              // G11 (after Swizzler.Resolve())
void Compute_Cell_CRCs(CRCEngine & crc);               // G12a (Object_CRCs cell pass)
void Print_Component_CRCs(FILE * fp);                  // G12b (Print_Heap_CRCs per-type & per-instance)
void Accumulate_Game_CRC(unsigned int & game_crc);     // G12c (Compute_Game_CRC)
void Shutdown_World(void);                             // Prog_End teardown

// ---------------------------------------------------------------------------
// Out-of-line component dispatchers (invoked when Has_User_Components == true)
// ---------------------------------------------------------------------------

namespace Detail {

void Dispatch_Object_Detach(ObjectClass & self, AbstractClass const * target, bool all);
void Dispatch_Cell_Detach(CellClass & self, AbstractClass const * target);
void Dispatch_Loco_Detach(LocomotionClass & self, AbstractClass const * target, bool all);

void Dispatch_Object_Limbo(ObjectClass & self);
void Dispatch_Object_Unlimbo(ObjectClass & self, Coord const & coord, Dir256 dir);
void Dispatch_Loco_Limbo(ILocomotion & loco);
void Dispatch_Loco_Unlimbo(ILocomotion & loco);
void Dispatch_Delete_Me(ObjectClass & self);

void Dispatch_Take_Damage(ObjectClass & self, int & damage, int distance, WarheadTypeClass const * warhead, TechnoClass * source, bool forced);
void Dispatch_Can_Fire(TechnoClass const & self, AbstractClass * target, int which, FireErrorType & verdict);
void Dispatch_Fire_At(TechnoClass & self, AbstractClass * target, int which, BulletClass * bullet);
void Dispatch_Record_The_Kill(TechnoClass & self, TechnoClass * source);
void Dispatch_Captured(TechnoClass & self, HouseClass * oldowner, HouseClass * newowner);
void Dispatch_Captured(BuildingClass & self, HouseClass * oldowner, HouseClass * newowner);
void Dispatch_Assign_Target(TechnoClass & self, AbstractClass * target);
void Dispatch_Assign_Destination(TechnoClass & self, AbstractClass * dest);
void Dispatch_Set_Coord(ObjectClass & self, Coord const & old_coord, Coord const & new_coord);
void Dispatch_Per_Cell_Process(FootClass & self, PCPType why);
void Dispatch_Receive_Message(ObjectClass & self, RadioClass * from, RadioMessageType message, std::intptr_t & param);
void Dispatch_Do_Cloak(TechnoClass & self);
void Dispatch_Do_Uncloak(TechnoClass & self);
void Dispatch_Stun(TechnoClass & self);
void Dispatch_Select(ObjectClass & self);
void Dispatch_Unselect(ObjectClass & self);
void Dispatch_Hidden(ObjectClass & self);
void Dispatch_Revealed(ObjectClass & self, HouseClass * house);
void Dispatch_Grand_Opening(BuildingClass & self, bool captured);
void Dispatch_Draw_Pre_Render(ObjectClass const & self, Point2D const & point, Rect const & cliprect);
void Dispatch_Draw_It(ObjectClass const & self, Point2D const & point, Rect const & cliprect);
void Dispatch_Draw_Post_Render(ObjectClass const & self, Point2D const & point, Rect const & cliprect);

void Dispatch_Occupy_Down(CellClass & self, ObjectClass * object, bool bridge);
void Dispatch_Occupy_Up(CellClass & self, ObjectClass * object, bool bridge);
void Dispatch_Recalc_Passability(CellClass & self);
void Dispatch_Recalc_Attributes(CellClass & self);
void Dispatch_Grow_Tiberium(CellClass & self);
void Dispatch_Spread_Tiberium(CellClass & self, bool forced);
void Dispatch_Reduce_Tiberium(CellClass & self, int levels);
void Dispatch_Goodie_Check(CellClass & self, FootClass * object);
void Dispatch_Cell_Draw_It(CellClass const & self, Point2D const & point, Rect const & cliprect);
void Dispatch_Draw_Overlay(CellClass & self, Point2D const & point, Rect const & cliprect);

void Dispatch_Link_To_Object(LocomotionClass & self, FootClass * object);
void Dispatch_Loco_Process(ILocomotion & loco);
void Dispatch_Loco_PostProcess(ILocomotion & loco, bool moving);
void Dispatch_Move_To(ILocomotion & loco, Coord to);
void Dispatch_Stop_Moving(ILocomotion & loco);
void Dispatch_Power_On(LocomotionClass & self);
void Dispatch_Power_Off(LocomotionClass & self);
void Dispatch_Begin_Piggyback(LocomotionClass & carrier, ILocomotion * carried);
void Dispatch_End_Piggyback(LocomotionClass & carrier, ILocomotion * restored);
void Dispatch_Draw_Matrix(LocomotionClass & self, Matrix3D & matrix, int * key);

void Compute_Entity_CRC(flecs::entity fe, CRCEngine & crc);

} // namespace Detail

// ---------------------------------------------------------------------------
// Inline Entity Hook Wrappers Operating Directly on flecs::entity (E02..E27)
// ---------------------------------------------------------------------------

template<typename ObjT>
inline void On_Object_Detach(ObjT & self, AbstractClass const * target, bool all)
{
	if (Has_User_Components(self.Entity)) {
		Detail::Dispatch_Object_Detach(self, target, all);
	}
}

template<typename CellT>
inline void On_Cell_Detach(CellT & self, AbstractClass const * target)
{
	if (Has_User_Components(self.Entity)) {
		Detail::Dispatch_Cell_Detach(self, target);
	}
}

template<typename LocoIface>
inline void On_Loco_Detach(LocoIface & loco, AbstractClass const * target, bool all)
{
	if (Has_User_Components(Entity_Of(loco))) {
		Detail::Dispatch_Loco_Detach(static_cast<LocomotionClass &>(loco), target, all);
	}
}

template<typename ObjT>
inline void On_Limbo(ObjT & self)
{
	self.Entity.add(flecs::Disabled);
	if (Has_User_Components(self.Entity)) {
		Detail::Dispatch_Object_Limbo(self);
	}
}

template<typename ObjT>
inline void On_Unlimbo(ObjT & self, Coord const & coord, Dir256 dir)
{
	self.Entity.remove(flecs::Disabled);
	if (Has_User_Components(self.Entity)) {
		Detail::Dispatch_Object_Unlimbo(self, coord, dir);
	}
}

template<typename LocoIface>
inline void On_Loco_Limbo(LocoIface & loco)
{
	flecs::entity fe = Entity_Of(loco);
	fe.add(flecs::Disabled);
	if (Has_User_Components(fe)) {
		Detail::Dispatch_Loco_Limbo(loco);
	}
}

template<typename LocoIface>
inline void On_Loco_Unlimbo(LocoIface & loco)
{
	flecs::entity fe = Entity_Of(loco);
	fe.remove(flecs::Disabled);
	if (Has_User_Components(fe)) {
		Detail::Dispatch_Loco_Unlimbo(loco);
	}
}

template<typename ObjT>
inline void On_Delete_Me(ObjT & self)
{
	if (Has_User_Components(self.Entity)) {
		Detail::Dispatch_Delete_Me(self);
	}
	self.Entity.add(flecs::Disabled);
}

template<typename ObjT>
inline void On_Take_Damage(ObjT & self, int & damage, int distance, WarheadTypeClass const * warhead, TechnoClass * source, bool forced)
{
	if (Has_User_Components(self.Entity)) {
		Detail::Dispatch_Take_Damage(self, damage, distance, warhead, source, forced);
	}
}

template<typename TechT>
inline void On_Can_Fire(TechT const & self, AbstractClass * target, int which, FireErrorType & verdict)
{
	if (Has_User_Components(self.Entity)) {
		Detail::Dispatch_Can_Fire(self, target, which, verdict);
	}
}

template<typename TechT>
inline void On_Fire_At(TechT & self, AbstractClass * target, int which, BulletClass * bullet)
{
	if (Has_User_Components(self.Entity)) {
		Detail::Dispatch_Fire_At(self, target, which, bullet);
	}
}

template<typename TechT>
inline void On_Record_The_Kill(TechT & self, TechnoClass * source)
{
	if (Has_User_Components(self.Entity)) {
		Detail::Dispatch_Record_The_Kill(self, source);
	}
}

template<typename TechT>
inline void On_Captured(TechT & self, HouseClass * oldowner, HouseClass * newowner)
{
	if (Has_User_Components(self.Entity)) {
		Detail::Dispatch_Captured(self, oldowner, newowner);
	}
}

template<typename TechT>
inline void On_Assign_Target(TechT & self, AbstractClass * target)
{
	if (Has_User_Components(self.Entity)) {
		Detail::Dispatch_Assign_Target(self, target);
	}
}

template<typename TechT>
inline void On_Assign_Destination(TechT & self, AbstractClass * dest)
{
	if (Has_User_Components(self.Entity)) {
		Detail::Dispatch_Assign_Destination(self, dest);
	}
}

template<typename ObjT>
inline void On_Set_Coord(ObjT & self, Coord const & old_coord, Coord const & new_coord)
{
	if (Has_User_Components(self.Entity)) {
		Detail::Dispatch_Set_Coord(self, old_coord, new_coord);
	}
}

template<typename FootT>
inline void On_Per_Cell_Process(FootT & self, PCPType why)
{
	if (Has_User_Components(self.Entity)) {
		Detail::Dispatch_Per_Cell_Process(self, why);
	}
}

template<typename ObjT>
inline void On_Receive_Message(ObjT & self, RadioClass * from, RadioMessageType message, std::intptr_t & param)
{
	if (Has_User_Components(self.Entity)) {
		Detail::Dispatch_Receive_Message(self, from, message, param);
	}
}

template<typename TechT>
inline void On_Do_Cloak(TechT & self)
{
	if (Has_User_Components(self.Entity)) {
		Detail::Dispatch_Do_Cloak(self);
	}
}

template<typename TechT>
inline void On_Do_Uncloak(TechT & self)
{
	if (Has_User_Components(self.Entity)) {
		Detail::Dispatch_Do_Uncloak(self);
	}
}

template<typename TechT>
inline void On_Stun(TechT & self)
{
	if (Has_User_Components(self.Entity)) {
		Detail::Dispatch_Stun(self);
	}
}

template<typename ObjT>
inline void On_Select(ObjT & self)
{
	if (Has_User_Components(self.Entity)) {
		Detail::Dispatch_Select(self);
	}
}

template<typename ObjT>
inline void On_Unselect(ObjT & self)
{
	if (Has_User_Components(self.Entity)) {
		Detail::Dispatch_Unselect(self);
	}
}

template<typename ObjT>
inline void On_Hidden(ObjT & self)
{
	if (Has_User_Components(self.Entity)) {
		Detail::Dispatch_Hidden(self);
	}
}

template<typename ObjT>
inline void On_Revealed(ObjT & self, HouseClass * house)
{
	if (Has_User_Components(self.Entity)) {
		Detail::Dispatch_Revealed(self, house);
	}
}

template<typename BldT>
inline void On_Grand_Opening(BldT & self, bool captured)
{
	if (Has_User_Components(self.Entity)) {
		Detail::Dispatch_Grand_Opening(self, captured);
	}
}

template<typename ObjT>
inline void On_Draw_Pre_Render(ObjT const & self, Point2D const & point, Rect const & cliprect)
{
	if (Has_User_Components(self.Entity)) {
		Detail::Dispatch_Draw_Pre_Render(self, point, cliprect);
	}
}

template<typename ObjT>
inline void On_Draw_It(ObjT const & self, Point2D const & point, Rect const & cliprect)
{
	if (Has_User_Components(self.Entity)) {
		Detail::Dispatch_Draw_It(self, point, cliprect);
	}
}

template<typename ObjT>
inline void On_Draw_Post_Render(ObjT const & self, Point2D const & point, Rect const & cliprect)
{
	if (Has_User_Components(self.Entity)) {
		Detail::Dispatch_Draw_Post_Render(self, point, cliprect);
	}
}

template<typename CellT>
inline void On_Occupy_Down(CellT & self, ObjectClass * object, bool bridge)
{
	if (Has_User_Components(self.Entity)) {
		Detail::Dispatch_Occupy_Down(self, object, bridge);
	}
}

template<typename CellT>
inline void On_Occupy_Up(CellT & self, ObjectClass * object, bool bridge)
{
	if (Has_User_Components(self.Entity)) {
		Detail::Dispatch_Occupy_Up(self, object, bridge);
	}
}

template<typename CellT>
inline void On_Recalc_Passability(CellT & self)
{
	if (Has_User_Components(self.Entity)) {
		Detail::Dispatch_Recalc_Passability(self);
	}
}

template<typename CellT>
inline void On_Recalc_Attributes(CellT & self)
{
	if (Has_User_Components(self.Entity)) {
		Detail::Dispatch_Recalc_Attributes(self);
	}
}

template<typename CellT>
inline void On_Grow_Tiberium(CellT & self)
{
	if (Has_User_Components(self.Entity)) {
		Detail::Dispatch_Grow_Tiberium(self);
	}
}

template<typename CellT>
inline void On_Spread_Tiberium(CellT & self, bool forced)
{
	if (Has_User_Components(self.Entity)) {
		Detail::Dispatch_Spread_Tiberium(self, forced);
	}
}

template<typename CellT>
inline void On_Reduce_Tiberium(CellT & self, int levels)
{
	if (Has_User_Components(self.Entity)) {
		Detail::Dispatch_Reduce_Tiberium(self, levels);
	}
}

template<typename CellT>
inline void On_Goodie_Check(CellT & self, FootClass * object)
{
	if (Has_User_Components(self.Entity)) {
		Detail::Dispatch_Goodie_Check(self, object);
	}
}

template<typename CellT>
inline void On_Cell_Draw_It(CellT const & self, Point2D const & point, Rect const & cliprect)
{
	if (Has_User_Components(self.Entity)) {
		Detail::Dispatch_Cell_Draw_It(self, point, cliprect);
	}
}

template<typename CellT>
inline void On_Draw_Overlay(CellT & self, Point2D const & point, Rect const & cliprect)
{
	if (Has_User_Components(self.Entity)) {
		Detail::Dispatch_Draw_Overlay(self, point, cliprect);
	}
}

template<typename LocoT>
inline void On_Link_To_Object(LocoT & self, FootClass * object)
{
	if (Has_User_Components(self.Entity)) {
		Detail::Dispatch_Link_To_Object(self, object);
	}
}

template<typename LocoIface>
inline void On_Loco_Process(LocoIface & loco)
{
	if (Has_User_Components(Entity_Of(loco))) {
		Detail::Dispatch_Loco_Process(loco);
	}
}

template<typename LocoIface>
inline void On_Loco_PostProcess(LocoIface & loco, bool moving)
{
	if (Has_User_Components(Entity_Of(loco))) {
		Detail::Dispatch_Loco_PostProcess(loco, moving);
	}
}

template<typename LocoIface>
inline void On_Move_To(LocoIface & loco, Coord to)
{
	if (Has_User_Components(Entity_Of(loco))) {
		Detail::Dispatch_Move_To(loco, to);
	}
}

template<typename LocoIface>
inline void On_Stop_Moving(LocoIface & loco)
{
	if (Has_User_Components(Entity_Of(loco))) {
		Detail::Dispatch_Stop_Moving(loco);
	}
}

template<typename LocoT>
inline void On_Power_On(LocoT & self)
{
	if (Has_User_Components(self.Entity)) {
		Detail::Dispatch_Power_On(self);
	}
}

template<typename LocoT>
inline void On_Power_Off(LocoT & self)
{
	if (Has_User_Components(self.Entity)) {
		Detail::Dispatch_Power_Off(self);
	}
}

template<typename LocoT>
inline void On_Begin_Piggyback(LocoT & carrier, ILocomotion * carried)
{
	Detail::Dispatch_Begin_Piggyback(carrier, carried);
}

template<typename LocoT>
inline void On_End_Piggyback(LocoT & carrier, ILocomotion * restored)
{
	Detail::Dispatch_End_Piggyback(carrier, restored);
}

template<typename LocoT>
inline void On_Draw_Matrix(LocoT & self, Matrix3D & matrix, int * key)
{
	if (Has_User_Components(self.Entity)) {
		Detail::Dispatch_Draw_Matrix(self, matrix, key);
	}
}

template<typename ObjT>
inline void Compute_Object_CRC(ObjT const & self, CRCEngine & crc)
{
	if (Has_User_Components(self.Entity)) {
		Detail::Compute_Entity_CRC(self.Entity, crc);
	}
}

template<typename CellT>
inline void Compute_Cell_CRC(CellT const & self, CRCEngine & crc)
{
	if (Has_User_Components(self.Entity)) {
		Detail::Compute_Entity_CRC(self.Entity, crc);
	}
}

template<typename LocoIface>
inline void Compute_Loco_CRC(LocoIface const & loco, CRCEngine & crc)
{
	flecs::entity fe = Entity_Of(loco);
	if (Has_User_Components(fe)) {
		Detail::Compute_Entity_CRC(fe, crc);
	}
}

} // namespace EC::Hooks
