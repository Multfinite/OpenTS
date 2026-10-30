/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

// Recursive C++20 ComponentRegistry<Ts...> generating zero-overhead static
// dispatch across dense flecs tables for all 12 global hooks (G01..G12) and
// 27 entity/component hooks (E01..E27), plus the two-subpass EC01 .SAV trailer
// and 4-level Compute_CRC audit.

#include "crc.h"
#include "ec/concepts.h"
#include "ec/entity.h"
#include "ec/entity_inl.h"
#include "ec/fnv1a.h"
#include "ec/game_components.h"
#include "ec/world.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace EC {

inline constexpr std::uint32_t EC_TRAILER_MAGIC = 0x31304345u; // 'EC01'
inline constexpr std::uint32_t EC_TRAILER_VERSION = 1u;

namespace Detail {

template<typename OwnerT>
decltype(auto) Holder_Of(OwnerT & owner) noexcept
{
	if constexpr (requires { owner.Components(); }) {
		return owner.Components();
	} else {
		return (owner.Components);
	}
}

inline char const * Root_Short_Label(std::uint8_t root) noexcept
{
	switch (static_cast<RootKind>(root)) {
		case RootKind::Object: return "Obj";
		case RootKind::Cell: return "Cell";
		case RootKind::Locomotor: return "Loco";
	}
	return "Unk";
}

struct SavedEntityManifest
{
	std::uint64_t FlecsId = 0;
	std::uint8_t Root = 0;
	std::uint8_t Flags = 0;
	std::uint32_t OwnerSwizzleId = 0;
	std::vector<std::uint64_t> TypeIds;
};

struct LoadedEntityManifest
{
	std::uint64_t FlecsId = 0;
	std::vector<std::uint64_t> TypeIds;
};

struct ComponentCrcRow
{
	StableOwnerKey Key;
	int SingleCrc = 0;
};

template<typename OwnerKeyFn>
bool Resolve_Entity_Owner_Key(flecs::entity fe, OwnerKeyFn && owner_key_fn, StableOwnerKey & out_key)
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

template<typename IdOfFn>
bool Resolve_Entity_Save_Owner(flecs::entity fe, IdOfFn && id_of, std::uint8_t & out_root, std::uint32_t & out_swizzle)
{
	if (auto const * obj = fe.try_get<OwnerLink<ObjectClass>>(); obj != nullptr && obj->ptr != nullptr) {
		out_root = static_cast<std::uint8_t>(RootKind::Object);
		out_swizzle = static_cast<std::uint32_t>(id_of(RootKind::Object, obj->ptr));
		return out_swizzle != 0;
	}
	if (auto const * cell = fe.try_get<OwnerLink<CellClass>>(); cell != nullptr && cell->ptr != nullptr) {
		out_root = static_cast<std::uint8_t>(RootKind::Cell);
		out_swizzle = static_cast<std::uint32_t>(id_of(RootKind::Cell, cell->ptr));
		return out_swizzle != 0;
	}
	if (auto const * loco = fe.try_get<OwnerLink<LocomotionClass>>(); loco != nullptr && loco->ptr != nullptr) {
		out_root = static_cast<std::uint8_t>(RootKind::Locomotor);
		out_swizzle = static_cast<std::uint32_t>(id_of(RootKind::Locomotor, loco->ptr));
		return out_swizzle != 0;
	}
	return false;
}

template<typename ObjT = ObjectClass, typename CellT = CellClass, typename LocoT = LocomotionClass>
inline std::uint64_t Materialize_Loaded_Owner(RootKind kind, void * owner_ptr, bool disabled) noexcept
{
	if (owner_ptr == nullptr) {
		return 0;
	}
	switch (kind) {
		case RootKind::Object: {
			auto & holder = Holder_Of(*static_cast<ObjT *>(owner_ptr));
			holder.Set_Disabled(disabled);
			return holder.Ensure_Materialized();
		}
		case RootKind::Cell: {
			auto & holder = Holder_Of(*static_cast<CellT *>(owner_ptr));
			holder.Set_Disabled(disabled);
			return holder.Ensure_Materialized();
		}
		case RootKind::Locomotor: {
			auto & holder = Holder_Of(*static_cast<LocoT *>(owner_ptr));
			holder.Set_Disabled(disabled);
			return holder.Ensure_Materialized();
		}
	}
	return 0;
}

} // namespace Detail

