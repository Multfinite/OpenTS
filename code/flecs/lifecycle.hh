/*
 * O P E N   T S
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 */

#pragma once

#include "flecs/id.hh"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

class AbstractClass;
class CRCEngine;
class FlecsWorldClass;
class SaveStreamClass;

// Simulation points inside LogicClass::AI where component ticks run. Each phase
// executes after its host loop completes and wraps component calls in a
// deferred command scope so structural changes never invalidate active tables.
enum class SimPhase : std::uint8_t {
	PostObjects = 0,
	PostHouses = 1,
};

// Controls whether a component's simulation tick pauses while its owning game
// object is off the map in limbo (for example, loaded inside a transport).
enum class TickScope : std::uint8_t {
	WorldActive = 0,
	Always = 1,
};

// Snapshot of the owning C++ game object's identity and activity flags queried
// on demand from the engine. Nothing here is duplicated in component storage.
struct ObjectStateInfo {
	std::int32_t Kind = 0;
	std::int32_t FetchId = -1;
	bool IsActive = true;
	bool IsInLimbo = false;
};

using ObjectInspectorFn = ObjectStateInfo (*)(void const * object, bool is_locomotor);

// Gives component callbacks access to the owning object and world without storing back-pointers.
struct EntityContext {
	FlecsWorldClass * World = nullptr;
	EntityId Entity = 0;
	EntityId ParentEntity = 0;
	void * Owner = nullptr;
	void * ParentOwner = nullptr;
	std::int32_t Kind = 0;
	std::int32_t FetchId = -1;
	bool IsLocomotor = false;

	template<typename T>
	T * Owner_As(void) const
	{
		return(static_cast<T *>(Owner));
	}

	template<typename T>
	T * Parent_Owner_As(void) const
	{
		return(static_cast<T *>(ParentOwner));
	}
};

// Unique per-type key immune to linker identical-code folding because each
// specialization owns a distinct mutable static byte in data storage.
using ComponentTypeKey = void const *;

template<typename T>
struct ComponentTypeTagHolder {
	static inline char Value = 0;
};

template<typename T>
constexpr ComponentTypeKey Component_Type_Key(void) noexcept
{
	return(&ComponentTypeTagHolder<std::remove_cvref_t<T>>::Value);
}

// Compile-time opt-in concepts for component lifecycle methods and traits.
template<typename T>
concept HasOnReady = requires(T & component, EntityContext const & context) {
	{ component.On_Ready(context) } -> std::same_as<void>;
};

template<typename T>
concept HasProcess = requires(T & component, EntityContext const & context) {
	{ component.Process(context) } -> std::same_as<void>;
};

template<typename T>
concept HasOnEnterWorld = requires(T & component, EntityContext const & context) {
	{ component.On_Enter_World(context) } -> std::same_as<void>;
};

template<typename T>
concept HasOnExitWorld = requires(T & component, EntityContext const & context) {
	{ component.On_Exit_World(context) } -> std::same_as<void>;
};

template<typename T>
concept HasRenderForeground = requires(T const & component, EntityContext const & context, void * surface) {
	{ component.Render_Foreground(context, surface) } -> std::same_as<void>;
};

template<typename T>
concept HasDetach = requires(T & component, EntityContext const & context, AbstractClass const * target, bool all) {
	{ component.Detach(context, target, all) } -> std::same_as<void>;
};

template<typename T>
concept HasComputeCRC = requires(T const & component, EntityContext const & context, CRCEngine & crc) {
	{ component.Compute_CRC(context, crc) } -> std::same_as<void>;
};

template<typename T>
concept HasSerialize = requires(T & component, SaveStreamClass & stream) {
	{ component.Serialize(stream) } -> std::same_as<void>;
};

template<typename T>
concept HasPostLoadResolve = requires(T & component, EntityContext const & context) {
	{ component.Post_Load_Resolve(context) } -> std::same_as<void>;
};

template<typename T>
concept HasOnRemove = requires(T & component, EntityContext const & context) {
	{ component.On_Remove(context) } -> std::same_as<void>;
};

template<typename T>
concept HasCustomPhase = requires {
	{ T::Phase } -> std::convertible_to<SimPhase>;
};

template<typename T>
concept HasCustomScope = requires {
	{ T::Scope } -> std::convertible_to<TickScope>;
};

