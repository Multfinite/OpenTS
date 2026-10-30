/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include "ec/entity.h"
#include "ec/fnv1a.h"

#include <concepts>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>

class AbstractClass;
class ObjectClass;
class TechnoClass;
class FootClass;
class BuildingClass;
class CellClass;
struct ILocomotion;
class LocomotionClass;
class CRCEngine;

namespace EC {

// ---------------------------------------------------------------------------
// Explicit Subclass / Root Owner Affinity Helpers
// ---------------------------------------------------------------------------

template<typename C>
concept HasExplicitOwnerType = requires { typename C::OwnerType; };

template<typename C>
concept HasDerivedObjectOwner = HasExplicitOwnerType<C> &&
	std::derived_from<typename C::OwnerType, ObjectClass> &&
	!std::same_as<typename C::OwnerType, ObjectClass>;

// ---------------------------------------------------------------------------
// Component SaveName & Compile-Time Type Tagging
// ---------------------------------------------------------------------------

template<typename C>
concept HasSaveName = requires {
	{ std::string_view(C::SaveName) } -> std::convertible_to<std::string_view>;
} && (!std::string_view(C::SaveName).empty());

template<typename C>
consteval std::string_view Component_Display_Name(void) noexcept
{
	if constexpr (HasSaveName<C>) {
		return std::string_view(C::SaveName);
	} else {
#if defined(_MSC_VER)
		return __FUNCSIG__;
#else
		return __PRETTY_FUNCTION__;
#endif
	}
}

template<typename C>
consteval std::uint64_t Component_Save_Id(void) noexcept
{
	if constexpr (HasSaveName<C>) {
		return Fnv1a_64(std::string_view(C::SaveName));
	} else {
		return 0ULL;
	}
}

template<typename C>
consteval std::uint32_t Component_Type_Tag_32(void) noexcept
{
	return Fnv1a_32_Fold(Fnv1a_64(Component_Display_Name<C>()));
}

// ---------------------------------------------------------------------------
// Global Hook Concepts (G01..G12) — Void, flecs::entity, DerivedOwner & Root
// ---------------------------------------------------------------------------

#define OPENTS_EC_DEFINE_MUT_GLOBAL_CONCEPT(ConceptPrefix, MethodName) \
	template<typename C> concept ConceptPrefix##Void = requires(C & c) { c.MethodName(); }; \
	template<typename C> concept ConceptPrefix##Entity = requires(C & c, flecs::entity fe) { c.MethodName(fe); }; \
	template<typename C> concept ConceptPrefix##DerivedOwner = \
		HasDerivedObjectOwner<C> && requires(C & c, typename C::OwnerType & o) { c.MethodName(o); }; \
	template<typename C, typename Root> concept ConceptPrefix##Root = \
		EntityRoot<Root> && requires(C & c, Root & r) { c.MethodName(r); }; \
	template<typename C> concept Has##ConceptPrefix = \
		ConceptPrefix##Void<C> || ConceptPrefix##Entity<C> || \
		ConceptPrefix##DerivedOwner<C> || \
		ConceptPrefix##Root<C, ObjectClass> || \
		ConceptPrefix##Root<C, CellClass> || \
		ConceptPrefix##Root<C, LocomotionClass>;

#define OPENTS_EC_DEFINE_CONST_GLOBAL_CONCEPT(ConceptPrefix, MethodName) \
	template<typename C> concept ConceptPrefix##Void = requires(C const & c) { c.MethodName(); }; \
	template<typename C> concept ConceptPrefix##Entity = requires(C const & c, flecs::entity fe) { c.MethodName(fe); }; \
	template<typename C> concept ConceptPrefix##DerivedOwner = \
		HasDerivedObjectOwner<C> && requires(C const & c, typename C::OwnerType const & o) { c.MethodName(o); }; \
	template<typename C, typename Root> concept ConceptPrefix##Root = \
		EntityRoot<Root> && requires(C const & c, Root const & r) { c.MethodName(r); }; \
	template<typename C> concept Has##ConceptPrefix = \
		ConceptPrefix##Void<C> || ConceptPrefix##Entity<C> || \
		ConceptPrefix##DerivedOwner<C> || \
		ConceptPrefix##Root<C, ObjectClass> || \
		ConceptPrefix##Root<C, CellClass> || \
		ConceptPrefix##Root<C, LocomotionClass>;

OPENTS_EC_DEFINE_MUT_GLOBAL_CONCEPT(AI, AI)                           // G01 (Pre-AI)
OPENTS_EC_DEFINE_MUT_GLOBAL_CONCEPT(PostAI, PostAI)                   // G02 (Post-AI)
OPENTS_EC_DEFINE_MUT_GLOBAL_CONCEPT(EnvironmentAI, Environment_AI)    // G03
OPENTS_EC_DEFINE_MUT_GLOBAL_CONCEPT(FrameBegin, Frame_Begin)          // G04
OPENTS_EC_DEFINE_MUT_GLOBAL_CONCEPT(FrameEnd, Frame_End)              // G05
OPENTS_EC_DEFINE_CONST_GLOBAL_CONCEPT(PreRender, Pre_Render)          // G06
OPENTS_EC_DEFINE_CONST_GLOBAL_CONCEPT(PostRender, Post_Render)        // G07
OPENTS_EC_DEFINE_MUT_GLOBAL_CONCEPT(ScenarioInit, Scenario_Init)      // G08
OPENTS_EC_DEFINE_MUT_GLOBAL_CONCEPT(PostLoad, Post_Load)              // G11

// E01: Native flecs component on_add / on_remove lifecycle hooks
OPENTS_EC_DEFINE_MUT_GLOBAL_CONCEPT(OnAdd, On_Add)
OPENTS_EC_DEFINE_MUT_GLOBAL_CONCEPT(OnAttach, On_Attach)
OPENTS_EC_DEFINE_MUT_GLOBAL_CONCEPT(OnRemove, On_Remove)

#undef OPENTS_EC_DEFINE_MUT_GLOBAL_CONCEPT
#undef OPENTS_EC_DEFINE_CONST_GLOBAL_CONCEPT

// G09: Static scenario clear hook
template<typename C>
concept HasScenarioClear = requires { C::Scenario_Clear(); };

// G10: Binary save/load hook
template<typename C, typename StreamT>
concept HasSerialize = HasSaveName<C> && requires(C & c, StreamT & s) {
	c.Serialize(s);
};

// G12 / E27: Per-component CRC hooks
template<typename C>
concept HasComputeCRCPlain = requires(C const & c, CRCEngine & crc) {
	c.Compute_CRC(crc);
};

template<typename C>
concept HasComputeCRCEntity = requires(C const & c, CRCEngine & crc, flecs::entity fe) {
	c.Compute_CRC(crc, fe);
};

template<typename C>
concept HasComputeCRCDerivedOwner = HasDerivedObjectOwner<C> &&
	requires(C const & c, CRCEngine & crc, typename C::OwnerType const & o) {
		c.Compute_CRC(crc, o);
	};

template<typename C, typename Root>
concept HasComputeCRCRoot = EntityRoot<Root> && requires(C const & c, CRCEngine & crc, Root const & r) {
	c.Compute_CRC(crc, r);
};

template<typename C>
concept HasComputeCRC =
	HasComputeCRCPlain<C> ||
	HasComputeCRCEntity<C> ||
	HasComputeCRCDerivedOwner<C> ||
	HasComputeCRCRoot<C, ObjectClass> ||
	HasComputeCRCRoot<C, CellClass> ||
	HasComputeCRCRoot<C, LocomotionClass>;

// ---------------------------------------------------------------------------
// Compile-Time Root Affinity Validation (AllowedOnRoot<C, Root>)
// ---------------------------------------------------------------------------

template<typename C, EntityRoot Root>
consteval bool Has_Any_Root_Typed_Hook_For(void) noexcept
{
	return
		AIRoot<C, Root> ||
		PostAIRoot<C, Root> ||
		EnvironmentAIRoot<C, Root> ||
		FrameBeginRoot<C, Root> ||
		FrameEndRoot<C, Root> ||
		PreRenderRoot<C, Root> ||
		PostRenderRoot<C, Root> ||
		ScenarioInitRoot<C, Root> ||
		PostLoadRoot<C, Root> ||
		OnAddRoot<C, Root> ||
		OnAttachRoot<C, Root> ||
		OnRemoveRoot<C, Root> ||
		HasComputeCRCRoot<C, Root>;
}

template<typename C, EntityRoot Root>
consteval bool Is_Allowed_On_Root(void) noexcept
{
	if constexpr (HasExplicitOwnerType<C>) {
		using OwnerT = typename C::OwnerType;
		if constexpr (std::same_as<OwnerT, Root>) {
			return true;
		} else if constexpr (std::same_as<Root, ObjectClass> && std::derived_from<OwnerT, ObjectClass>) {
			return true;
		} else {
			return false;
		}
	} else {
		constexpr bool uses_obj = Has_Any_Root_Typed_Hook_For<C, ObjectClass>();
		constexpr bool uses_cell = Has_Any_Root_Typed_Hook_For<C, CellClass>();
		constexpr bool uses_loco = Has_Any_Root_Typed_Hook_For<C, LocomotionClass>();
		if constexpr (!uses_obj && !uses_cell && !uses_loco) {
			return true;
		} else if constexpr (std::same_as<Root, ObjectClass>) {
			return uses_obj;
		} else if constexpr (std::same_as<Root, CellClass>) {
			return uses_cell;
		} else {
			return uses_loco;
		}
	}
}

template<typename C, typename Root>
concept AllowedOnRoot = EntityRoot<Root> && Is_Allowed_On_Root<C, Root>();

// ---------------------------------------------------------------------------
// Entity-Scoped Hook Concepts (E02..E26)
// ---------------------------------------------------------------------------

// E02: Detach (dangling pointer scrub)
template<typename C>
concept HasDetachPlain = requires(C & c, AbstractClass const * target, bool all) {
	c.Detach(target, all);
};
template<typename C, typename Root>
concept HasDetachRoot = EntityRoot<Root> && requires(C & c, Root & self, AbstractClass const * target, bool all) {
	c.Detach(self, target, all);
};

// E03..E26: Parameterized entity event concepts so exact engine argument types
// forward statically with zero dynamic wrappers.
#define OPENTS_EC_DEFINE_ENTITY_CONCEPT(ConceptName, MethodName) \
	template<typename C, typename SelfT, typename... Args> \
	concept ConceptName##WithSelf = requires(C & c, SelfT & self, Args... args) { \
		c.MethodName(self, std::forward<Args>(args)...); \
	}; \
	template<typename C, typename... Args> \
	concept ConceptName##NoSelf = requires(C & c, Args... args) { \
		c.MethodName(std::forward<Args>(args)...); \
	};

