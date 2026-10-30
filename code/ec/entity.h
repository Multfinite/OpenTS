/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

// Core entity-root traits, strongly typed OwnerLink<Root> back-reference, and
// 1:1 constructor/destructor lifecycle helpers for flecs::entity handles stored
// directly on ObjectClass, CellClass, and LocomotionClass.

#include "ec/world.h"

#include <compare>
#include <concepts>
#include <cstdint>
#include <type_traits>

class ObjectClass;
class CellClass;
class LocomotionClass;
struct ILocomotion;

namespace EC {

enum class RootKind : std::uint8_t
{
	Object = 0,
	Cell = 1,
	Locomotor = 2,
};

// Deterministic ordering key used by Print_Component_CRCs and global CRC passes
// so component sync digests never depend on raw pointer addresses or archetype order.
struct StableOwnerKey
{
	std::uint8_t Root = 0;
	int PrimaryId = 0;
	int SubSlot = 0;

	friend constexpr auto operator<=>(StableOwnerKey const &, StableOwnerKey const &) noexcept = default;
};

template<typename T>
concept EntityRoot =
	std::same_as<T, ObjectClass> ||
	std::same_as<T, CellClass> ||
	std::same_as<T, LocomotionClass>;

template<EntityRoot Root>
consteval RootKind Root_Kind_Of(void) noexcept
{
	if constexpr (std::same_as<Root, ObjectClass>) {
		return RootKind::Object;
	} else if constexpr (std::same_as<Root, CellClass>) {
		return RootKind::Cell;
	} else {
		return RootKind::Locomotor;
	}
}

// Strongly typed back-link component stored on every root's flecs::entity,
// allowing dense table iteration to pass ObjectClass&, CellClass&, or
// LocomotionClass& statically with zero void* or dynamic_cast.
template<EntityRoot Root>
struct OwnerLink
{
	Root * ptr = nullptr;
};

// Creates a flecs::entity bound 1:1 to `owner` (called in the root constructor).
template<EntityRoot Root>
inline flecs::entity Create_Entity(Root * owner, bool disabled = false) noexcept
{
	flecs::entity entity = World().entity();
	entity.set<OwnerLink<Root>>(OwnerLink<Root>{owner});
	if (disabled) {
		entity.add(flecs::Disabled);
	}
	return entity;
}

template<EntityRoot Root>
inline flecs::entity Create_Entity(Root & owner, bool disabled = false) noexcept
{
	return Create_Entity(&owner, disabled);
}

// Destroys a root's flecs::entity (called in the root destructor and before
// legacy placement-new reconstruction of CellClass slots).
void Destroy_Entity(flecs::entity & entity) noexcept;

// Uniformly extracts the flecs::entity handle from any root object or ILocomotion.
template<typename OwnerT>
inline flecs::entity Entity_Of(OwnerT const & owner) noexcept
{
	if constexpr (requires { { owner.Entity } -> std::convertible_to<flecs::entity>; }) {
		return owner.Entity;
	} else {
		return owner.Get_Entity();
	}
}

// Fast O(1) flecs archetype-table check: returns true iff `fe` carries at least
// one component beyond its mandatory OwnerLink<Root> (and optional flecs::Disabled).
inline bool Has_User_Components(flecs::entity fe) noexcept
{
	if (fe.id() == 0 || !Is_World_Alive()) {
		return false;
	}
	ecs_world_t const * world_ptr = fe.world().c_ptr();
	ecs_table_t const * table = ecs_get_table(world_ptr, fe.id());
	if (table == nullptr) {
		return false;
	}
	ecs_type_t const * type = ecs_table_get_type(table);
	if (type == nullptr || type->count <= 1) {
		return false;
	}
	if (type->count == 2 && ecs_has_id(world_ptr, fe.id(), flecs::Disabled)) {
		return false;
	}
	return true;
}

} // namespace EC
