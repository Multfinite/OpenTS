/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

// Lightweight, <flecs.h>-free entity handle header safe to include from
// object.h, cell.h, iloco.h, and loco.h without pulling third-party headers
// or OS macros into general engine translation units.
//
// Translation units that attach, query, or remove concrete component types
// include "ec/entity_inl.h" alongside the component definitions.

#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

class ObjectClass;
class CellClass;
class LocomotionClass;
class CRCEngine;

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

// Strongly typed back-link component stored on every materialized flecs entity,
// allowing dense table iteration to pass ObjectClass&, CellClass&, or
// LocomotionClass& statically with zero void* or dynamic_cast.
template<EntityRoot Root>
struct OwnerLink
{
	Root * ptr = nullptr;
};

namespace Detail {

std::uint64_t Materialize_Entity_Handle(RootKind kind, void * owner, bool disabled) noexcept;
void Destroy_Entity_Handle(std::uint64_t id) noexcept;
void Rebind_Entity_Owner(std::uint64_t id, RootKind kind, void * owner) noexcept;
void Set_Entity_Disabled_Flag(std::uint64_t id, bool disabled) noexcept;
bool Get_Entity_Disabled_Flag(std::uint64_t id) noexcept;

} // namespace Detail

// RAII move-only token for temporarily detaching an entity handle while
// CellClass slots are shifted or rebuilt in MapClass::Set_Map_Dimensions.
// Any handle not reclaimed via Release() is destroyed automatically on scope exit.
class DetachedEntitySlot
{
	public:
		constexpr DetachedEntitySlot(void) noexcept = default;
		constexpr explicit DetachedEntitySlot(std::uint64_t id) noexcept : IdValue(id) {}

		DetachedEntitySlot(DetachedEntitySlot const &) = delete;
		DetachedEntitySlot & operator=(DetachedEntitySlot const &) = delete;

		constexpr DetachedEntitySlot(DetachedEntitySlot && other) noexcept : IdValue(other.IdValue)
		{
			other.IdValue = 0;
		}

		DetachedEntitySlot & operator=(DetachedEntitySlot && other) noexcept
		{
			if (this != &other) {
				if (IdValue != 0) {
					Detail::Destroy_Entity_Handle(IdValue);
				}
				IdValue = other.IdValue;
				other.IdValue = 0;
			}
			return *this;
		}

		~DetachedEntitySlot(void) noexcept
		{
			if (IdValue != 0) {
				Detail::Destroy_Entity_Handle(IdValue);
				IdValue = 0;
			}
		}

		constexpr std::uint64_t Id(void) const noexcept { return IdValue; }

		constexpr std::uint64_t Release(void) noexcept
		{
			std::uint64_t const out = IdValue;
			IdValue = 0;
			return out;
		}

	private:
		std::uint64_t IdValue = 0;
};

// Non-owning typed view passed to generic component hooks (e.g. void AI(EntityView))
// so root-agnostic components can inspect sibling components on the same entity.
class EntityView
{
	public:
		constexpr EntityView(void) noexcept = default;
		constexpr explicit EntityView(std::uint64_t id) noexcept : IdValue(id) {}

		constexpr std::uint64_t Id(void) const noexcept { return IdValue; }
		constexpr bool Is_Valid(void) const noexcept { return IdValue != 0; }

		template<typename C, typename... Args>
		C & Emplace(Args &&... args);

		template<typename C>
		C & Add(C value = C{});

		template<typename C>
		C & Get(void);

		template<typename C>
		C const & Get(void) const;

		template<typename C>
		C * Try_Get(void) noexcept;

		template<typename C>
		C const * Try_Get(void) const noexcept;

		template<typename C>
		bool Has(void) const noexcept;

		template<typename C>
		void Remove(void) noexcept;

	private:
		std::uint64_t IdValue = 0;
};

// Component-holder embedded by value in ObjectClass, CellClass, and LocomotionClass.
// Lazily allocates an underlying flecs entity only when the first component is added,
// keeping the thousands of plain cells and objects completely free of ECS allocations.
template<EntityRoot Root>
class TypedEntity
{
	public:
		using RootType = Root;