#define OPENTS_EC_DEFINE_CONST_ENTITY_CONCEPT(ConceptName, MethodName) \
	template<typename C, typename SelfT, typename... Args> \
	concept ConceptName##WithSelf = requires(C const & c, SelfT const & self, Args... args) { \
		c.MethodName(self, std::forward<Args>(args)...); \
	}; \
	template<typename C, typename... Args> \
	concept ConceptName##NoSelf = requires(C const & c, Args... args) { \
		c.MethodName(std::forward<Args>(args)...); \
	};

OPENTS_EC_DEFINE_ENTITY_CONCEPT(HasLimbo, Limbo)                             // E03a
OPENTS_EC_DEFINE_ENTITY_CONCEPT(HasUnlimbo, Unlimbo)                         // E03b
OPENTS_EC_DEFINE_ENTITY_CONCEPT(HasDeleteMe, Delete_Me)                      // E04
OPENTS_EC_DEFINE_ENTITY_CONCEPT(HasTakeDamage, Take_Damage)                  // E05
OPENTS_EC_DEFINE_CONST_ENTITY_CONCEPT(HasCanFire, Can_Fire)                  // E06a
OPENTS_EC_DEFINE_ENTITY_CONCEPT(HasFireAt, Fire_At)                          // E06b
OPENTS_EC_DEFINE_ENTITY_CONCEPT(HasRecordTheKill, Record_The_Kill)           // E07
OPENTS_EC_DEFINE_ENTITY_CONCEPT(HasCaptured, Captured)                       // E08
OPENTS_EC_DEFINE_ENTITY_CONCEPT(HasAssignTarget, Assign_Target)              // E09a
OPENTS_EC_DEFINE_ENTITY_CONCEPT(HasAssignDestination, Assign_Destination)    // E09b
OPENTS_EC_DEFINE_ENTITY_CONCEPT(HasSetCoord, Set_Coord)                      // E10
OPENTS_EC_DEFINE_ENTITY_CONCEPT(HasPerCellProcess, Per_Cell_Process)         // E11
OPENTS_EC_DEFINE_ENTITY_CONCEPT(HasReceiveMessage, Receive_Message)          // E12
OPENTS_EC_DEFINE_ENTITY_CONCEPT(HasDoCloak, Do_Cloak)                        // E13a
OPENTS_EC_DEFINE_ENTITY_CONCEPT(HasDoUncloak, Do_Uncloak)                    // E13b
OPENTS_EC_DEFINE_ENTITY_CONCEPT(HasStun, Stun)                               // E13c
OPENTS_EC_DEFINE_ENTITY_CONCEPT(HasSelect, Select)                           // E14a
OPENTS_EC_DEFINE_ENTITY_CONCEPT(HasUnselect, Unselect)                       // E14b
OPENTS_EC_DEFINE_ENTITY_CONCEPT(HasHidden, Hidden)                           // E15a
OPENTS_EC_DEFINE_ENTITY_CONCEPT(HasRevealed, Revealed)                       // E15b
OPENTS_EC_DEFINE_ENTITY_CONCEPT(HasGrandOpening, Grand_Opening)              // E15c (BuildingClass)
OPENTS_EC_DEFINE_CONST_ENTITY_CONCEPT(HasDrawPreRender, Draw_Pre_Render)     // E16a
OPENTS_EC_DEFINE_CONST_ENTITY_CONCEPT(HasDrawIt, Draw_It)                    // E16b
OPENTS_EC_DEFINE_CONST_ENTITY_CONCEPT(HasDrawPostRender, Draw_Post_Render)   // E16c