// Rejects mistyped hook signatures or missing const qualifiers at compile time.
namespace detail {

struct HookNameProbes {
	int On_Ready;
	int Process;
	int On_Enter_World;
	int On_Exit_World;
	int Render_Foreground;
	int Detach;
	int Compute_CRC;
	int Serialize;
	int Post_Load_Resolve;
	int On_Remove;
	int Phase;
	int Scope;
};

template<typename T>
struct CombinedHookProbe : T, HookNameProbes {};

template<typename T>
concept DeclaresOnReady = !std::is_final_v<T> && !requires { &CombinedHookProbe<T>::On_Ready; };

template<typename T>
concept DeclaresProcess = !std::is_final_v<T> && !requires { &CombinedHookProbe<T>::Process; };

template<typename T>
concept DeclaresOnEnterWorld = !std::is_final_v<T> && !requires { &CombinedHookProbe<T>::On_Enter_World; };

template<typename T>
concept DeclaresOnExitWorld = !std::is_final_v<T> && !requires { &CombinedHookProbe<T>::On_Exit_World; };

template<typename T>
concept DeclaresRenderForeground = !std::is_final_v<T> && !requires { &CombinedHookProbe<T>::Render_Foreground; };

template<typename T>
concept DeclaresDetach = !std::is_final_v<T> && !requires { &CombinedHookProbe<T>::Detach; };

template<typename T>
concept DeclaresComputeCRC = !std::is_final_v<T> && !requires { &CombinedHookProbe<T>::Compute_CRC; };

template<typename T>
concept DeclaresSerialize = !std::is_final_v<T> && !requires { &CombinedHookProbe<T>::Serialize; };

template<typename T>
concept DeclaresPostLoadResolve = !std::is_final_v<T> && !requires { &CombinedHookProbe<T>::Post_Load_Resolve; };

template<typename T>
concept DeclaresOnRemove = !std::is_final_v<T> && !requires { &CombinedHookProbe<T>::On_Remove; };

template<typename T>
concept DeclaresPhase = !std::is_final_v<T> && !requires { &CombinedHookProbe<T>::Phase; };

template<typename T>
concept DeclaresScope = !std::is_final_v<T> && !requires { &CombinedHookProbe<T>::Scope; };

template<typename T>
constexpr void Validate_Component_Hooks(void)
{
	static_assert(std::is_class_v<T>,
		"Components registered with ComponentOps must be structs or classes.");
	static_assert(std::is_default_constructible_v<T>,
		"Components must be default constructible so archetype storage and save loading can initialize slots.");
	static_assert(std::is_move_constructible_v<T> && std::is_move_assignable_v<T>,
		"Components must be move constructible and move assignable across archetype table transitions.");
	static_assert(!DeclaresOnReady<T> || HasOnReady<T>,
		"On_Ready must match: void On_Ready(EntityContext const & context).");
	static_assert(!DeclaresProcess<T> || HasProcess<T>,
		"Process must match: void Process(EntityContext const & context).");
	static_assert(!DeclaresOnEnterWorld<T> || HasOnEnterWorld<T>,
		"On_Enter_World must match: void On_Enter_World(EntityContext const & context).");
	static_assert(!DeclaresOnExitWorld<T> || HasOnExitWorld<T>,
		"On_Exit_World must match: void On_Exit_World(EntityContext const & context).");
	static_assert(!DeclaresRenderForeground<T> || HasRenderForeground<T>,
		"Render_Foreground must match: void Render_Foreground(EntityContext const & context, void * surface) const.");
	static_assert(!DeclaresDetach<T> || HasDetach<T>,
		"Detach must match: void Detach(EntityContext const & context, AbstractClass const * target, bool all).");
	static_assert(!DeclaresComputeCRC<T> || HasComputeCRC<T>,
		"Compute_CRC must match: void Compute_CRC(EntityContext const & context, CRCEngine & crc) const.");
	static_assert(!DeclaresSerialize<T> || HasSerialize<T>,
		"Serialize must match: void Serialize(SaveStreamClass & stream).");
	static_assert(!DeclaresPostLoadResolve<T> || HasPostLoadResolve<T>,
		"Post_Load_Resolve must match: void Post_Load_Resolve(EntityContext const & context).");
	static_assert(!DeclaresOnRemove<T> || HasOnRemove<T>,
		"On_Remove must match: void On_Remove(EntityContext const & context).");
	static_assert(!DeclaresPhase<T> || HasCustomPhase<T>,
		"Phase trait must be convertible to SimPhase.");
	static_assert(!DeclaresScope<T> || HasCustomScope<T>,
		"Scope trait must be convertible to TickScope.");
}

}	// namespace detail

// Type-erased storage lifecycle and engine hook table generated once per
// component type at registration time.
struct ComponentOps {
	std::size_t Size = 0;
	std::size_t Alignment = 0;
	SimPhase Phase = SimPhase::PostObjects;
	TickScope Scope = TickScope::WorldActive;
	bool IsCopyable = false;