		constexpr explicit TypedEntity(Root * owner = nullptr) noexcept :
			IdValue(0),
			OwnerPtr(owner),
			PendingDisabled(false)
		{
		}

		~TypedEntity(void) noexcept
		{
			Reset();
		}

		TypedEntity(TypedEntity const &) = delete;

		// Struct-level value copy (used by legacy cell-array shifts) intentionally
		// leaves both the destination's owner pointer and entity handle untouched;
		// explicit ownership transfer goes through Release_Id() / Adopt_Id().
		TypedEntity & operator=(TypedEntity const &) noexcept
		{
			return *this;
		}

		TypedEntity(TypedEntity &&) = delete;
		TypedEntity & operator=(TypedEntity &&) = delete;

		constexpr std::uint64_t Id(void) const noexcept { return IdValue; }
		constexpr bool Is_Materialized(void) const noexcept { return IdValue != 0; }
		constexpr Root * Owner(void) const noexcept { return OwnerPtr; }
		constexpr EntityView View(void) const noexcept { return EntityView(IdValue); }

		void Bind_Owner(Root * owner) noexcept
		{
			OwnerPtr = owner;
			if (IdValue != 0) {
				Detail::Rebind_Entity_Owner(IdValue, Root_Kind_Of<Root>(), OwnerPtr);
			}
		}

		void Reset(void) noexcept
		{
			if (IdValue != 0) {
				std::uint64_t const doomed = IdValue;
				IdValue = 0;
				Detail::Destroy_Entity_Handle(doomed);
			}
			PendingDisabled = false;
		}

		std::uint64_t Release_Id(void) noexcept
		{
			std::uint64_t const out = IdValue;
			IdValue = 0;
			PendingDisabled = false;
			return out;
		}

		void Adopt_Id(std::uint64_t id) noexcept
		{
			if (IdValue == id) {
				if (IdValue != 0) {
					Detail::Rebind_Entity_Owner(IdValue, Root_Kind_Of<Root>(), OwnerPtr);
				}
				return;
			}
			if (IdValue != 0) {
				Detail::Destroy_Entity_Handle(IdValue);
			}
			IdValue = id;
			if (IdValue != 0) {
				Detail::Rebind_Entity_Owner(IdValue, Root_Kind_Of<Root>(), OwnerPtr);
				PendingDisabled = Detail::Get_Entity_Disabled_Flag(IdValue);
			} else {
				PendingDisabled = false;
			}
		}

		void Set_Disabled(bool disabled) noexcept
		{
			PendingDisabled = disabled;
			if (IdValue != 0) {
				Detail::Set_Entity_Disabled_Flag(IdValue, disabled);
			}
		}

		bool Is_Disabled(void) const noexcept
		{
			if (IdValue != 0) {
				return Detail::Get_Entity_Disabled_Flag(IdValue);
			}
			return PendingDisabled;
		}

		std::uint64_t Ensure_Materialized(void) noexcept
		{
			if (IdValue == 0) {
				IdValue = Detail::Materialize_Entity_Handle(Root_Kind_Of<Root>(), OwnerPtr, PendingDisabled);
			}
			return IdValue;
		}

		template<typename C, typename... Args>
		C & Emplace(Args &&... args);

		template<typename C>
		C & Add(C value = C{});

		template<typename C>
		C & Get(void);

		template<typename C>
		C const & Get(void) const;

		template<typename C>
		C * Try_Get(void) noexcept;

		template<typename C>
		C const * Try_Get(void) const noexcept;

		template<typename C>
		bool Has(void) const noexcept;

		template<typename C>
		void Remove(void) noexcept;

		template<typename C>
		bool Compute_CRC_Of(CRCEngine & crc) const;

		void Compute_CRC(CRCEngine & crc) const;

	private:
		std::uint64_t IdValue = 0;
		Root * OwnerPtr = nullptr;
		bool PendingDisabled = false;
};

} // namespace EC
