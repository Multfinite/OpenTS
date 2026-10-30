/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

// Inline template definitions for TypedEntity<Root>, EntityView, and
// Single_Component_CRC<C>. Include this header in translation units that attach,
// query, remove, or CRC-check concrete component types.

#include "crc.h"
#include "ec/concepts.h"
#include "ec/entity.h"
#include "ec/game_components.h"
#include "ec/world.h"

#include <cassert>
#include <utility>

namespace EC {

namespace Detail {

template<typename C, EntityRoot Root>
void Invoke_On_Attach(C & comp, Root * owner, std::uint64_t id)
{
	if constexpr (HasOnAttachRoot<C, Root>) {
		if (owner != nullptr) {
			comp.On_Attach(*owner);
		}
	} else if constexpr (HasOnAttachView<C>) {
		comp.On_Attach(EntityView(id));
	} else if constexpr (HasOnAttachVoid<C>) {
		comp.On_Attach();
	}
	(void)comp;
	(void)owner;
	(void)id;
}

template<typename C, EntityRoot Root>
void Invoke_On_Remove(C & comp, Root * owner, std::uint64_t id)
{
	if constexpr (HasOnRemoveRoot<C, Root>) {
		if (owner != nullptr) {
			comp.On_Remove(*owner);
		}
	} else if constexpr (HasOnRemoveView<C>) {
		comp.On_Remove(EntityView(id));
	} else if constexpr (HasOnRemoveVoid<C>) {
		comp.On_Remove();
	}
	(void)comp;
	(void)owner;
	(void)id;
}

template<typename C>
void Invoke_On_Attach_Generic(C & comp, std::uint64_t id)
{
	if constexpr (HasOnAttachView<C>) {
		comp.On_Attach(EntityView(id));
	} else if constexpr (HasOnAttachVoid<C>) {
		comp.On_Attach();
	}
	(void)comp;
	(void)id;
}

template<typename C>
void Invoke_On_Remove_Generic(C & comp, std::uint64_t id)
{
	if constexpr (HasOnRemoveView<C>) {
		comp.On_Remove(EntityView(id));
	} else if constexpr (HasOnRemoveVoid<C>) {
		comp.On_Remove();
	}
	(void)comp;
	(void)id;
}

template<typename C>
void Feed_Component_CRC(C const & comp, flecs::entity fe, CRCEngine & crc)
{
	crc(static_cast<int>(Component_Type_Tag_32<C>()));
	if constexpr (HasComputeCRCPlain<C>) {
		comp.Compute_CRC(crc);
		(void)fe;
	} else {
		if constexpr (HasComputeCRCRoot<C, ObjectClass>) {
			if (auto const * link = fe.try_get<OwnerLink<ObjectClass>>(); link != nullptr && link->ptr != nullptr) {
				comp.Compute_CRC(crc, *link->ptr);
			}
		}
		if constexpr (HasComputeCRCRoot<C, CellClass>) {
			if (auto const * link = fe.try_get<OwnerLink<CellClass>>(); link != nullptr && link->ptr != nullptr) {
				comp.Compute_CRC(crc, *link->ptr);
			}
		}
		if constexpr (HasComputeCRCRoot<C, LocomotionClass>) {
			if (auto const * link = fe.try_get<OwnerLink<LocomotionClass>>(); link != nullptr && link->ptr != nullptr) {
				comp.Compute_CRC(crc, *link->ptr);
			}
		}
	}
}

} // namespace Detail

// Standalone individual component CRC helper (Level 1a).
template<HasComputeCRCPlain C>
int Single_Component_CRC(C const & comp, int seed = 0)
{
	CRCEngine crc(seed);
	crc(static_cast<int>(Component_Type_Tag_32<C>()));
	comp.Compute_CRC(crc);
	return crc();
}

template<typename C, EntityRoot Root>
requires HasComputeCRCRoot<C, Root>
int Single_Component_CRC(C const & comp, Root const & owner, int seed = 0)
{
	CRCEngine crc(seed);
	crc(static_cast<int>(Component_Type_Tag_32<C>()));
	comp.Compute_CRC(crc, owner);
	return crc();
}

// ---------------------------------------------------------------------------
// EntityView inline implementations
// ---------------------------------------------------------------------------

template<typename C, typename... Args>
C & EntityView::Emplace(Args &&... args)
{
	static_assert(IsRegisteredComponent<C>,
		"Component type C must be listed in EC::GameComponents.");
	assert(IdValue != 0);
	flecs::entity fe(World(), static_cast<flecs::entity_t>(IdValue));
	fe.emplace<C>(std::forward<Args>(args)...);
	C & ref = fe.ensure<C>();
	Detail::Invoke_On_Attach_Generic(ref, IdValue);
	return ref;
}

template<typename C>
C & EntityView::Add(C value)
{
	return Emplace<C>(std::move(value));
}

template<typename C>
C & EntityView::Get(void)
{
	static_assert(IsRegisteredComponent<C>,
		"Component type C must be listed in EC::GameComponents.");
	assert(IdValue != 0);
	flecs::entity fe(World(), static_cast<flecs::entity_t>(IdValue));
	C * ptr = fe.try_get_mut<C>();
	assert(ptr != nullptr);
	return *ptr;
}

template<typename C>
C const & EntityView::Get(void) const
{
	static_assert(IsRegisteredComponent<C>,
		"Component type C must be listed in EC::GameComponents.");
	assert(IdValue != 0);
	flecs::entity fe(World(), static_cast<flecs::entity_t>(IdValue));
	C const * ptr = fe.try_get<C>();
	assert(ptr != nullptr);
	return *ptr;
}

template<typename C>
C * EntityView::Try_Get(void) noexcept
{
	static_assert(IsRegisteredComponent<C>,
		"Component type C must be listed in EC::GameComponents.");
	if (IdValue == 0 || !Is_World_Alive()) {
		return nullptr;
	}
	flecs::entity fe(World(), static_cast<flecs::entity_t>(IdValue));
	return fe.is_alive() ? fe.try_get_mut<C>() : nullptr;
}

template<typename C>
C const * EntityView::Try_Get(void) const noexcept
{
	static_assert(IsRegisteredComponent<C>,
		"Component type C must be listed in EC::GameComponents.");
	if (IdValue == 0 || !Is_World_Alive()) {
		return nullptr;
	}
	flecs::entity fe(World(), static_cast<flecs::entity_t>(IdValue));
	return fe.is_alive() ? fe.try_get<C>() : nullptr;
}

template<typename C>
bool EntityView::Has(void) const noexcept
{
	static_assert(IsRegisteredComponent<C>,
		"Component type C must be listed in EC::GameComponents.");
	if (IdValue == 0 || !Is_World_Alive()) {
		return false;
	}
	flecs::entity fe(World(), static_cast<flecs::entity_t>(IdValue));
	return fe.is_alive() && fe.has<C>();
}

template<typename C>
void EntityView::Remove(void) noexcept
{
	static_assert(IsRegisteredComponent<C>,
		"Component type C must be listed in EC::GameComponents.");
	if (IdValue == 0 || !Is_World_Alive()) {
		return;
	}
	flecs::entity fe(World(), static_cast<flecs::entity_t>(IdValue));
	if (!fe.is_alive()) {
		return;
	}
	if (C * comp = fe.try_get_mut<C>(); comp != nullptr) {
		Detail::Invoke_On_Remove_Generic(*comp, IdValue);
		fe.remove<C>();
	}
}

// ---------------------------------------------------------------------------
// TypedEntity<Root> inline implementations
// ---------------------------------------------------------------------------

template<EntityRoot Root>
template<typename C, typename... Args>
C & TypedEntity<Root>::Emplace(Args &&... args)
{
	static_assert(IsRegisteredComponent<C>,
		"Component type C must be listed in EC::GameComponents.");
	static_assert(AllowedOnRoot<C, Root>,
		"Component type C is statically incompatible with this EntityRoot.");

	std::uint64_t const eid = Ensure_Materialized();
	flecs::entity fe(World(), static_cast<flecs::entity_t>(eid));
	fe.emplace<C>(std::forward<Args>(args)...);
	C & ref = fe.ensure<C>();
	Detail::Invoke_On_Attach(ref, OwnerPtr, eid);
	return ref;
}

template<EntityRoot Root>
template<typename C>
C & TypedEntity<Root>::Add(C value)
{
	return Emplace<C>(std::move(value));
}

template<EntityRoot Root>
template<typename C>
C & TypedEntity<Root>::Get(void)
{
	static_assert(IsRegisteredComponent<C>,
		"Component type C must be listed in EC::GameComponents.");
	static_assert(AllowedOnRoot<C, Root>,
		"Component type C is statically incompatible with this EntityRoot.");
	assert(IdValue != 0);
	flecs::entity fe(World(), static_cast<flecs::entity_t>(IdValue));
	C * ptr = fe.try_get_mut<C>();
	assert(ptr != nullptr);
	return *ptr;
}

template<EntityRoot Root>
template<typename C>
C const & TypedEntity<Root>::Get(void) const
{
	static_assert(IsRegisteredComponent<C>,
		"Component type C must be listed in EC::GameComponents.");
	static_assert(AllowedOnRoot<C, Root>,
		"Component type C is statically incompatible with this EntityRoot.");
	assert(IdValue != 0);
	flecs::entity fe(World(), static_cast<flecs::entity_t>(IdValue));
	C const * ptr = fe.try_get<C>();
	assert(ptr != nullptr);
	return *ptr;
}

template<EntityRoot Root>
template<typename C>
C * TypedEntity<Root>::Try_Get(void) noexcept
{
	static_assert(IsRegisteredComponent<C>,
		"Component type C must be listed in EC::GameComponents.");
	static_assert(AllowedOnRoot<C, Root>,
		"Component type C is statically incompatible with this EntityRoot.");
	if (IdValue == 0 || !Is_World_Alive()) {
		return nullptr;
	}
	flecs::entity fe(World(), static_cast<flecs::entity_t>(IdValue));
	return fe.is_alive() ? fe.try_get_mut<C>() : nullptr;
}

template<EntityRoot Root>
template<typename C>
C const * TypedEntity<Root>::Try_Get(void) const noexcept
{
	static_assert(IsRegisteredComponent<C>,
		"Component type C must be listed in EC::GameComponents.");
	static_assert(AllowedOnRoot<C, Root>,
		"Component type C is statically incompatible with this EntityRoot.");
	if (IdValue == 0 || !Is_World_Alive()) {
		return nullptr;
	}
	flecs::entity fe(World(), static_cast<flecs::entity_t>(IdValue));
	return fe.is_alive() ? fe.try_get<C>() : nullptr;
}

template<EntityRoot Root>
template<typename C>
bool TypedEntity<Root>::Has(void) const noexcept
{
	static_assert(IsRegisteredComponent<C>,
		"Component type C must be listed in EC::GameComponents.");
	static_assert(AllowedOnRoot<C, Root>,
		"Component type C is statically incompatible with this EntityRoot.");
	if (IdValue == 0 || !Is_World_Alive()) {
		return false;
	}
	flecs::entity fe(World(), static_cast<flecs::entity_t>(IdValue));
	return fe.is_alive() && fe.has<C>();
}

template<EntityRoot Root>
template<typename C>
void TypedEntity<Root>::Remove(void) noexcept
{
	static_assert(IsRegisteredComponent<C>,
		"Component type C must be listed in EC::GameComponents.");
	static_assert(AllowedOnRoot<C, Root>,
		"Component type C is statically incompatible with this EntityRoot.");
	if (IdValue == 0 || !Is_World_Alive()) {
		return;
	}
	flecs::entity fe(World(), static_cast<flecs::entity_t>(IdValue));
	if (!fe.is_alive()) {
		return;
	}
	if (C * comp = fe.try_get_mut<C>(); comp != nullptr) {
		Detail::Invoke_On_Remove(*comp, OwnerPtr, IdValue);
		fe.remove<C>();
	}
}

template<EntityRoot Root>
template<typename C>
bool TypedEntity<Root>::Compute_CRC_Of(CRCEngine & crc) const
{
	static_assert(IsRegisteredComponent<C>,
		"Component type C must be listed in EC::GameComponents.");
	static_assert(AllowedOnRoot<C, Root>,
		"Component type C is statically incompatible with this EntityRoot.");
	static_assert(HasComputeCRC<C>,
		"Component type C does not define Compute_CRC.");
	if (IdValue == 0 || !Is_World_Alive()) {
		return false;
	}
	flecs::entity fe(World(), static_cast<flecs::entity_t>(IdValue));
	if (!fe.is_alive()) {
		return false;
	}
	C const * comp = fe.try_get<C>();
	if (comp == nullptr) {
		return false;
	}
	Detail::Feed_Component_CRC(*comp, fe, crc);
	return true;
}

} // namespace EC