// ===========================================================================
// Base Case: ComponentRegistry<> (Zero Registered Components)
// ===========================================================================

template<>
struct ComponentRegistry<>
{
	static constexpr std::size_t count = 0;

	template<typename>
	static constexpr bool contains = false;

	template<std::uint64_t>
	static constexpr bool contains_save_id = false;

	static void Register_All(flecs::world &) noexcept {}

	// Global hooks G01..G09, G11
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

	// Save helpers
	template<typename StreamT>
	static constexpr std::uint32_t Count_Serializable_Types(void) noexcept { return 0; }

	template<typename StreamT>
	static void Write_Type_Directory(StreamT &) {}

	template<typename StreamT>
	static void Append_Entity_Save_Types(flecs::entity, std::vector<std::uint64_t> &) {}

	template<typename StreamT>
	static bool Write_Component_By_Save_Id(flecs::entity, std::uint64_t, StreamT &) { return false; }

	template<typename StreamT>
	static bool Ensure_Component_By_Save_Id(flecs::entity, std::uint64_t) { return false; }

	template<typename StreamT>
	static bool Read_Component_By_Save_Id(flecs::entity, std::uint64_t, StreamT &) { return false; }

	template<typename StreamT, typename IdOfFn>
	static bool Save_Trailer(flecs::world &, StreamT & stream, IdOfFn &&)
	{
		return !stream.Was_Error();
	}

	template<typename StreamT, typename ResolveOwnerFn>
	static bool Load_Trailer(flecs::world &, StreamT & stream, ResolveOwnerFn &&)
	{
		if (stream.Offset() >= stream.Size()) {
			return !stream.Was_Error();
		}
		// Consume an EC01 trailer gracefully even if this build defines 0 components.
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
		typename StreamT::BoundScope scope(stream, stream.Offset() + payload_bytes);
		std::vector<unsigned char> skip(payload_bytes);
		if (payload_bytes > 0) {
			stream.Serialize_Bytes(skip.data(), static_cast<int>(payload_bytes));
		}
		return !stream.Was_Error();
	}

	// CRC hooks G12 / E27
	template<typename C, EntityRoot Root>
	static bool Compute_Entity_Component_CRC(TypedEntity<Root> const & holder, CRCEngine & crc)
	{
		return holder.template Compute_CRC_Of<C>(crc);
	}

	static void Compute_Flecs_Entity_CRC(flecs::entity, CRCEngine &) noexcept {}

	template<EntityRoot Root>
	static void Compute_Entity_CRC(TypedEntity<Root> const &, CRCEngine &) noexcept {}

	template<typename OwnerKeyFn>
	static void Compute_CRC_All(flecs::world &, CRCEngine &, OwnerKeyFn &&) noexcept {}

	template<EntityRoot Root, typename OwnerKeyFn>
	static void Compute_Root_CRC_All(flecs::world &, CRCEngine &, OwnerKeyFn &&) noexcept {}

	template<typename OwnerKeyFn>
	static void Print_Component_CRCs(flecs::world &, FILE *, OwnerKeyFn &&) noexcept {}