	void (*Ctor)(void * ptr, std::int32_t count) = nullptr;
	void (*Dtor)(void * ptr, std::int32_t count) = nullptr;
	void (*Copy)(void * dst, void const * src, std::int32_t count) = nullptr;
	void (*Move)(void * dst, void * src, std::int32_t count) = nullptr;
	void (*CopyCtor)(void * dst, void const * src, std::int32_t count) = nullptr;
	void (*MoveCtor)(void * dst, void * src, std::int32_t count) = nullptr;

	void (*OnReady)(void * component, EntityContext const & context) = nullptr;
	void (*Process)(void * component, EntityContext const & context) = nullptr;
	void (*OnEnterWorld)(void * component, EntityContext const & context) = nullptr;
	void (*OnExitWorld)(void * component, EntityContext const & context) = nullptr;
	void (*RenderForeground)(void const * component, EntityContext const & context, void * surface) = nullptr;
	void (*Detach)(void * component, EntityContext const & context, AbstractClass const * target, bool all) = nullptr;
	void (*ComputeCRC)(void const * component, EntityContext const & context, CRCEngine & crc) = nullptr;
	void (*Serialize)(void * component, SaveStreamClass & stream) = nullptr;
	void (*PostLoadResolve)(void * component, EntityContext const & context) = nullptr;
	void (*OnRemove)(void * component, EntityContext const & context) = nullptr;
};

template<typename T>
ComponentOps Make_Component_Ops(void)
{
	using CleanT = std::remove_cvref_t<T>;
	detail::Validate_Component_Hooks<CleanT>();

	ComponentOps ops;
	ops.Size = sizeof(CleanT);
	ops.Alignment = alignof(CleanT);
	if constexpr (HasCustomPhase<CleanT>) {
		ops.Phase = CleanT::Phase;
	}
	if constexpr (HasCustomScope<CleanT>) {
		ops.Scope = CleanT::Scope;
	}

	ops.Ctor = [](void * ptr, std::int32_t count) {
		CleanT * items = static_cast<CleanT *>(ptr);
		for (std::int32_t i = 0; i < count; ++i) {
			std::construct_at(items + i);
		}
	};

	if constexpr (!std::is_trivially_destructible_v<CleanT>) {
		ops.Dtor = [](void * ptr, std::int32_t count) {
			CleanT * items = static_cast<CleanT *>(ptr);
			for (std::int32_t i = 0; i < count; ++i) {
				std::destroy_at(items + i);
			}
		};
	}

	if constexpr (!std::is_trivially_move_assignable_v<CleanT> || !std::is_trivially_move_constructible_v<CleanT>) {
		ops.Move = [](void * dst, void * src, std::int32_t count) {
			CleanT * dst_items = static_cast<CleanT *>(dst);
			CleanT * src_items = static_cast<CleanT *>(src);
			for (std::int32_t i = 0; i < count; ++i) {
				dst_items[i] = std::move(src_items[i]);
			}
		};
		ops.MoveCtor = [](void * dst, void * src, std::int32_t count) {
			CleanT * dst_items = static_cast<CleanT *>(dst);
			CleanT * src_items = static_cast<CleanT *>(src);
			for (std::int32_t i = 0; i < count; ++i) {
				std::construct_at(dst_items + i, std::move(src_items[i]));
			}
		};
	}

	if constexpr (std::is_copy_constructible_v<CleanT> && std::is_copy_assignable_v<CleanT>) {
		ops.IsCopyable = true;
		if constexpr (!std::is_trivially_copyable_v<CleanT>) {
			ops.Copy = [](void * dst, void const * src, std::int32_t count) {
				CleanT * dst_items = static_cast<CleanT *>(dst);
				CleanT const * src_items = static_cast<CleanT const *>(src);
				for (std::int32_t i = 0; i < count; ++i) {
					dst_items[i] = src_items[i];
				}
			};
			ops.CopyCtor = [](void * dst, void const * src, std::int32_t count) {
				CleanT * dst_items = static_cast<CleanT *>(dst);
				CleanT const * src_items = static_cast<CleanT const *>(src);
				for (std::int32_t i = 0; i < count; ++i) {
					std::construct_at(dst_items + i, src_items[i]);
				}
			};
		}
	}

	if constexpr (HasOnReady<CleanT>) {
		ops.OnReady = [](void * component, EntityContext const & context) {
			static_cast<CleanT *>(component)->On_Ready(context);
		};
	}
	if constexpr (HasProcess<CleanT>) {
		ops.Process = [](void * component, EntityContext const & context) {
			static_cast<CleanT *>(component)->Process(context);
		};
	}
	if constexpr (HasOnEnterWorld<CleanT>) {
		ops.OnEnterWorld = [](void * component, EntityContext const & context) {
			static_cast<CleanT *>(component)->On_Enter_World(context);
		};
	}
	if constexpr (HasOnExitWorld<CleanT>) {
		ops.OnExitWorld = [](void * component, EntityContext const & context) {
			static_cast<CleanT *>(component)->On_Exit_World(context);
		};
	}
	if constexpr (HasRenderForeground<CleanT>) {
		ops.RenderForeground = [](void const * component, EntityContext const & context, void * surface) {
			static_cast<CleanT const *>(component)->Render_Foreground(context, surface);
		};
	}
	if constexpr (HasDetach<CleanT>) {
		ops.Detach = [](void * component, EntityContext const & context, AbstractClass const * target, bool all) {
			static_cast<CleanT *>(component)->Detach(context, target, all);
		};
	}
	if constexpr (HasComputeCRC<CleanT>) {
		ops.ComputeCRC = [](void const * component, EntityContext const & context, CRCEngine & crc) {
			static_cast<CleanT const *>(component)->Compute_CRC(context, crc);
		};
	}
	if constexpr (HasSerialize<CleanT>) {
		ops.Serialize = [](void * component, SaveStreamClass & stream) {
			static_cast<CleanT *>(component)->Serialize(stream);
		};
	}
	if constexpr (HasPostLoadResolve<CleanT>) {
		ops.PostLoadResolve = [](void * component, EntityContext const & context) {
			static_cast<CleanT *>(component)->Post_Load_Resolve(context);
		};
	}
	if constexpr (HasOnRemove<CleanT>) {
		ops.OnRemove = [](void * component, EntityContext const & context) {
			static_cast<CleanT *>(component)->On_Remove(context);
		};
	}

	return(ops);
}