OPENTS_EC_DEFINE_ENTITY_CONCEPT(HasOccupyDown, Occupy_Down)                  // E17a
OPENTS_EC_DEFINE_ENTITY_CONCEPT(HasOccupyUp, Occupy_Up)                      // E17b
OPENTS_EC_DEFINE_ENTITY_CONCEPT(HasRecalcPassability, Recalc_Passability)    // E18a
OPENTS_EC_DEFINE_ENTITY_CONCEPT(HasRecalcAttributes, Recalc_Attributes)      // E18b
OPENTS_EC_DEFINE_ENTITY_CONCEPT(HasGrowTiberium, Grow_Tiberium)              // E19a
OPENTS_EC_DEFINE_ENTITY_CONCEPT(HasSpreadTiberium, Spread_Tiberium)          // E19b
OPENTS_EC_DEFINE_ENTITY_CONCEPT(HasReduceTiberium, Reduce_Tiberium)          // E19c
OPENTS_EC_DEFINE_ENTITY_CONCEPT(HasGoodieCheck, Goodie_Check)                // E20
OPENTS_EC_DEFINE_CONST_ENTITY_CONCEPT(HasCellDrawIt, Cell_Draw_It)           // E21a
OPENTS_EC_DEFINE_ENTITY_CONCEPT(HasDrawOverlay, Draw_Overlay)                // E21b

