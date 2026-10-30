/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

// Compile-time recursive ComponentRegistry<Ts...> over native flecs storage:
// - Native flecs component registration with .on_add / .on_remove observers
// - Persistent cached flecs::query<> passes stored on flecs::world
// - Direct flecs::entity event dispatchers (zero lazy-creation machinery)
// - Two-subpass EC01 .SAV trailer (queries only serializable component tables)
// - 4-level deterministic Compute_CRC over flecs::entity handles

#include "crc.h"
#include "ec/concepts.h"
#include "ec/entity.h"
#include "ec/fnv1a.h"
#include "ec/game_components.h"
#include "ec/world.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace EC {

inline constexpr std::uint32_t EC_TRAILER_MAGIC = 0x31304345u; // 'EC01' little-endian
inline constexpr std::uint32_t EC_TRAILER_VERSION = 1u;

// ---------------------------------------------------------------------------
// Standalone Per-Component CRC Helpers (Level 1a & Level 1b)
// ---------------------------------------------------------------------------

template<HasComputeCRCPlain C>
int Single_Component_CRC(C const & comp, int seed = 0)
{
	CRCEngine crc(seed);
	crc(static_cast<int>(Component_Type_Tag_32<C>()));
	comp.Compute_CRC(crc);
	return crc();
}

template<typename C, typename OwnerT>
requires (
	(EntityRoot<OwnerT> && HasComputeCRCRoot<C, OwnerT>) ||
	(HasDerivedObjectOwner<C> && std::same_as<OwnerT, typename C::OwnerType> && HasComputeCRCDerivedOwner<C>)
)
int Single_Component_CRC(C const & comp, OwnerT const & owner, int seed = 0)
{
	CRCEngine crc(seed);
	crc(static_cast<int>(Component_Type_Tag_32<C>()));
	comp.Compute_CRC(crc, owner);
	return crc();
}