	// Entity hooks E02..E26
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
			Detail::Holder_Of(*carried).Set_Disabled(true);
		}
	}
	template<typename OwnerT, typename CarriedIface>
	static void Dispatch_End_Piggyback(OwnerT & carrier, CarriedIface * restored) noexcept
	{
		if (restored != nullptr) {
			bool const carrier_disabled = Detail::Holder_Of(carrier).Is_Disabled();
			Detail::Holder_Of(*restored).Set_Disabled(carrier_disabled);
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

	static_assert(!Next::template contains<Head>,
		"Duplicate component type registered in EC::ComponentRegistry.");
	static_assert(Component_Save_Id<Head>() == 0ULL || !Next::template contains_save_id<Component_Save_Id<Head>()>,
		"Hash collision or duplicate SaveName detected in EC::ComponentRegistry.");

	template<typename T>
	static constexpr bool contains = std::same_as<T, Head> || Next::template contains<T>;

	template<std::uint64_t SaveId>
	static constexpr bool contains_save_id =
		(SaveId != 0ULL && Component_Save_Id<Head>() == SaveId) || Next::template contains_save_id<SaveId>;

	static void Register_All(flecs::world & world)
	{
		world.component<Head>();
		Next::Register_All(world);
	}

	// -----------------------------------------------------------------------
	// Global Dense Table Iteration Helpers (Zero Queries for Passive Types)
	// -----------------------------------------------------------------------

#define OPENTS_EC_IMPL_MUT_GLOBAL_PASS(PassName, ConceptPrefix, MethodName, ExtraQueryFlags) \
	static void PassName(flecs::world & world) \
	{ \
		if constexpr (ConceptPrefix##Void<Head>) { \
			world.query_builder<Head>().query_flags(ExtraQueryFlags).each([](Head & comp) { \
				comp.MethodName(); \
			}); \
		} else if constexpr (ConceptPrefix##View<Head>) { \
			world.query_builder<Head>().query_flags(ExtraQueryFlags).each([](flecs::entity fe, Head & comp) { \
				comp.MethodName(EntityView(static_cast<std::uint64_t>(fe.id()))); \
			}); \
		} else if constexpr (ConceptPrefix##DerivedOwner<Head>) { \
			using SubOwnerT = typename Head::OwnerType; \
			world.query_builder<OwnerLink<ObjectClass> const, Head>().query_flags(ExtraQueryFlags).each( \
				[](OwnerLink<ObjectClass> const & link, Head & comp) { \
					if (link.ptr != nullptr) comp.MethodName(static_cast<SubOwnerT &>(*link.ptr)); \
				}); \
		} else { \
			if constexpr (ConceptPrefix##Root<Head, ObjectClass>) { \
				world.query_builder<OwnerLink<ObjectClass> const, Head>().query_flags(ExtraQueryFlags).each( \
					[](OwnerLink<ObjectClass> const & link, Head & comp) { \
						if (link.ptr != nullptr) comp.MethodName(*link.ptr); \
					}); \
			} \
			if constexpr (ConceptPrefix##Root<Head, CellClass>) { \
				world.query_builder<OwnerLink<CellClass> const, Head>().query_flags(ExtraQueryFlags).each( \
					[](OwnerLink<CellClass> const & link, Head & comp) { \
						if (link.ptr != nullptr) comp.MethodName(*link.ptr); \
					}); \
			} \
			if constexpr (ConceptPrefix##Root<Head, LocomotionClass>) { \
				world.query_builder<OwnerLink<LocomotionClass> const, Head>().query_flags(ExtraQueryFlags).each( \
					[](OwnerLink<LocomotionClass> const & link, Head & comp) { \
						if (link.ptr != nullptr) comp.MethodName(*link.ptr); \
					}); \
			} \
		} \
		Next::PassName(world); \
	}

#define OPENTS_EC_IMPL_CONST_GLOBAL_PASS(PassName, ConceptPrefix, MethodName) \
	static void PassName(flecs::world & world) \
	{ \
		if constexpr (ConceptPrefix##Void<Head>) { \
			world.query_builder<Head const>().each([](Head const & comp) { \
				comp.MethodName(); \
			}); \
		} else if constexpr (ConceptPrefix##View<Head>) { \
			world.query_builder<Head const>().each([](flecs::entity fe, Head const & comp) { \
				comp.MethodName(EntityView(static_cast<std::uint64_t>(fe.id()))); \
			}); \
		} else if constexpr (ConceptPrefix##DerivedOwner<Head>) { \
			using SubOwnerT = typename Head::OwnerType; \
			world.query_builder<OwnerLink<ObjectClass> const, Head const>().each( \
				[](OwnerLink<ObjectClass> const & link, Head const & comp) { \
					if (link.ptr != nullptr) comp.MethodName(static_cast<SubOwnerT const &>(*link.ptr)); \
				}); \
		} else { \
			if constexpr (ConceptPrefix##Root<Head, ObjectClass>) { \
				world.query_builder<OwnerLink<ObjectClass> const, Head const>().each( \
					[](OwnerLink<ObjectClass> const & link, Head const & comp) { \
						if (link.ptr != nullptr) comp.MethodName(*link.ptr); \
					}); \
			} \
			if constexpr (ConceptPrefix##Root<Head, CellClass>) { \
				world.query_builder<OwnerLink<CellClass> const, Head const>().each( \
					[](OwnerLink<CellClass> const & link, Head const & comp) { \
						if (link.ptr != nullptr) comp.MethodName(*link.ptr); \
					}); \
			} \
			if constexpr (ConceptPrefix##Root<Head, LocomotionClass>) { \
				world.query_builder<OwnerLink<LocomotionClass> const, Head const>().each( \
					[](OwnerLink<LocomotionClass> const & link, Head const & comp) { \
						if (link.ptr != nullptr) comp.MethodName(*link.ptr); \
					}); \
			} \
		} \
		Next::PassName(world); \
	}

	OPENTS_EC_IMPL_MUT_GLOBAL_PASS(AI_All, AI, AI, 0)                                     // G01
	OPENTS_EC_IMPL_MUT_GLOBAL_PASS(PostAI_All, PostAI, PostAI, 0)                         // G02
	OPENTS_EC_IMPL_MUT_GLOBAL_PASS(Environment_AI_All, EnvironmentAI, Environment_AI, 0)  // G03
	OPENTS_EC_IMPL_MUT_GLOBAL_PASS(Frame_Begin_All, FrameBegin, Frame_Begin, 0)           // G04
	OPENTS_EC_IMPL_MUT_GLOBAL_PASS(Frame_End_All, FrameEnd, Frame_End, 0)                 // G05
	OPENTS_EC_IMPL_CONST_GLOBAL_PASS(Pre_Render_All, PreRender, Pre_Render)               // G06
	OPENTS_EC_IMPL_CONST_GLOBAL_PASS(Post_Render_All, PostRender, Post_Render)            // G07
	OPENTS_EC_IMPL_MUT_GLOBAL_PASS(Scenario_Init_All, ScenarioInit, Scenario_Init, 0)     // G08
	// Post_Load runs across both enabled and disabled entities (e.g. units in Limbo / Piggybacker)
	OPENTS_EC_IMPL_MUT_GLOBAL_PASS(Post_Load_All, PostLoad, Post_Load, EcsQueryMatchDisabled) // G11

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
	// G10: Two-Subpass EC01 .SAV Trailer (Freeze Archetypes Before Swizzler!)
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

	template<typename StreamT>
	static void Append_Entity_Save_Types(flecs::entity fe, std::vector<std::uint64_t> & out_ids)
	{
		if constexpr (HasSerialize<Head, StreamT>) {
			if (fe.has<Head>()) {
				out_ids.push_back(Component_Save_Id<Head>());
			}
		}
		Next::template Append_Entity_Save_Types<StreamT>(fe, out_ids);
	}

	template<typename StreamT>
	static bool Write_Component_By_Save_Id(flecs::entity fe, std::uint64_t save_id, StreamT & stream)
	{
		if constexpr (HasSerialize<Head, StreamT>) {
			if (save_id == Component_Save_Id<Head>()) {
				Head * comp = fe.try_get_mut<Head>();
				if (comp == nullptr) {
					return false;
				}
				comp->Serialize(stream);
				return !stream.Was_Error();
			}
		}
		return Next::template Write_Component_By_Save_Id<StreamT>(fe, save_id, stream);
	}

	template<typename StreamT>
	static bool Ensure_Component_By_Save_Id(flecs::entity fe, std::uint64_t save_id)
	{
		if constexpr (HasSerialize<Head, StreamT>) {
			if (save_id == Component_Save_Id<Head>()) {
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
			if (save_id == Component_Save_Id<Head>()) {
				Head * comp = fe.try_get_mut<Head>();
				if (comp == nullptr) {
					return false;
				}
				comp->Serialize(stream);
				return !stream.Was_Error();
			}
		}
		return Next::template Read_Component_By_Save_Id<StreamT>(fe, save_id, stream);
	}

	template<typename StreamT, typename IdOfFn>
	static bool Save_Trailer(flecs::world & world, StreamT & stream, IdOfFn && id_of)
	{
		if (Live_Entity_Count() == 0) {
			return !stream.Was_Error();
		}

		std::vector<Detail::SavedEntityManifest> records;
		auto collect_for_root = [&]<EntityRoot Root>(void) {
			world.query_builder<OwnerLink<Root> const>()
				.query_flags(EcsQueryMatchDisabled)
				.each([&](flecs::entity fe, OwnerLink<Root> const & link) {
					if (link.ptr == nullptr) {
						return;
					}
					Detail::SavedEntityManifest rec;
					rec.FlecsId = static_cast<std::uint64_t>(fe.id());
					rec.Root = static_cast<std::uint8_t>(Root_Kind_Of<Root>());
					rec.Flags = fe.has(flecs::Disabled) ? 1u : 0u;
					rec.OwnerSwizzleId = static_cast<std::uint32_t>(id_of(Root_Kind_Of<Root>(), link.ptr));
					if (rec.OwnerSwizzleId == 0) {
						return;
					}
					Append_Entity_Save_Types<StreamT>(fe, rec.TypeIds);
					if (!rec.TypeIds.empty() || rec.Flags != 0) {
						records.push_back(std::move(rec));
					}
				});
		};

		collect_for_root.template operator()<ObjectClass>();
		collect_for_root.template operator()<CellClass>();
		collect_for_root.template operator()<LocomotionClass>();

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

		// Subpass 2: Framed component payload blobs
		for (Detail::SavedEntityManifest const & rec : records) {
			flecs::entity fe(world, static_cast<flecs::entity_t>(rec.FlecsId));
			for (std::uint64_t const tid : rec.TypeIds) {
				unsigned int const blob_len_pos = stream.Offset();
				std::uint32_t blob_bytes = 0;
				stream.Serialize(blob_bytes);
				unsigned int const blob_start = stream.Offset();
				if (!Write_Component_By_Save_Id<StreamT>(fe, tid, stream)) {
					stream.Fail();
					return false;
				}
				blob_bytes = stream.Offset() - blob_start;
				stream.Overwrite_Bytes(blob_len_pos, &blob_bytes, sizeof(blob_bytes));
			}
		}

		payload_bytes = stream.Offset() - payload_start;
		stream.Overwrite_Bytes(payload_len_pos, &payload_bytes, sizeof(payload_bytes));
		return !stream.Was_Error();
	}

	template<typename StreamT, typename ResolveOwnerFn>
	static bool Load_Trailer(flecs::world & world, StreamT & stream, ResolveOwnerFn && resolve_owner)
	{
		if (stream.Offset() >= stream.Size()) {
			return !stream.Was_Error();
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

		typename StreamT::BoundScope trailer_scope(stream, stream.Offset() + payload_bytes);
		if (stream.Was_Error()) {
			return false;
		}

		// Read type directory (metadata used for diagnostics/forward-compat)
		std::uint32_t type_count = 0;
		stream.Serialize(type_count);
		if (!stream.Fits(static_cast<int>(type_count), sizeof(std::uint64_t) + sizeof(std::uint16_t))) {
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
				std::string dummy(name_len, '\0');
				stream.Serialize_Bytes(dummy.data(), static_cast<int>(name_len));
			}
		}

		// Subpass 1: Materialize all entities and ensure<C>() every component slot
		// BEFORE any Serialize() call registers raw component addresses in Swizzler!
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

			void * owner_ptr = resolve_owner(static_cast<RootKind>(root), owner_id);
			if (owner_ptr != nullptr) {
				bool const is_disabled = (flags & 1u) != 0u;
				item.FlecsId = Detail::Materialize_Loaded_Owner(static_cast<RootKind>(root), owner_ptr, is_disabled);
				flecs::entity fe(world, static_cast<flecs::entity_t>(item.FlecsId));
				for (std::uint64_t const tid : item.TypeIds) {
					(void)Ensure_Component_By_Save_Id<StreamT>(fe, tid);
				}
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
				if (item.FlecsId != 0) {
					flecs::entity fe(world, static_cast<flecs::entity_t>(item.FlecsId));
					consumed = Read_Component_By_Save_Id<StreamT>(fe, tid, stream);
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
	// G12 / E27: 4-Level Per-Component & Global Compute_CRC
	// -----------------------------------------------------------------------

	template<typename C, EntityRoot Root>
	static bool Compute_Entity_Component_CRC(TypedEntity<Root> const & holder, CRCEngine & crc)
	{
		return holder.template Compute_CRC_Of<C>(crc);
	}

	static void Compute_Flecs_Entity_CRC(flecs::entity fe, CRCEngine & crc)
	{
		if constexpr (HasComputeCRC<Head>) {
			if (Head const * comp = fe.try_get<Head>(); comp != nullptr) {
				Detail::Feed_Component_CRC(*comp, fe, crc);
			}
		}
		Next::Compute_Flecs_Entity_CRC(fe, crc);
	}

	template<EntityRoot Root>
	static void Compute_Entity_CRC(TypedEntity<Root> const & holder, CRCEngine & crc)
	{
		if (!holder.Is_Materialized() || !Is_World_Alive()) {
			return;
		}
		flecs::entity fe(World(), static_cast<flecs::entity_t>(holder.Id()));
		if (fe.is_alive()) {
			Compute_Flecs_Entity_CRC(fe, crc);
		}
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
	// Entity-Scoped Dispatchers (E02..E26)
	// -----------------------------------------------------------------------

	template<typename OwnerT>
	static void Dispatch_Detach(OwnerT & owner, AbstractClass const * target, bool all)
	{
		auto & holder = Detail::Holder_Of(owner);
		if (!holder.Is_Materialized() || !Is_World_Alive()) {
			return;
		}
		using RootT = typename std::remove_cvref_t<decltype(holder)>::RootType;
		if constexpr (HasDetachRoot<Head, RootT> || HasDetachPlain<Head>) {
			flecs::entity fe(World(), static_cast<flecs::entity_t>(holder.Id()));
			if (Head * comp = fe.try_get_mut<Head>(); comp != nullptr) {
				if constexpr (HasDetachRoot<Head, RootT>) {
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
		auto & holder = Detail::Holder_Of(owner); \
		if (!holder.Is_Materialized() || !Is_World_Alive()) { \
			return; \
		} \
		if constexpr (ConceptName##WithSelf<Head, OwnerT, Args...> || ConceptName##NoSelf<Head, Args...>) { \
			flecs::entity fe(World(), static_cast<flecs::entity_t>(holder.Id())); \
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
		auto const & holder = Detail::Holder_Of(owner); \
		if (!holder.Is_Materialized() || !Is_World_Alive()) { \
			return; \
		} \
		if constexpr (ConceptName##WithSelf<Head, OwnerT, Args...> || ConceptName##NoSelf<Head, Args...>) { \
			flecs::entity fe(World(), static_cast<flecs::entity_t>(holder.Id())); \
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
		auto & holder = Detail::Holder_Of(owner);
		if (!holder.Is_Materialized() || !Is_World_Alive()) {
			return;
		}
		if constexpr (
			HasCapturedWithSelf<Head, OwnerT, HouseT *, HouseT *> ||
			HasCapturedNoSelf<Head, HouseT *, HouseT *> ||
			HasCapturedWithSelf<Head, OwnerT, HouseT *> ||
			HasCapturedNoSelf<Head, HouseT *>
		) {
			flecs::entity fe(World(), static_cast<flecs::entity_t>(holder.Id()));
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

	// E26a/E26b: Piggyback start/end also automatically toggles flecs::Disabled
	// on the carried locomotor via the base case!
	template<typename OwnerT>
	static void Dispatch_Begin_Piggyback(OwnerT & carrier, ILocomotion * carried)
	{
		auto & holder = Detail::Holder_Of(carrier);
		if (holder.Is_Materialized() && Is_World_Alive()) {
			if constexpr (HasBeginPiggybackWithSelf<Head, OwnerT, ILocomotion *> || HasBeginPiggybackNoSelf<Head, ILocomotion *>) {
				flecs::entity fe(World(), static_cast<flecs::entity_t>(holder.Id()));
				if (Head * comp = fe.try_get_mut<Head>(); comp != nullptr) {
					if constexpr (HasBeginPiggybackWithSelf<Head, OwnerT, ILocomotion *>) {
						comp->Begin_Piggyback(carrier, carried);
					} else {
						comp->Begin_Piggyback(carried);
					}
				}
			}
		}
		Next::Dispatch_Begin_Piggyback(carrier, carried);
	}

	template<typename OwnerT>
	static void Dispatch_End_Piggyback(OwnerT & carrier, ILocomotion * restored)
	{
		auto & holder = Detail::Holder_Of(carrier);
		if (holder.Is_Materialized() && Is_World_Alive()) {
			if constexpr (HasEndPiggybackWithSelf<Head, OwnerT, ILocomotion *> || HasEndPiggybackNoSelf<Head, ILocomotion *>) {
				flecs::entity fe(World(), static_cast<flecs::entity_t>(holder.Id()));
				if (Head * comp = fe.try_get_mut<Head>(); comp != nullptr) {
					if constexpr (HasEndPiggybackWithSelf<Head, OwnerT, ILocomotion *>) {
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

template<EntityRoot Root>
inline void TypedEntity<Root>::Compute_CRC(CRCEngine & crc) const
{
	GameComponents::Compute_Entity_CRC(*this, crc);
}

} // namespace EC