OPENTS_EC_DEFINE_ENTITY_CONCEPT(HasLinkToObject, Link_To_Object)             // E22
OPENTS_EC_DEFINE_ENTITY_CONCEPT(HasLocoProcess, Loco_Process)                // E23a
OPENTS_EC_DEFINE_ENTITY_CONCEPT(HasLocoPostProcess, Loco_PostProcess)        // E23b
OPENTS_EC_DEFINE_ENTITY_CONCEPT(HasMoveTo, Move_To)                          // E24a
OPENTS_EC_DEFINE_ENTITY_CONCEPT(HasStopMoving, Stop_Moving)                  // E24b
OPENTS_EC_DEFINE_ENTITY_CONCEPT(HasPowerOn, Power_On)                        // E25a
OPENTS_EC_DEFINE_ENTITY_CONCEPT(HasPowerOff, Power_Off)                      // E25b
OPENTS_EC_DEFINE_ENTITY_CONCEPT(HasBeginPiggyback, Begin_Piggyback)          // E26a
OPENTS_EC_DEFINE_ENTITY_CONCEPT(HasEndPiggyback, End_Piggyback)              // E26b
OPENTS_EC_DEFINE_ENTITY_CONCEPT(HasDrawMatrix, Draw_Matrix)                  // E26c

#undef OPENTS_EC_DEFINE_ENTITY_CONCEPT
#undef OPENTS_EC_DEFINE_CONST_ENTITY_CONCEPT

} // namespace EC