namespace Detail {

struct SavedEntityManifest
{
	flecs::entity_t FlecsId = 0;
	std::uint8_t Root = 0;
	std::uint8_t Flags = 0; // bit 0: flecs::Disabled
	std::uint32_t OwnerSwizzleId = 0;
	std::vector<std::uint64_t> TypeIds;
};

struct LoadedEntityManifest
{
	flecs::entity Entity;
	std::vector<std::uint64_t> TypeIds;
};

struct ComponentCrcRow
{
	StableOwnerKey Key{};
	int SingleCrc = 0;
};

inline char const * Root_Short_Label(std::uint8_t kind) noexcept
{
	switch (static_cast<RootKind>(kind)) {
		case RootKind::Object: return "Obj";
		case RootKind::Cell: return "Cell";
		case RootKind::Locomotor: return "Loco";
	}
	return "Unk";
}

template<typename OwnerKeyFn>
inline bool Resolve_Entity_Owner_Key(flecs::entity fe, OwnerKeyFn & owner_key_fn, StableOwnerKey & out_key)
{
	if (auto const * obj = fe.try_get<OwnerLink<ObjectClass>>(); obj != nullptr && obj->ptr != nullptr) {
		out_key = owner_key_fn(RootKind::Object, obj->ptr);
		return true;
	}
	if (auto const * cell = fe.try_get<OwnerLink<CellClass>>(); cell != nullptr && cell->ptr != nullptr) {
		out_key = owner_key_fn(RootKind::Cell, cell->ptr);
		return true;
	}
	if (auto const * loco = fe.try_get<OwnerLink<LocomotionClass>>(); loco != nullptr && loco->ptr != nullptr) {
		out_key = owner_key_fn(RootKind::Locomotor, loco->ptr);
		return true;
	}
	return false;
}

inline bool Resolve_Entity_Root_Ptr(flecs::entity fe, RootKind & out_kind, void const *& out_ptr) noexcept
{
	if (auto const * obj = fe.try_get<OwnerLink<ObjectClass>>(); obj != nullptr && obj->ptr != nullptr) {
		out_kind = RootKind::Object;
		out_ptr = obj->ptr;
		return true;
	}
	if (auto const * cell = fe.try_get<OwnerLink<CellClass>>(); cell != nullptr && cell->ptr != nullptr) {
		out_kind = RootKind::Cell;
		out_ptr = cell->ptr;
		return true;
	}
	if (auto const * loco = fe.try_get<OwnerLink<LocomotionClass>>(); loco != nullptr && loco->ptr != nullptr) {
		out_kind = RootKind::Locomotor;
		out_ptr = loco->ptr;
		return true;
	}
	return false;
}

template<typename C>
inline void Assert_Component_Root_Affinity(flecs::entity fe) noexcept
{
#ifndef NDEBUG
	if (fe.has<OwnerLink<ObjectClass>>()) {
		assert((AllowedOnRoot<C, ObjectClass>));
	} else if (fe.has<OwnerLink<CellClass>>()) {
		assert((AllowedOnRoot<C, CellClass>));
	} else if (fe.has<OwnerLink<LocomotionClass>>()) {
		assert((AllowedOnRoot<C, LocomotionClass>));
	}
#else
	(void)fe;
#endif
}

template<typename C>
inline void Invoke_Native_On_Add(flecs::entity fe, C & comp)
{
	Assert_Component_Root_Affinity<C>(fe);

	if constexpr (OnAddDerivedOwner<C>) {
		if (auto const * link = fe.try_get<OwnerLink<ObjectClass>>(); link != nullptr && link->ptr != nullptr) {
			comp.On_Add(static_cast<typename C::OwnerType &>(*link->ptr));
		}
	} else if constexpr (OnAddRoot<C, ObjectClass>) {
		if (auto const * link = fe.try_get<OwnerLink<ObjectClass>>(); link != nullptr && link->ptr != nullptr) {
			comp.On_Add(*link->ptr);
		}
	} else if constexpr (OnAddRoot<C, CellClass>) {
		if (auto const * link = fe.try_get<OwnerLink<CellClass>>(); link != nullptr && link->ptr != nullptr) {
			comp.On_Add(*link->ptr);
		}
	} else if constexpr (OnAddRoot<C, LocomotionClass>) {
		if (auto const * link = fe.try_get<OwnerLink<LocomotionClass>>(); link != nullptr && link->ptr != nullptr) {
			comp.On_Add(*link->ptr);
		}
	} else if constexpr (OnAddEntity<C>) {
		comp.On_Add(fe);
	} else if constexpr (OnAddVoid<C>) {
		comp.On_Add();
	} else if constexpr (OnAttachDerivedOwner<C>) {
		if (auto const * link = fe.try_get<OwnerLink<ObjectClass>>(); link != nullptr && link->ptr != nullptr) {
			comp.On_Attach(static_cast<typename C::OwnerType &>(*link->ptr));
		}
	} else if constexpr (OnAttachRoot<C, ObjectClass>) {
		if (auto const * link = fe.try_get<OwnerLink<ObjectClass>>(); link != nullptr && link->ptr != nullptr) {
			comp.On_Attach(*link->ptr);
		}
	} else if constexpr (OnAttachRoot<C, CellClass>) {
		if (auto const * link = fe.try_get<OwnerLink<CellClass>>(); link != nullptr && link->ptr != nullptr) {
			comp.On_Attach(*link->ptr);
		}
	} else if constexpr (OnAttachRoot<C, LocomotionClass>) {
		if (auto const * link = fe.try_get<OwnerLink<LocomotionClass>>(); link != nullptr && link->ptr != nullptr) {
			comp.On_Attach(*link->ptr);
		}
	} else if constexpr (OnAttachEntity<C>) {
		comp.On_Attach(fe);
	} else if constexpr (OnAttachVoid<C>) {
		comp.On_Attach();
	}
	(void)comp;
}

template<typename C>
inline void Invoke_Native_On_Remove(flecs::entity fe, C & comp)
{
	if constexpr (OnRemoveDerivedOwner<C>) {
		if (auto const * link = fe.try_get<OwnerLink<ObjectClass>>(); link != nullptr && link->ptr != nullptr) {
			comp.On_Remove(static_cast<typename C::OwnerType &>(*link->ptr));
		}
	} else if constexpr (OnRemoveRoot<C, ObjectClass>) {
		if (auto const * link = fe.try_get<OwnerLink<ObjectClass>>(); link != nullptr && link->ptr != nullptr) {
			comp.On_Remove(*link->ptr);
		}
	} else if constexpr (OnRemoveRoot<C, CellClass>) {
		if (auto const * link = fe.try_get<OwnerLink<CellClass>>(); link != nullptr && link->ptr != nullptr) {
			comp.On_Remove(*link->ptr);
		}
	} else if constexpr (OnRemoveRoot<C, LocomotionClass>) {
		if (auto const * link = fe.try_get<OwnerLink<LocomotionClass>>(); link != nullptr && link->ptr != nullptr) {
			comp.On_Remove(*link->ptr);
		}
	} else if constexpr (OnRemoveEntity<C>) {
		comp.On_Remove(fe);
	} else if constexpr (OnRemoveVoid<C>) {
		comp.On_Remove();
	}
	(void)comp;
}

template<typename C>
inline void Feed_Component_CRC(C const & comp, flecs::entity fe, CRCEngine & crc)
{
	crc(static_cast<int>(Component_Type_Tag_32<C>()));
	if constexpr (HasComputeCRCPlain<C>) {
		comp.Compute_CRC(crc);
		(void)fe;
	} else if constexpr (HasComputeCRCEntity<C>) {
		comp.Compute_CRC(crc, fe);
	} else {
		if constexpr (HasComputeCRCRoot<C, ObjectClass>) {
			if (auto const * link = fe.try_get<OwnerLink<ObjectClass>>(); link != nullptr && link->ptr != nullptr) {
				comp.Compute_CRC(crc, *link->ptr);
			}
		} else if constexpr (HasComputeCRCDerivedOwner<C>) {
			if (auto const * link = fe.try_get<OwnerLink<ObjectClass>>(); link != nullptr && link->ptr != nullptr) {
				comp.Compute_CRC(crc, static_cast<typename C::OwnerType const &>(*link->ptr));
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

// Persistent cached flecs::query<> stored directly as a singleton on flecs::world
// so query cache lifetime matches the world with zero global state.
template<typename PassTag, typename... QueryTerms>
struct WorldCachedQuery
{
	flecs::query<QueryTerms...> Query;
};

template<typename PassTag, ecs_flags32_t ExtraFlags, typename... QueryTerms>
inline flecs::query<QueryTerms...> const & Get_Cached_Query(flecs::world & world)
{
	using SlotT = WorldCachedQuery<PassTag, QueryTerms...>;
	SlotT & slot = world.ensure<SlotT>();
	if (!slot.Query) {
		slot.Query = world.query_builder<QueryTerms...>().cached().query_flags(ExtraFlags).build();
	}
	return slot.Query;
}

struct PassTagAI {};
struct PassTagPostAI {};
struct PassTagEnvAI {};
struct PassTagFrameBegin {};
struct PassTagFrameEnd {};
struct PassTagPreRender {};
struct PassTagPostRender {};
struct PassTagScenarioInit {};
struct PassTagPostLoad {};

} // namespace Detail

template<HasComputeCRC C>
bool Compute_Component_CRC(flecs::entity fe, CRCEngine & crc)
{
	if (fe.id() == 0 || !fe.is_alive()) {
		return false;
	}
	C const * comp = fe.try_get<C>();
	if (comp == nullptr) {
		return false;
	}
	Detail::Feed_Component_CRC(*comp, fe, crc);
	return true;
}

// ===========================================================================
// Base Case: ComponentRegistry<> (Empty Pack)
// ===========================================================================

template<typename... Ts>
struct ComponentRegistry;

template<>
struct ComponentRegistry<>
{
	static constexpr std::size_t count = 0;

	template<typename C>
	static constexpr bool contains = false;

	static constexpr bool has_save_id(std::uint64_t) noexcept { return false; }

	static void Register_All(flecs::world &) noexcept {}

	static void AI_All(flecs::world &) noexcept {}
	static void PostAI_All(flecs::world &) noexcept {}
	static void Environment_AI_All(flecs::world &) noexcept {}
	static void Frame_Begin_All(flecs::world &) noexcept {}
	static void Frame_End_All(flecs::world &) noexcept {}
	static void Pre_Render_All(flecs::world &) noexcept {}
	static void Post_Render_All(flecs::world &) noexcept {}
	static void Scenario_Init_All(flecs::world &) noexcept {}
	static void Scenario_Clear_All(void) noexcept {}
	static void Post_Load_All(flecs::world &) noexcept {}

	template<typename StreamT>
	static constexpr std::uint32_t Count_Serializable_Types(void) noexcept { return 0; }
	template<typename StreamT>
	static void Write_Type_Directory(StreamT &) noexcept {}
	template<typename StreamT, typename RecordFn>
	static void Collect_Serializable_Entities(flecs::world &, RecordFn &&) noexcept {}
	template<typename StreamT>
	static bool Write_Component_By_Save_Id(flecs::entity, std::uint64_t, StreamT &) noexcept { return false; }
	template<typename StreamT>
	static bool Ensure_Component_By_Save_Id(flecs::entity, std::uint64_t) noexcept { return false; }
	template<typename StreamT>
	static bool Read_Component_By_Save_Id(flecs::entity, std::uint64_t, StreamT &) noexcept { return false; }

	template<typename StreamT, typename IdOfFn>
	static bool Save_Trailer(flecs::world & world, StreamT & stream, IdOfFn && id_of)
	{
		(void)world;
		(void)id_of;
		return !stream.Was_Error();
	}

	template<typename StreamT, typename ResolveOwnerFn>
	static bool Load_Trailer(flecs::world & world, StreamT & stream, ResolveOwnerFn && resolve_owner)
	{
		(void)world;
		(void)resolve_owner;
		if (stream.Offset() >= stream.Size()) {
			return true;
		}
		std::uint32_t magic = 0;
		std::uint32_t version = 0;
		std::uint32_t payload_bytes = 0;
		stream.Serialize(magic);
		stream.Serialize(version);
		stream.Serialize(payload_bytes);
		if (stream.Was_Error() || magic != EC_TRAILER_MAGIC || version != EC_TRAILER_VERSION) {
			stream.Fail();
			return false;
		}
		if (!stream.Fits(static_cast<int>(payload_bytes), 1)) {
			return false;
		}
		std::vector<unsigned char> discard(payload_bytes);
		if (payload_bytes > 0) {
			stream.Serialize_Bytes(discard.data(), static_cast<int>(payload_bytes));
		}
		return !stream.Was_Error();
	}

	static void Compute_Flecs_Entity_CRC(flecs::entity, CRCEngine &) noexcept {}
	static void Compute_Entity_CRC(flecs::entity, CRCEngine &) noexcept {}

	template<typename C>
	static bool Compute_Entity_Component_CRC(flecs::entity, CRCEngine &) noexcept { return false; }

	template<typename OwnerKeyFn>
	static void Compute_CRC_All(flecs::world &, CRCEngine &, OwnerKeyFn &&) noexcept {}

	template<EntityRoot Root, typename OwnerKeyFn>
	static void Compute_Root_CRC_All(flecs::world &, CRCEngine &, OwnerKeyFn &&) noexcept {}

	template<typename OwnerKeyFn>
	static void Print_Component_CRCs(flecs::world &, FILE *, OwnerKeyFn &&) noexcept {}

	template<typename OwnerT>
	static void Dispatch_Detach(OwnerT &, AbstractClass const *, bool) noexcept {}
	template<typename OwnerT>
	static void Dispatch_Limbo(OwnerT &) noexcept {}
	template<typename OwnerT, typename... Args>
	static void Dispatch_Unlimbo(OwnerT &, Args &&...) noexcept {}
	template<typename OwnerT>
	static void Dispatch_Delete_Me(OwnerT &) noexcept {}
	template<typename OwnerT, typename... Args>
	static void Dispatch_Take_Damage(OwnerT &, Args &&...) noexcept {}
	template<typename OwnerT, typename... Args>
	static void Dispatch_Can_Fire(OwnerT const &, Args &&...) noexcept {}
	template<typename OwnerT, typename... Args>
	static void Dispatch_Fire_At(OwnerT &, Args &&...) noexcept {}
	template<typename OwnerT, typename... Args>
	static void Dispatch_Record_The_Kill(OwnerT &, Args &&...) noexcept {}
	template<typename OwnerT, typename... Args>
	static void Dispatch_Captured(OwnerT &, Args &&...) noexcept {}
	template<typename OwnerT, typename... Args>
	static void Dispatch_Assign_Target(OwnerT &, Args &&...) noexcept {}
	template<typename OwnerT, typename... Args>
	static void Dispatch_Assign_Destination(OwnerT &, Args &&...) noexcept {}
	template<typename OwnerT, typename... Args>
	static void Dispatch_Set_Coord(OwnerT &, Args &&...) noexcept {}
	template<typename OwnerT, typename... Args>
	static void Dispatch_Per_Cell_Process(OwnerT &, Args &&...) noexcept {}
	template<typename OwnerT, typename... Args>
	static void Dispatch_Receive_Message(OwnerT &, Args &&...) noexcept {}
	template<typename OwnerT>
	static void Dispatch_Do_Cloak(OwnerT &) noexcept {}
	template<typename OwnerT>
	static void Dispatch_Do_Uncloak(OwnerT &) noexcept {}
	template<typename OwnerT>
	static void Dispatch_Stun(OwnerT &) noexcept {}
	template<typename OwnerT>
	static void Dispatch_Select(OwnerT &) noexcept {}
	template<typename OwnerT>
	static void Dispatch_Unselect(OwnerT &) noexcept {}
	template<typename OwnerT>
	static void Dispatch_Hidden(OwnerT &) noexcept {}
	template<typename OwnerT, typename... Args>
	static void Dispatch_Revealed(OwnerT &, Args &&...) noexcept {}
	template<typename OwnerT, typename... Args>
	static void Dispatch_Grand_Opening(OwnerT &, Args &&...) noexcept {}
	template<typename OwnerT, typename... Args>
	static void Dispatch_Draw_Pre_Render(OwnerT const &, Args &&...) noexcept {}
	template<typename OwnerT, typename... Args>
	static void Dispatch_Draw_It(OwnerT const &, Args &&...) noexcept {}
	template<typename OwnerT, typename... Args>
	static void Dispatch_Draw_Post_Render(OwnerT const &, Args &&...) noexcept {}

	template<typename OwnerT, typename... Args>
	static void Dispatch_Occupy_Down(OwnerT &, Args &&...) noexcept {}
	template<typename OwnerT, typename... Args>
	static void Dispatch_Occupy_Up(OwnerT &, Args &&...) noexcept {}
	template<typename OwnerT>
	static void Dispatch_Recalc_Passability(OwnerT &) noexcept {}
	template<typename OwnerT>
	static void Dispatch_Recalc_Attributes(OwnerT &) noexcept {}
	template<typename OwnerT>
	static void Dispatch_Grow_Tiberium(OwnerT &) noexcept {}
	template<typename OwnerT, typename... Args>
	static void Dispatch_Spread_Tiberium(OwnerT &, Args &&...) noexcept {}
	template<typename OwnerT, typename... Args>
	static void Dispatch_Reduce_Tiberium(OwnerT &, Args &&...) noexcept {}
	template<typename OwnerT, typename... Args>
	static void Dispatch_Goodie_Check(OwnerT &, Args &&...) noexcept {}
	template<typename OwnerT, typename... Args>
	static void Dispatch_Cell_Draw_It(OwnerT const &, Args &&...) noexcept {}
	template<typename OwnerT, typename... Args>
	static void Dispatch_Draw_Overlay(OwnerT &, Args &&...) noexcept {}

	template<typename OwnerT, typename... Args>
	static void Dispatch_Link_To_Object(OwnerT &, Args &&...) noexcept {}
	template<typename OwnerT>
	static void Dispatch_Loco_Process(OwnerT &) noexcept {}
	template<typename OwnerT, typename... Args>
	static void Dispatch_Loco_PostProcess(OwnerT &, Args &&...) noexcept {}
	template<typename OwnerT, typename... Args>
	static void Dispatch_Move_To(OwnerT &, Args &&...) noexcept {}
	template<typename OwnerT>
	static void Dispatch_Stop_Moving(OwnerT &) noexcept {}
	template<typename OwnerT>
	static void Dispatch_Power_On(OwnerT &) noexcept {}
	template<typename OwnerT>
	static void Dispatch_Power_Off(OwnerT &) noexcept {}
	template<typename OwnerT, typename CarriedIface>
	static void Dispatch_Begin_Piggyback(OwnerT &, CarriedIface * carried) noexcept
	{
		if (carried != nullptr) {
			flecs::entity ce = Entity_Of(*carried);
			if (ce.id() != 0) {
				ce.add(flecs::Disabled);
			}
		}
	}
	template<typename OwnerT, typename CarriedIface>
	static void Dispatch_End_Piggyback(OwnerT & carrier, CarriedIface * restored) noexcept
	{
		if (restored != nullptr) {
			flecs::entity ce = Entity_Of(*restored);
			flecs::entity carrier_e = Entity_Of(carrier);
			if (ce.id() != 0) {
				if (carrier_e.id() != 0 && carrier_e.has(flecs::Disabled)) {
					ce.add(flecs::Disabled);
				} else {
					ce.remove(flecs::Disabled);
				}
			}
		}
	}
	template<typename OwnerT, typename... Args>
	static void Dispatch_Draw_Matrix(OwnerT &, Args &&...) noexcept {}
};

// ===========================================================================
// Recursive Case: ComponentRegistry<Head, Tail...>
// ===========================================================================

template<typename Head, typename... Tail>
struct ComponentRegistry<Head, Tail...>
{
	using Next = ComponentRegistry<Tail...>;

	static constexpr std::size_t count = 1 + Next::count;

	template<typename C>
	static constexpr bool contains = std::same_as<C, Head> || Next::template contains<C>;

	static_assert(!Next::template contains<Head>,
		"Duplicate component type registered in EC::ComponentRegistry.");

	static constexpr bool has_save_id(std::uint64_t id) noexcept
	{
		if constexpr (HasSaveName<Head>) {
			if (Component_Save_Id<Head>() == id) {
				return true;
			}
		}
		return Next::has_save_id(id);
	}

	static_assert(!HasSaveName<Head> || !Next::has_save_id(Component_Save_Id<Head>()),
		"FNV-1a SaveName collision across registered components in EC::ComponentRegistry.");

	// Native flecs component registration + .on_add / .on_remove observers
	static void Register_All(flecs::world & world)
	{
		auto comp = world.component<Head>();

#ifndef NDEBUG
		constexpr bool needs_add_hook = true;
#else
		constexpr bool needs_add_hook = HasOnAdd<Head> || HasOnAttach<Head>;
#endif
		if constexpr (needs_add_hook) {
			comp.on_add([](flecs::entity fe, Head & c) {
				Detail::Invoke_Native_On_Add<Head>(fe, c);
			});
		}
		if constexpr (HasOnRemove<Head>) {
			comp.on_remove([](flecs::entity fe, Head & c) {
				Detail::Invoke_Native_On_Remove<Head>(fe, c);
			});
		}

		Next::Register_All(world);
	}

	// -----------------------------------------------------------------------
	// Global Frame, Simulation, Render, and Lifecycle Passes (G01..G08, G11)
	// Backed by persistent cached flecs::query<> instances on flecs::world.
	// -----------------------------------------------------------------------

#define OPENTS_EC_IMPL_MUT_GLOBAL_PASS(PassName, TagType, ConceptPrefix, MethodName, ExtraQueryFlags) \
	static void PassName(flecs::world & world) \
	{ \
		if constexpr (ConceptPrefix##Void<Head>) { \
			Detail::Get_Cached_Query<TagType, ExtraQueryFlags, Head>(world).each( \
				[](Head & comp) { comp.MethodName(); }); \
		} else if constexpr (ConceptPrefix##Entity<Head>) { \
			Detail::Get_Cached_Query<TagType, ExtraQueryFlags, Head>(world).each( \
				[](flecs::entity fe, Head & comp) { comp.MethodName(fe); }); \
		} else if constexpr (ConceptPrefix##DerivedOwner<Head>) { \
			using SubOwnerT = typename Head::OwnerType; \
			Detail::Get_Cached_Query<TagType, ExtraQueryFlags, OwnerLink<ObjectClass> const, Head>(world).each( \
				[](OwnerLink<ObjectClass> const & link, Head & comp) { \
					if (link.ptr != nullptr) comp.MethodName(static_cast<SubOwnerT &>(*link.ptr)); \
				}); \
		} else { \
			if constexpr (ConceptPrefix##Root<Head, ObjectClass>) { \
				Detail::Get_Cached_Query<TagType, ExtraQueryFlags, OwnerLink<ObjectClass> const, Head>(world).each( \
					[](OwnerLink<ObjectClass> const & link, Head & comp) { \
						if (link.ptr != nullptr) comp.MethodName(*link.ptr); \
					}); \
			} \
			if constexpr (ConceptPrefix##Root<Head, CellClass>) { \
				Detail::Get_Cached_Query<TagType, ExtraQueryFlags, OwnerLink<CellClass> const, Head>(world).each( \
					[](OwnerLink<CellClass> const & link, Head & comp) { \
						if (link.ptr != nullptr) comp.MethodName(*link.ptr); \
					}); \
			} \
			if constexpr (ConceptPrefix##Root<Head, LocomotionClass>) { \
				Detail::Get_Cached_Query<TagType, ExtraQueryFlags, OwnerLink<LocomotionClass> const, Head>(world).each( \
					[](OwnerLink<LocomotionClass> const & link, Head & comp) { \
						if (link.ptr != nullptr) comp.MethodName(*link.ptr); \
					}); \
			} \
		} \
		Next::PassName(world); \
	}

#define OPENTS_EC_IMPL_CONST_GLOBAL_PASS(PassName, TagType, ConceptPrefix, MethodName) \
	static void PassName(flecs::world & world) \
	{ \
		if constexpr (ConceptPrefix##Void<Head>) { \
			Detail::Get_Cached_Query<TagType, 0, Head const>(world).each( \
				[](Head const & comp) { comp.MethodName(); }); \
		} else if constexpr (ConceptPrefix##Entity<Head>) { \
			Detail::Get_Cached_Query<TagType, 0, Head const>(world).each( \
				[](flecs::entity fe, Head const & comp) { comp.MethodName(fe); }); \
		} else if constexpr (ConceptPrefix##DerivedOwner<Head>) { \
			using SubOwnerT = typename Head::OwnerType; \
			Detail::Get_Cached_Query<TagType, 0, OwnerLink<ObjectClass> const, Head const>(world).each( \
				[](OwnerLink<ObjectClass> const & link, Head const & comp) { \
					if (link.ptr != nullptr) comp.MethodName(static_cast<SubOwnerT const &>(*link.ptr)); \
				}); \
		} else { \
			if constexpr (ConceptPrefix##Root<Head, ObjectClass>) { \
				Detail::Get_Cached_Query<TagType, 0, OwnerLink<ObjectClass> const, Head const>(world).each( \
					[](OwnerLink<ObjectClass> const & link, Head const & comp) { \
						if (link.ptr != nullptr) comp.MethodName(*link.ptr); \
					}); \
			} \
			if constexpr (ConceptPrefix##Root<Head, CellClass>) { \
				Detail::Get_Cached_Query<TagType, 0, OwnerLink<CellClass> const, Head const>(world).each( \
					[](OwnerLink<CellClass> const & link, Head const & comp) { \
						if (link.ptr != nullptr) comp.MethodName(*link.ptr); \
					}); \
			} \
			if constexpr (ConceptPrefix##Root<Head, LocomotionClass>) { \
				Detail::Get_Cached_Query<TagType, 0, OwnerLink<LocomotionClass> const, Head const>(world).each( \
					[](OwnerLink<LocomotionClass> const & link, Head const & comp) { \
						if (link.ptr != nullptr) comp.MethodName(*link.ptr); \
					}); \
			} \
		} \
		Next::PassName(world); \
	}

	OPENTS_EC_IMPL_MUT_GLOBAL_PASS(AI_All, Detail::PassTagAI, AI, AI, 0)                                     // G01
	OPENTS_EC_IMPL_MUT_GLOBAL_PASS(PostAI_All, Detail::PassTagPostAI, PostAI, PostAI, 0)                     // G02
	OPENTS_EC_IMPL_MUT_GLOBAL_PASS(Environment_AI_All, Detail::PassTagEnvAI, EnvironmentAI, Environment_AI, 0)// G03
	OPENTS_EC_IMPL_MUT_GLOBAL_PASS(Frame_Begin_All, Detail::PassTagFrameBegin, FrameBegin, Frame_Begin, 0)   // G04
	OPENTS_EC_IMPL_MUT_GLOBAL_PASS(Frame_End_All, Detail::PassTagFrameEnd, FrameEnd, Frame_End, 0)           // G05
	OPENTS_EC_IMPL_CONST_GLOBAL_PASS(Pre_Render_All, Detail::PassTagPreRender, PreRender, Pre_Render)        // G06
	OPENTS_EC_IMPL_CONST_GLOBAL_PASS(Post_Render_All, Detail::PassTagPostRender, PostRender, Post_Render)    // G07
	OPENTS_EC_IMPL_MUT_GLOBAL_PASS(Scenario_Init_All, Detail::PassTagScenarioInit, ScenarioInit, Scenario_Init, 0) // G08
	// Post_Load runs across both enabled and disabled entities (e.g. units in Limbo / Piggybacker)
	OPENTS_EC_IMPL_MUT_GLOBAL_PASS(Post_Load_All, Detail::PassTagPostLoad, PostLoad, Post_Load, EcsQueryMatchDisabled) // G11

#undef OPENTS_EC_IMPL_MUT_GLOBAL_PASS
#undef OPENTS_EC_IMPL_CONST_GLOBAL_PASS

	// G09: Scenario_Clear
	static void Scenario_Clear_All(void)
	{
		if constexpr (HasScenarioClear<Head>) {
			Head::Scenario_Clear();
		}
		Next::Scenario_Clear_All();
	}

	// -----------------------------------------------------------------------
	// G10: Two-Subpass EC01 .SAV Trailer (Queries Serializable Tables Only!)
	// -----------------------------------------------------------------------

	template<typename StreamT>
	static constexpr std::uint32_t Count_Serializable_Types(void) noexcept
	{
		return (HasSerialize<Head, StreamT> ? 1u : 0u) + Next::template Count_Serializable_Types<StreamT>();
	}

	template<typename StreamT>
	static void Write_Type_Directory(StreamT & stream)
	{
		if constexpr (HasSerialize<Head, StreamT>) {
			std::uint64_t type_id = Component_Save_Id<Head>();
			std::string_view const name = std::string_view(Head::SaveName);
			std::uint16_t name_len = static_cast<std::uint16_t>(name.size());
			stream.Serialize(type_id);
			stream.Serialize(name_len);
			if (name_len > 0) {
				stream.Serialize_Bytes(const_cast<char *>(name.data()), static_cast<int>(name_len));
			}
		}
		Next::template Write_Type_Directory<StreamT>(stream);
	}

	template<typename StreamT, typename RecordFn>
	static void Collect_Serializable_Entities(flecs::world & world, RecordFn && record_fn)
	{
		if constexpr (HasSerialize<Head, StreamT>) {
			constexpr std::uint64_t save_id = Component_Save_Id<Head>();
			world.query_builder<Head const>()
				.query_flags(EcsQueryMatchDisabled)
				.each([&](flecs::entity fe, Head const &) {
					record_fn(fe, save_id);
				});
		}
		Next::template Collect_Serializable_Entities<StreamT>(world, std::forward<RecordFn>(record_fn));
	}

	template<typename StreamT>
	static bool Write_Component_By_Save_Id(flecs::entity fe, std::uint64_t save_id, StreamT & stream)
	{
		if constexpr (HasSerialize<Head, StreamT>) {
			if (Component_Save_Id<Head>() == save_id) {
				if (Head * comp = fe.try_get_mut<Head>(); comp != nullptr) {
					comp->Serialize(stream);
					return true;
				}
				return false;
			}
		}
		return Next::template Write_Component_By_Save_Id<StreamT>(fe, save_id, stream);
	}

	template<typename StreamT>
	static bool Ensure_Component_By_Save_Id(flecs::entity fe, std::uint64_t save_id)
	{
		if constexpr (HasSerialize<Head, StreamT>) {
			if (Component_Save_Id<Head>() == save_id) {
				(void)fe.ensure<Head>();
				return true;
			}
		}
		return Next::template Ensure_Component_By_Save_Id<StreamT>(fe, save_id);
	}

	template<typename StreamT>
	static bool Read_Component_By_Save_Id(flecs::entity fe, std::uint64_t save_id, StreamT & stream)
	{
		if constexpr (HasSerialize<Head, StreamT>) {
			if (Component_Save_Id<Head>() == save_id) {
				if (Head * comp = fe.try_get_mut<Head>(); comp != nullptr) {
					comp->Serialize(stream);
					return true;
				}
				return false;
			}
		}
		return Next::template Read_Component_By_Save_Id<StreamT>(fe, save_id, stream);
	}

	template<typename StreamT, typename IdOfFn>
	static bool Save_Trailer(flecs::world & world, StreamT & stream, IdOfFn && id_of)
	{
		std::vector<Detail::SavedEntityManifest> records;

		auto find_or_add = [&](flecs::entity fe) -> Detail::SavedEntityManifest * {
			flecs::entity_t const fid = fe.id();
			for (Detail::SavedEntityManifest & existing : records) {
				if (existing.FlecsId == fid) {
					return &existing;
				}
			}
			RootKind kind{};
			void const * owner_ptr = nullptr;
			if (!Detail::Resolve_Entity_Root_Ptr(fe, kind, owner_ptr)) {
				return nullptr;
			}
			std::uint32_t const swizzle_id = static_cast<std::uint32_t>(id_of(kind, owner_ptr));
			if (swizzle_id == 0) {
				return nullptr;
			}
			Detail::SavedEntityManifest rec;
			rec.FlecsId = fid;
			rec.Root = static_cast<std::uint8_t>(kind);
			rec.Flags = fe.has(flecs::Disabled) ? 1u : 0u;
			rec.OwnerSwizzleId = swizzle_id;
			records.push_back(std::move(rec));
			return &records.back();
		};

		// Visit only entities carrying at least one serializable component (never scans plain cells/objects).
		Collect_Serializable_Entities<StreamT>(world, [&](flecs::entity fe, std::uint64_t save_id) {
			if (Detail::SavedEntityManifest * rec = find_or_add(fe); rec != nullptr) {
				rec->TypeIds.push_back(save_id);
			}
		});

		if (records.empty()) {
			return !stream.Was_Error();
		}

		std::sort(records.begin(), records.end(), [](auto const & a, auto const & b) {
			return std::tie(a.Root, a.OwnerSwizzleId) < std::tie(b.Root, b.OwnerSwizzleId);
		});

		std::uint32_t magic = EC_TRAILER_MAGIC;
		std::uint32_t version = EC_TRAILER_VERSION;
		stream.Serialize(magic);
		stream.Serialize(version);

		unsigned int const payload_len_pos = stream.Offset();
		std::uint32_t payload_bytes = 0;
		stream.Serialize(payload_bytes);
		unsigned int const payload_start = stream.Offset();

		// Type directory
		std::uint32_t type_count = Count_Serializable_Types<StreamT>();
		stream.Serialize(type_count);
		Write_Type_Directory<StreamT>(stream);

		// Subpass 1: Entity manifest
		std::uint32_t entity_count = static_cast<std::uint32_t>(records.size());
		stream.Serialize(entity_count);
		for (Detail::SavedEntityManifest const & rec : records) {
			std::uint8_t root = rec.Root;
			std::uint8_t flags = rec.Flags;
			std::uint16_t comp_count = static_cast<std::uint16_t>(rec.TypeIds.size());
			std::uint32_t owner_id = rec.OwnerSwizzleId;
			stream.Serialize(root);
			stream.Serialize(flags);
			stream.Serialize(comp_count);
			stream.Serialize(owner_id);
			for (std::uint64_t tid : rec.TypeIds) {
				stream.Serialize(tid);
			}
		}

		// Subpass 2: Component payloads (each framed with u32 byte length)
		for (Detail::SavedEntityManifest const & rec : records) {
			flecs::entity fe(world, rec.FlecsId);
			for (std::uint64_t const tid : rec.TypeIds) {
				unsigned int const blob_len_pos = stream.Offset();
				std::uint32_t blob_bytes = 0;
				stream.Serialize(blob_bytes);
				unsigned int const blob_start = stream.Offset();
				(void)Write_Component_By_Save_Id<StreamT>(fe, tid, stream);
				blob_bytes = stream.Offset() - blob_start;
				stream.Overwrite_Bytes(blob_len_pos, &blob_bytes, sizeof(blob_bytes));
			}
		}

		payload_bytes = stream.Offset() - payload_start;
		stream.Overwrite_Bytes(payload_len_pos, &payload_bytes, sizeof(payload_bytes));
		return !stream.Was_Error();
	}

	template<typename StreamT, typename ResolveOwnerEntityFn>
	static bool Load_Trailer(flecs::world & world, StreamT & stream, ResolveOwnerEntityFn && resolve_owner_entity)
	{
		(void)world;
		if (stream.Offset() >= stream.Size()) {
			return true; // Backward compatibility: saves written before EC01 have no trailer.
		}

		std::uint32_t magic = 0;
		std::uint32_t version = 0;
		std::uint32_t payload_bytes = 0;
		stream.Serialize(magic);
		stream.Serialize(version);
		stream.Serialize(payload_bytes);
		if (stream.Was_Error() || magic != EC_TRAILER_MAGIC || version != EC_TRAILER_VERSION) {
			stream.Fail();
			return false;
		}
		if (!stream.Fits(static_cast<int>(payload_bytes), 1)) {
			return false;
		}

		unsigned int const trailer_end = stream.Offset() + payload_bytes;
		typename StreamT::BoundScope trailer_scope(stream, trailer_end);

		std::uint32_t type_count = 0;
		stream.Serialize(type_count);
		if (!stream.Fits(static_cast<int>(type_count), 10)) {
			return false;
		}
		for (std::uint32_t i = 0; i < type_count; ++i) {
			std::uint64_t tid = 0;
			std::uint16_t name_len = 0;
			stream.Serialize(tid);
			stream.Serialize(name_len);
			if (!stream.Fits(static_cast<int>(name_len), 1)) {
				return false;
			}
			if (name_len > 0) {
				std::vector<char> skip_name(name_len);
				stream.Serialize_Bytes(skip_name.data(), static_cast<int>(name_len));
			}
		}

		// Subpass 1: Attach all saved component types onto the already-constructed
		// owners' flecs::entity handles first, pinning every archetype table before
		// Subpass 2 registers any interior component pointer with Swizzler!
		std::uint32_t entity_count = 0;
		stream.Serialize(entity_count);
		if (!stream.Fits(static_cast<int>(entity_count), 8)) {
			return false;
		}

		std::vector<Detail::LoadedEntityManifest> loaded;
		loaded.reserve(entity_count);

		for (std::uint32_t i = 0; i < entity_count; ++i) {
			std::uint8_t root = 0;
			std::uint8_t flags = 0;
			std::uint16_t comp_count = 0;
			std::uint32_t owner_id = 0;
			stream.Serialize(root);
			stream.Serialize(flags);
			stream.Serialize(comp_count);
			stream.Serialize(owner_id);
			if (stream.Was_Error() || root > static_cast<std::uint8_t>(RootKind::Locomotor)) {
				stream.Fail();
				return false;
			}
			if (!stream.Fits(static_cast<int>(comp_count), sizeof(std::uint64_t))) {
				return false;
			}

			Detail::LoadedEntityManifest item;
			item.TypeIds.resize(comp_count);
			for (std::uint16_t k = 0; k < comp_count; ++k) {
				stream.Serialize(item.TypeIds[k]);
			}
			if (stream.Was_Error()) {
				return false;
			}

			item.Entity = resolve_owner_entity(static_cast<RootKind>(root), owner_id);
			if (item.Entity.id() != 0 && item.Entity.is_alive()) {
				if ((flags & 1u) != 0u) {
					item.Entity.add(flecs::Disabled);
				} else {
					item.Entity.remove(flecs::Disabled);
				}
				for (std::uint64_t const tid : item.TypeIds) {
					(void)Ensure_Component_By_Save_Id<StreamT>(item.Entity, tid);
				}
			} else {
				item.Entity = flecs::entity::null();
			}
			loaded.push_back(std::move(item));
		}

		// Subpass 2: Populate already-pinned component memory slots.
		for (Detail::LoadedEntityManifest const & item : loaded) {
			for (std::uint64_t const tid : item.TypeIds) {
				std::uint32_t blob_bytes = 0;
				stream.Serialize(blob_bytes);
				if (stream.Was_Error() || !stream.Fits(static_cast<int>(blob_bytes), 1)) {
					return false;
				}
				unsigned int const blob_end = stream.Offset() + blob_bytes;
				typename StreamT::BoundScope blob_scope(stream, blob_end);
				bool consumed = false;
				if (item.Entity.id() != 0) {
					consumed = Read_Component_By_Save_Id<StreamT>(item.Entity, tid, stream);
				}
				if (!consumed && stream.Offset() < blob_end) {
					unsigned int const rem = blob_end - stream.Offset();
					std::vector<unsigned char> skip(rem);
					stream.Serialize_Bytes(skip.data(), static_cast<int>(rem));
				}
				if (stream.Was_Error() || stream.Offset() != blob_end) {
					stream.Fail();
					return false;
				}
			}
		}

		return !stream.Was_Error();
	}

	// -----------------------------------------------------------------------
	// G12 / E27: 4-Level Per-Component & Global Compute_CRC over flecs::entity
	// -----------------------------------------------------------------------

	static void Compute_Flecs_Entity_CRC(flecs::entity fe, CRCEngine & crc)
	{
		if constexpr (HasComputeCRC<Head>) {
			if (Head const * comp = fe.try_get<Head>(); comp != nullptr) {
				Detail::Feed_Component_CRC(*comp, fe, crc);
			}
		}
		Next::Compute_Flecs_Entity_CRC(fe, crc);
	}

	static void Compute_Entity_CRC(flecs::entity fe, CRCEngine & crc)
	{
		if (!Has_User_Components(fe)) {
			return;
		}
		Compute_Flecs_Entity_CRC(fe, crc);
	}

	template<typename C>
	static bool Compute_Entity_Component_CRC(flecs::entity fe, CRCEngine & crc)
	{
		static_assert(contains<C>, "Component type C is not registered in this ComponentRegistry.");
		if constexpr (HasComputeCRC<C>) {
			if (fe.id() != 0 && fe.is_alive()) {
				if (C const * comp = fe.try_get<C>(); comp != nullptr) {
					Detail::Feed_Component_CRC(*comp, fe, crc);
					return true;
				}
			}
		}
		return false;
	}

	template<typename OwnerKeyFn>
	static void Collect_Head_CRC_Rows(flecs::world & world, OwnerKeyFn & owner_key_fn, std::vector<Detail::ComponentCrcRow> & rows)
	{
		if constexpr (HasComputeCRC<Head>) {
			world.query_builder<Head const>()
				.query_flags(EcsQueryMatchDisabled)
				.each([&](flecs::entity fe, Head const & comp) {
					StableOwnerKey key{};
					if (!Detail::Resolve_Entity_Owner_Key(fe, owner_key_fn, key)) {
						return;
					}
					CRCEngine single;
					Detail::Feed_Component_CRC(comp, fe, single);
					rows.push_back(Detail::ComponentCrcRow{key, single()});
				});
			std::sort(rows.begin(), rows.end(), [](auto const & a, auto const & b) {
				return a.Key < b.Key;
			});
		}
	}

	template<typename OwnerKeyFn>
	static void Compute_CRC_All(flecs::world & world, CRCEngine & crc, OwnerKeyFn && owner_key_fn)
	{
		if constexpr (HasComputeCRC<Head>) {
			std::vector<Detail::ComponentCrcRow> rows;
			Collect_Head_CRC_Rows(world, owner_key_fn, rows);
			if (!rows.empty()) {
				crc(static_cast<int>(Component_Type_Tag_32<Head>()));
				crc(static_cast<int>(rows.size()));
				for (Detail::ComponentCrcRow const & row : rows) {
					crc(static_cast<int>(row.Key.Root));
					crc(row.Key.PrimaryId);
					crc(row.Key.SubSlot);
					crc(row.SingleCrc);
				}
			}
		}
		Next::Compute_CRC_All(world, crc, std::forward<OwnerKeyFn>(owner_key_fn));
	}

	template<EntityRoot Root, typename OwnerKeyFn>
	static void Compute_Root_CRC_All(flecs::world & world, CRCEngine & crc, OwnerKeyFn && owner_key_fn)
	{
		if constexpr (HasComputeCRC<Head> && AllowedOnRoot<Head, Root>) {
			std::vector<Detail::ComponentCrcRow> rows;
			world.query_builder<OwnerLink<Root> const, Head const>()
				.query_flags(EcsQueryMatchDisabled)
				.each([&](flecs::entity fe, OwnerLink<Root> const & link, Head const & comp) {
					if (link.ptr == nullptr) {
						return;
					}
					CRCEngine single;
					Detail::Feed_Component_CRC(comp, fe, single);
					rows.push_back(Detail::ComponentCrcRow{owner_key_fn(Root_Kind_Of<Root>(), link.ptr), single()});
				});
			if (!rows.empty()) {
				std::sort(rows.begin(), rows.end(), [](auto const & a, auto const & b) {
					return a.Key < b.Key;
				});
				crc(static_cast<int>(Component_Type_Tag_32<Head>()));
				crc(static_cast<int>(rows.size()));
				for (Detail::ComponentCrcRow const & row : rows) {
					crc(row.Key.PrimaryId);
					crc(row.Key.SubSlot);
					crc(row.SingleCrc);
				}
			}
		}
		Next::template Compute_Root_CRC_All<Root>(world, crc, std::forward<OwnerKeyFn>(owner_key_fn));
	}

	template<typename OwnerKeyFn>
	static void Print_Component_CRCs(flecs::world & world, FILE * fp, OwnerKeyFn && owner_key_fn)
	{
		if (fp == nullptr) {
			return;
		}
		if constexpr (HasComputeCRC<Head>) {
			std::vector<Detail::ComponentCrcRow> rows;
			Collect_Head_CRC_Rows(world, owner_key_fn, rows);
			if (!rows.empty()) {
				CRCEngine table_crc;
				table_crc(static_cast<int>(Component_Type_Tag_32<Head>()));
				for (Detail::ComponentCrcRow const & row : rows) {
					table_crc(static_cast<int>(row.Key.Root));
					table_crc(row.Key.PrimaryId);
					table_crc(row.Key.SubSlot);
					table_crc(row.SingleCrc);
				}
				std::string const name(Component_Display_Name<Head>());
				std::fprintf(
					fp,
					"Component[%s] = %08X (Count: %zu)\n",
					name.c_str(),
					static_cast<unsigned int>(table_crc()),
					rows.size());
				for (std::size_t i = 0; i < rows.size(); ++i) {
					Detail::ComponentCrcRow const & row = rows[i];
					if (row.Key.SubSlot != 0) {
						std::fprintf(
							fp,
							"  [%zu] Owner:%s:%d:%d CRC = %08X\n",
							i,
							Detail::Root_Short_Label(row.Key.Root),
							row.Key.PrimaryId,
							row.Key.SubSlot,
							static_cast<unsigned int>(row.SingleCrc));
					} else {
						std::fprintf(
							fp,
							"  [%zu] Owner:%s:%d CRC = %08X\n",
							i,
							Detail::Root_Short_Label(row.Key.Root),
							row.Key.PrimaryId,
							static_cast<unsigned int>(row.SingleCrc));
					}
				}
			}
		}
		Next::Print_Component_CRCs(world, fp, std::forward<OwnerKeyFn>(owner_key_fn));
	}

	// -----------------------------------------------------------------------
	// Entity-Scoped Dispatchers (E02..E26) Directly Over flecs::entity
	// -----------------------------------------------------------------------

	template<typename OwnerT>
	static void Dispatch_Detach(OwnerT & owner, AbstractClass const * target, bool all)
	{
		flecs::entity fe = Entity_Of(owner);
		if (!Has_User_Components(fe)) {
			return;
		}
		if constexpr (
			(std::derived_from<OwnerT, ObjectClass> && HasDetachRoot<Head, ObjectClass>) ||
			(std::same_as<OwnerT, CellClass> && HasDetachRoot<Head, CellClass>) ||
			(std::derived_from<OwnerT, LocomotionClass> && HasDetachRoot<Head, LocomotionClass>) ||
			HasDetachPlain<Head>
		) {
			if (Head * comp = fe.try_get_mut<Head>(); comp != nullptr) {
				if constexpr (std::derived_from<OwnerT, ObjectClass> && HasDetachRoot<Head, ObjectClass>) {
					comp->Detach(owner, target, all);
				} else if constexpr (std::same_as<OwnerT, CellClass> && HasDetachRoot<Head, CellClass>) {
					comp->Detach(owner, target, all);
				} else if constexpr (std::derived_from<OwnerT, LocomotionClass> && HasDetachRoot<Head, LocomotionClass>) {
					comp->Detach(owner, target, all);
				} else {
					comp->Detach(target, all);
				}
			}
		}
		Next::Dispatch_Detach(owner, target, all);
	}

#define OPENTS_EC_IMPL_MUT_ENTITY_DISPATCH(FuncName, ConceptName, MethodName) \
	template<typename OwnerT, typename... Args> \
	static void FuncName(OwnerT & owner, Args &&... args) \
	{ \
		flecs::entity fe = Entity_Of(owner); \
		if (!Has_User_Components(fe)) { \
			return; \
		} \
		if constexpr (ConceptName##WithSelf<Head, OwnerT, Args...> || ConceptName##NoSelf<Head, Args...>) { \
			if (Head * comp = fe.try_get_mut<Head>(); comp != nullptr) { \
				if constexpr (ConceptName##WithSelf<Head, OwnerT, Args...>) { \
					comp->MethodName(owner, args...); \
				} else { \
					comp->MethodName(args...); \
				} \
			} \
		} \
		Next::FuncName(owner, std::forward<Args>(args)...); \
	}

#define OPENTS_EC_IMPL_CONST_ENTITY_DISPATCH(FuncName, ConceptName, MethodName) \
	template<typename OwnerT, typename... Args> \
	static void FuncName(OwnerT const & owner, Args &&... args) \
	{ \
		flecs::entity fe = Entity_Of(owner); \
		if (!Has_User_Components(fe)) { \
			return; \
		} \
		if constexpr (ConceptName##WithSelf<Head, OwnerT, Args...> || ConceptName##NoSelf<Head, Args...>) { \
			if (Head const * comp = fe.try_get<Head>(); comp != nullptr) { \
				if constexpr (ConceptName##WithSelf<Head, OwnerT, Args...>) { \
					comp->MethodName(owner, args...); \
				} else { \
					comp->MethodName(args...); \
				} \
			} \
		} \
		Next::FuncName(owner, std::forward<Args>(args)...); \
	}

	OPENTS_EC_IMPL_MUT_ENTITY_DISPATCH(Dispatch_Limbo, HasLimbo, Limbo)                             // E03a
	OPENTS_EC_IMPL_MUT_ENTITY_DISPATCH(Dispatch_Unlimbo, HasUnlimbo, Unlimbo)                       // E03b
	OPENTS_EC_IMPL_MUT_ENTITY_DISPATCH(Dispatch_Delete_Me, HasDeleteMe, Delete_Me)                  // E04
	OPENTS_EC_IMPL_MUT_ENTITY_DISPATCH(Dispatch_Take_Damage, HasTakeDamage, Take_Damage)            // E05
	OPENTS_EC_IMPL_CONST_ENTITY_DISPATCH(Dispatch_Can_Fire, HasCanFire, Can_Fire)                   // E06a
	OPENTS_EC_IMPL_MUT_ENTITY_DISPATCH(Dispatch_Fire_At, HasFireAt, Fire_At)                        // E06b
	OPENTS_EC_IMPL_MUT_ENTITY_DISPATCH(Dispatch_Record_The_Kill, HasRecordTheKill, Record_The_Kill) // E07

	// E08: Captured supports both (self, oldowner, newowner) and 1-arg (self, newowner)
	template<typename OwnerT, typename HouseT>
	static void Dispatch_Captured(OwnerT & owner, HouseT * oldowner, HouseT * newowner)
	{
		flecs::entity fe = Entity_Of(owner);
		if (!Has_User_Components(fe)) {
			return;
		}
		if constexpr (
			HasCapturedWithSelf<Head, OwnerT, HouseT *, HouseT *> ||
			HasCapturedNoSelf<Head, HouseT *, HouseT *> ||
			HasCapturedWithSelf<Head, OwnerT, HouseT *> ||
			HasCapturedNoSelf<Head, HouseT *>
		) {
			if (Head * comp = fe.try_get_mut<Head>(); comp != nullptr) {
				if constexpr (HasCapturedWithSelf<Head, OwnerT, HouseT *, HouseT *>) {
					comp->Captured(owner, oldowner, newowner);
				} else if constexpr (HasCapturedNoSelf<Head, HouseT *, HouseT *>) {
					comp->Captured(oldowner, newowner);
				} else if constexpr (HasCapturedWithSelf<Head, OwnerT, HouseT *>) {
					comp->Captured(owner, newowner);
				} else {
					comp->Captured(newowner);
				}
			}
		}
		Next::Dispatch_Captured(owner, oldowner, newowner);
	}

	template<typename OwnerT, typename HouseT>
	static void Dispatch_Captured(OwnerT & owner, HouseT * newowner)
	{
		Dispatch_Captured(owner, static_cast<HouseT *>(nullptr), newowner);
	}

	OPENTS_EC_IMPL_MUT_ENTITY_DISPATCH(Dispatch_Assign_Target, HasAssignTarget, Assign_Target)      // E09a
	OPENTS_EC_IMPL_MUT_ENTITY_DISPATCH(Dispatch_Assign_Destination, HasAssignDestination, Assign_Destination) // E09b
	OPENTS_EC_IMPL_MUT_ENTITY_DISPATCH(Dispatch_Set_Coord, HasSetCoord, Set_Coord)                  // E10
	OPENTS_EC_IMPL_MUT_ENTITY_DISPATCH(Dispatch_Per_Cell_Process, HasPerCellProcess, Per_Cell_Process) // E11
	OPENTS_EC_IMPL_MUT_ENTITY_DISPATCH(Dispatch_Receive_Message, HasReceiveMessage, Receive_Message)// E12
	OPENTS_EC_IMPL_MUT_ENTITY_DISPATCH(Dispatch_Do_Cloak, HasDoCloak, Do_Cloak)                     // E13a
	OPENTS_EC_IMPL_MUT_ENTITY_DISPATCH(Dispatch_Do_Uncloak, HasDoUncloak, Do_Uncloak)               // E13b
	OPENTS_EC_IMPL_MUT_ENTITY_DISPATCH(Dispatch_Stun, HasStun, Stun)                                // E13c
	OPENTS_EC_IMPL_MUT_ENTITY_DISPATCH(Dispatch_Select, HasSelect, Select)                          // E14a
	OPENTS_EC_IMPL_MUT_ENTITY_DISPATCH(Dispatch_Unselect, HasUnselect, Unselect)                    // E14b
	OPENTS_EC_IMPL_MUT_ENTITY_DISPATCH(Dispatch_Hidden, HasHidden, Hidden)                          // E15a
	OPENTS_EC_IMPL_MUT_ENTITY_DISPATCH(Dispatch_Revealed, HasRevealed, Revealed)                    // E15b
	OPENTS_EC_IMPL_MUT_ENTITY_DISPATCH(Dispatch_Grand_Opening, HasGrandOpening, Grand_Opening)      // E15c
	OPENTS_EC_IMPL_CONST_ENTITY_DISPATCH(Dispatch_Draw_Pre_Render, HasDrawPreRender, Draw_Pre_Render) // E16a
	OPENTS_EC_IMPL_CONST_ENTITY_DISPATCH(Dispatch_Draw_It, HasDrawIt, Draw_It)                      // E16b
	OPENTS_EC_IMPL_CONST_ENTITY_DISPATCH(Dispatch_Draw_Post_Render, HasDrawPostRender, Draw_Post_Render) // E16c

	OPENTS_EC_IMPL_MUT_ENTITY_DISPATCH(Dispatch_Occupy_Down, HasOccupyDown, Occupy_Down)            // E17a
	OPENTS_EC_IMPL_MUT_ENTITY_DISPATCH(Dispatch_Occupy_Up, HasOccupyUp, Occupy_Up)                  // E17b
	OPENTS_EC_IMPL_MUT_ENTITY_DISPATCH(Dispatch_Recalc_Passability, HasRecalcPassability, Recalc_Passability) // E18a
	OPENTS_EC_IMPL_MUT_ENTITY_DISPATCH(Dispatch_Recalc_Attributes, HasRecalcAttributes, Recalc_Attributes)   // E18b
	OPENTS_EC_IMPL_MUT_ENTITY_DISPATCH(Dispatch_Grow_Tiberium, HasGrowTiberium, Grow_Tiberium)      // E19a
	OPENTS_EC_IMPL_MUT_ENTITY_DISPATCH(Dispatch_Spread_Tiberium, HasSpreadTiberium, Spread_Tiberium)// E19b
	OPENTS_EC_IMPL_MUT_ENTITY_DISPATCH(Dispatch_Reduce_Tiberium, HasReduceTiberium, Reduce_Tiberium)// E19c
	OPENTS_EC_IMPL_MUT_ENTITY_DISPATCH(Dispatch_Goodie_Check, HasGoodieCheck, Goodie_Check)         // E20
	OPENTS_EC_IMPL_CONST_ENTITY_DISPATCH(Dispatch_Cell_Draw_It, HasCellDrawIt, Cell_Draw_It)        // E21a
	OPENTS_EC_IMPL_MUT_ENTITY_DISPATCH(Dispatch_Draw_Overlay, HasDrawOverlay, Draw_Overlay)         // E21b

	OPENTS_EC_IMPL_MUT_ENTITY_DISPATCH(Dispatch_Link_To_Object, HasLinkToObject, Link_To_Object)    // E22
	OPENTS_EC_IMPL_MUT_ENTITY_DISPATCH(Dispatch_Loco_Process, HasLocoProcess, Loco_Process)         // E23a
	OPENTS_EC_IMPL_MUT_ENTITY_DISPATCH(Dispatch_Loco_PostProcess, HasLocoPostProcess, Loco_PostProcess) // E23b
	OPENTS_EC_IMPL_MUT_ENTITY_DISPATCH(Dispatch_Move_To, HasMoveTo, Move_To)                        // E24a
	OPENTS_EC_IMPL_MUT_ENTITY_DISPATCH(Dispatch_Stop_Moving, HasStopMoving, Stop_Moving)            // E24b
	OPENTS_EC_IMPL_MUT_ENTITY_DISPATCH(Dispatch_Power_On, HasPowerOn, Power_On)                     // E25a
	OPENTS_EC_IMPL_MUT_ENTITY_DISPATCH(Dispatch_Power_Off, HasPowerOff, Power_Off)                  // E25b
	OPENTS_EC_IMPL_MUT_ENTITY_DISPATCH(Dispatch_Draw_Matrix, HasDrawMatrix, Draw_Matrix)            // E26c

#undef OPENTS_EC_IMPL_MUT_ENTITY_DISPATCH
#undef OPENTS_EC_IMPL_CONST_ENTITY_DISPATCH

	// E26a/E26b: Piggyback start/end toggles flecs::Disabled on the carried locomotor.
	template<typename OwnerT, typename CarriedIface>
	static void Dispatch_Begin_Piggyback(OwnerT & carrier, CarriedIface * carried)
	{
		flecs::entity fe = Entity_Of(carrier);
		if (Has_User_Components(fe)) {
			if constexpr (HasBeginPiggybackWithSelf<Head, OwnerT, CarriedIface *> || HasBeginPiggybackNoSelf<Head, CarriedIface *>) {
				if (Head * comp = fe.try_get_mut<Head>(); comp != nullptr) {
					if constexpr (HasBeginPiggybackWithSelf<Head, OwnerT, CarriedIface *>) {
						comp->Begin_Piggyback(carrier, carried);
					} else {
						comp->Begin_Piggyback(carried);
					}
				}
			}
		}
		Next::Dispatch_Begin_Piggyback(carrier, carried);
	}

	template<typename OwnerT, typename CarriedIface>
	static void Dispatch_End_Piggyback(OwnerT & carrier, CarriedIface * restored)
	{
		flecs::entity fe = Entity_Of(carrier);
		if (Has_User_Components(fe)) {
			if constexpr (HasEndPiggybackWithSelf<Head, OwnerT, CarriedIface *> || HasEndPiggybackNoSelf<Head, CarriedIface *>) {
				if (Head * comp = fe.try_get_mut<Head>(); comp != nullptr) {
					if constexpr (HasEndPiggybackWithSelf<Head, OwnerT, CarriedIface *>) {
						comp->End_Piggyback(carrier, restored);
					} else {
						comp->End_Piggyback(restored);
					}
				}
			}
		}
		Next::Dispatch_End_Piggyback(carrier, restored);
	}
};

} // namespace EC