// Structural concept matching SaveStreamClass's byte framing operations so
// world.cpp can read and write length-prefixed component records without
// pulling platform headers into the bridge.
template<typename S>
concept ByteSaveStream = requires(
	S & stream,
	S const & cstream,
	void * data,
	void const * cdata,
	unsigned int offset,
	int length)
{
	{ cstream.Is_Saving() } -> std::convertible_to<bool>;
	{ cstream.Is_Loading() } -> std::convertible_to<bool>;
	{ cstream.Was_Error() } -> std::convertible_to<bool>;
	{ stream.Fail() };
	{ cstream.Offset() } -> std::convertible_to<unsigned int>;
	{ cstream.Size() } -> std::convertible_to<unsigned int>;
	{ stream.Overwrite_Bytes(offset, cdata, length) };
	{ stream.Serialize_Bytes(data, length) };
};

struct ComponentStreamAdapter {
	SaveStreamClass * Stream = nullptr;
	bool (*IsSaving)(void const * raw) = nullptr;
	bool (*IsLoading)(void const * raw) = nullptr;
	bool (*WasError)(void const * raw) = nullptr;
	void (*Fail)(void * raw) = nullptr;
	unsigned int (*Offset)(void const * raw) = nullptr;
	unsigned int (*Size)(void const * raw) = nullptr;
	void (*OverwriteBytes)(void * raw, unsigned int offset, void const * data, int length) = nullptr;
	void (*SerializeBytes)(void * raw, void * data, int length) = nullptr;
};

template<ByteSaveStream S>
ComponentStreamAdapter Make_Stream_Adapter(S & stream)
{
	static_assert(std::is_same_v<std::remove_cvref_t<S>, SaveStreamClass>,
		"Component serialization streams must name SaveStreamClass so component Serialize methods bind directly.");

	ComponentStreamAdapter adapter;
	adapter.Stream = &stream;
	adapter.IsSaving = [](void const * raw) -> bool {
		return(static_cast<S const *>(raw)->Is_Saving());
	};
	adapter.IsLoading = [](void const * raw) -> bool {
		return(static_cast<S const *>(raw)->Is_Loading());
	};
	adapter.WasError = [](void const * raw) -> bool {
		return(static_cast<S const *>(raw)->Was_Error());
	};
	adapter.Fail = [](void * raw) {
		static_cast<S *>(raw)->Fail();
	};
	adapter.Offset = [](void const * raw) -> unsigned int {
		return(static_cast<S const *>(raw)->Offset());
	};
	adapter.Size = [](void const * raw) -> unsigned int {
		return(static_cast<S const *>(raw)->Size());
	};
	adapter.OverwriteBytes = [](void * raw, unsigned int offset, void const * data, int length) {
		static_cast<S *>(raw)->Overwrite_Bytes(offset, data, length);
	};
	adapter.SerializeBytes = [](void * raw, void * data, int length) {
		static_cast<S *>(raw)->Serialize_Bytes(data, length);
	};
	return(adapter);
}
