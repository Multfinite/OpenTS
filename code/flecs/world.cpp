/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The only translation unit that includes flecs headers. The compile
// definitions flecs is built with are exported by its target, so the layout
// this file sees is the layout the library was built with.

#include "flecs/world.h"

#include "flecs/components.hh"

#include <flecs.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

flecs::world * As_World(void * world)
{
	return(static_cast<flecs::world *>(world));
}


#ifndef OPENTS_EC_DISABLED
flecs::world const * As_Const_World(void const * world)
{
	return(static_cast<flecs::world const *>(world));
}
#endif


struct CanonicalSortKey {
	std::int32_t Kind = 0;
	std::int32_t FetchId = -1;
	std::uint32_t AttachSequence = 0;

	bool operator<(CanonicalSortKey const & other) const noexcept
	{
		if (Kind != other.Kind) {
			return(Kind < other.Kind);
		}
		if (FetchId != other.FetchId) {
			return(FetchId < other.FetchId);
		}
		return(AttachSequence < other.AttachSequence);
	}
};


bool Skip_Stream_Bytes(ComponentStreamAdapter const & io, std::uint32_t byte_count)
{
	unsigned char scratch[256];
	std::uint32_t remaining = byte_count;
	while (remaining > 0) {
		int const chunk = static_cast<int>(remaining > sizeof(scratch) ? sizeof(scratch) : remaining);
		io.SerializeBytes(io.Stream, scratch, chunk);
		if (io.WasError(io.Stream)) {
			return(false);
		}
		remaining -= static_cast<std::uint32_t>(chunk);
	}
	return(true);
}

}	// namespace


struct FlecsWorldClass::BridgeState {
	struct RegisteredComponent {
		FlecsWorldClass * OwnerBridge = nullptr;
		ComponentTypeKey Key = nullptr;
		std::string Name;
		std::uint32_t Index = 0;
		ComponentOps Ops;
		ecs_entity_t FlecsId = 0;
		ecs_query_t * Query = nullptr;
	};

	struct PendingReadyItem {
		EntityId Entity = 0;
		std::uint32_t ComponentIndex = 0;
	};

	ObjectInspectorFn Inspector = nullptr;
	std::vector<std::unique_ptr<RegisteredComponent>> Components;
	std::unordered_map<ComponentTypeKey, std::uint32_t> IndexByKey;
	std::unordered_map<std::string, std::uint32_t> IndexByName;
	std::vector<PendingReadyItem> PendingReady;
	std::unordered_set<EntityId> PendingDestroy;
	std::uint32_t NextAttachSequence = 1;
	bool ScenarioLoading = false;
	bool InSaveLoad = false;
	bool HasAnyDetach = false;
};


#ifndef OPENTS_EC_DISABLED
struct FlecsWorldClass::BridgeInternals {

using RegisteredEntry = FlecsWorldClass::BridgeState::RegisteredComponent;


static EntityContext Build_Entity_Context(
	FlecsWorldClass * bridge,
	ecs_world_t const * raw_world,
	ObjectInspectorFn inspector,
	EntityId entity_id,
	ObjectStateInfo * out_info = nullptr,
	CanonicalSortKey * out_sort = nullptr)
{
	EntityContext context;
	context.World = bridge;
	context.Entity = entity_id;

	ObjectStateInfo info;
	CanonicalSortKey sort_key;

	if (raw_world == nullptr || entity_id == 0) {
		if (out_info != nullptr) {
			*out_info = info;
		}
		if (out_sort != nullptr) {
			*out_sort = sort_key;
		}
		return(context);
	}

	flecs::entity const entity(raw_world, entity_id);
	if (EntityBridgeMeta const * meta = entity.try_get<EntityBridgeMeta>()) {
		sort_key.AttachSequence = meta->AttachSequence;
	}

	if (LegacyObjectLink const * obj_link = entity.try_get<LegacyObjectLink>()) {
		context.Owner = obj_link->Object;
		context.IsLocomotor = false;
	} else if (LegacyLocomotionLink const * loco_link = entity.try_get<LegacyLocomotionLink>()) {
		context.Owner = loco_link->Locomotion;
		context.IsLocomotor = true;
	}

	ecs_entity_t const parent_id = ecs_get_parent(raw_world, entity_id);
	if (parent_id != 0 && ecs_is_alive(raw_world, parent_id)) {
		flecs::entity const parent(raw_world, parent_id);
		context.ParentEntity = parent_id;
		if (LegacyObjectLink const * parent_link = parent.try_get<LegacyObjectLink>()) {
			context.ParentOwner = parent_link->Object;
		} else if (LegacyLocomotionLink const * parent_loco = parent.try_get<LegacyLocomotionLink>()) {
			context.ParentOwner = parent_loco->Locomotion;
		}
	}

	if (inspector != nullptr) {
		if (context.IsLocomotor && context.ParentOwner != nullptr) {
			// Locomotor child entities inherit activity, limbo state, and sort
			// identity from the unit they drive when parented.
			info = inspector(context.ParentOwner, false);
		} else {
			info = inspector(context.Owner, context.IsLocomotor);
		}
	} else {
		info.Kind = 0;
		info.FetchId = static_cast<std::int32_t>(sort_key.AttachSequence);
		info.IsActive = true;
		info.IsInLimbo = false;
	}

	context.Kind = info.Kind;
	context.FetchId = info.FetchId;
	sort_key.Kind = info.Kind;
	sort_key.FetchId = info.FetchId;

	if (out_info != nullptr) {
		*out_info = info;
	}
	if (out_sort != nullptr) {
		*out_sort = sort_key;
	}
	return(context);
}


static void Hook_Ctor(void * ptr, int32_t count, ecs_type_info_t const * type_info)
{
	RegisteredEntry const * entry = static_cast<RegisteredEntry const *>(type_info->hooks.ctx);
	entry->Ops.Ctor(ptr, count);
}


static void Hook_Dtor(void * ptr, int32_t count, ecs_type_info_t const * type_info)
{
	RegisteredEntry const * entry = static_cast<RegisteredEntry const *>(type_info->hooks.ctx);
	entry->Ops.Dtor(ptr, count);
}


static void Hook_Copy(void * dst, void const * src, int32_t count, ecs_type_info_t const * type_info)
{
	RegisteredEntry const * entry = static_cast<RegisteredEntry const *>(type_info->hooks.ctx);
	entry->Ops.Copy(dst, src, count);
}


static void Hook_Move(void * dst, void * src, int32_t count, ecs_type_info_t const * type_info)
{
	RegisteredEntry const * entry = static_cast<RegisteredEntry const *>(type_info->hooks.ctx);
	entry->Ops.Move(dst, src, count);
}


static void Hook_Copy_Ctor(void * dst, void const * src, int32_t count, ecs_type_info_t const * type_info)
{
	RegisteredEntry const * entry = static_cast<RegisteredEntry const *>(type_info->hooks.ctx);
	entry->Ops.CopyCtor(dst, src, count);
}


static void Hook_Move_Ctor(void * dst, void * src, int32_t count, ecs_type_info_t const * type_info)
{
	RegisteredEntry const * entry = static_cast<RegisteredEntry const *>(type_info->hooks.ctx);
	entry->Ops.MoveCtor(dst, src, count);
}


static void Hook_On_Remove(ecs_iter_t * it)
{
	RegisteredEntry * entry = static_cast<RegisteredEntry *>(it->ctx);
	if (entry == nullptr || entry->Ops.OnRemove == nullptr) {
		return;
	}

	FlecsWorldClass * bridge = entry->OwnerBridge;
	ObjectInspectorFn const inspector = bridge != nullptr ? bridge->Get_Object_Inspector() : nullptr;

	unsigned char * base = static_cast<unsigned char *>(ecs_field_w_size(it, entry->Ops.Size, 0));
	for (int32_t i = 0; i < it->count; ++i) {
		EntityContext const context = Build_Entity_Context(
			bridge,
			it->real_world,
			inspector,
			it->entities[i]);
		entry->Ops.OnRemove(base + static_cast<std::size_t>(i) * entry->Ops.Size, context);
	}
}


static void Bind_Component_To_World(flecs::world * world, RegisteredEntry & entry)
{
	if (world == nullptr || entry.FlecsId != 0) {
		return;
	}

	ecs_entity_t entity_id = 0;
	if (!entry.Name.empty()) {
		ecs_entity_desc_t edesc = {};
		edesc.name = entry.Name.c_str();
		edesc.sep = "";
		entity_id = ecs_entity_init(world->c_ptr(), &edesc);
	} else {
		entity_id = world->entity().id();
	}

	ecs_component_desc_t cdesc = {};
	cdesc.entity = entity_id;
	cdesc.type.size = static_cast<ecs_size_t>(entry.Ops.Size);
	cdesc.type.alignment = static_cast<ecs_size_t>(entry.Ops.Alignment);
	entry.FlecsId = ecs_component_init(world->c_ptr(), &cdesc);

	ecs_type_hooks_t hooks = {};
	hooks.ctx = &entry;
	hooks.ctor = Hook_Ctor;
	hooks.dtor = entry.Ops.Dtor != nullptr ? Hook_Dtor : nullptr;
	hooks.move = entry.Ops.Move != nullptr ? Hook_Move : nullptr;
	hooks.move_ctor = entry.Ops.MoveCtor != nullptr ? Hook_Move_Ctor : nullptr;
	if (entry.Ops.IsCopyable) {
		hooks.copy = entry.Ops.Copy != nullptr ? Hook_Copy : nullptr;
		hooks.copy_ctor = entry.Ops.CopyCtor != nullptr ? Hook_Copy_Ctor : nullptr;
	} else {
		hooks.flags |= ECS_TYPE_HOOK_COPY_ILLEGAL;
	}
	if (entry.Ops.OnRemove != nullptr) {
		hooks.on_remove = Hook_On_Remove;
	}
	ecs_set_hooks_id(world->c_ptr(), entry.FlecsId, &hooks);

	ecs_query_desc_t qdesc = {};
	qdesc.terms[0].id = entry.FlecsId;
	qdesc.cache_kind = EcsQueryCacheAuto;
	entry.Query = ecs_query_init(world->c_ptr(), &qdesc);
}

};
#endif


FlecsWorldClass::~FlecsWorldClass(void)
{
	Shutdown();
	delete State;
	State = nullptr;
}


FlecsWorldClass::BridgeState & FlecsWorldClass::Ensure_State(void)
{
	if (State == nullptr) {
		State = new BridgeState();
	}
	return(*State);
}


void FlecsWorldClass::Ensure_World(void)
{
#ifndef OPENTS_EC_DISABLED
	BridgeState & state = Ensure_State();
	if (World == nullptr) {
		flecs::world * world = new flecs::world();
		World = world;
		world->component<LegacyObjectLink>()
			.on_remove([this](flecs::entity, LegacyObjectLink &) {
				LiveCount--;
			});
		world->component<LegacyLocomotionLink>()
			.on_remove([this](flecs::entity, LegacyLocomotionLink &) {
				LiveCount--;
			});
		world->component<EntityBridgeMeta>();
		Register_Tags();

		for (std::unique_ptr<BridgeState::RegisteredComponent> & entry : state.Components) {
			BridgeInternals::Bind_Component_To_World(world, *entry);
		}
	}
#endif
}


void FlecsWorldClass::Register_Tag(char const * classname)
{
	flecs::world * world = As_World(World);
	if (world != nullptr && classname != nullptr && !world->lookup(classname)) {
		world->entity(classname);
	}
}


EntityId FlecsWorldClass::Attach(void * object, char const * classname, bool locomotion)
{
#ifdef OPENTS_EC_DISABLED
	(void)object;
	(void)classname;
	(void)locomotion;
	return(0);
#else
	Ensure_World();
	BridgeState & state = Ensure_State();
	flecs::world * world = As_World(World);
	flecs::entity const entity = world->entity();

	if (classname != nullptr) {
		flecs::entity const tag = world->lookup(classname);
		if (tag) {
			entity.add(tag);
		}
	}
	entity.set<EntityBridgeMeta>({ state.NextAttachSequence++ });
	if (locomotion) {
		entity.set<LegacyLocomotionLink>({ object });
	} else {
		entity.set<LegacyObjectLink>({ object });
	}
	LiveCount++;
	return(entity.id());
#endif
}


EntityId FlecsWorldClass::Attach_Entity(void * object, char const * classname)
{
	return(Attach(object, classname, false));
}


EntityId FlecsWorldClass::Attach_Locomotor_Entity(void * locomotion, char const * classname)
{
	return(Attach(locomotion, classname, true));
}


EntityId FlecsWorldClass::Replace_Entity(EntityId current, void * object, char const * classname)
{
	Destroy_Entity(current);
	return(Attach_Entity(object, classname));
}


EntityId FlecsWorldClass::Replace_Locomotor_Entity(EntityId current, void * locomotion, char const * classname)
{
	Destroy_Entity(current);
	return(Attach_Locomotor_Entity(locomotion, classname));
}


void FlecsWorldClass::Destroy_Entity(EntityId entity)
{
#ifdef OPENTS_EC_DISABLED
	(void)entity;
#else
	if (entity == 0 || World == nullptr) {
		return;
	}
	flecs::world * world = As_World(World);
	flecs::entity const target = world->entity(entity);
	if (!target.is_alive()) {
		return;
	}
	if (world->is_deferred() && State != nullptr) {
		State->PendingDestroy.insert(entity);
	}
	target.destruct();
#endif
}


void FlecsWorldClass::Set_Entity_Parent(EntityId child, EntityId parent)
{
#ifdef OPENTS_EC_DISABLED
	(void)child;
	(void)parent;
#else
	if (child == 0 || World == nullptr) {
		return;
	}
	flecs::world * world = As_World(World);
	flecs::entity const childentity = world->entity(child);
	if (!childentity.is_alive()) {
		return;
	}
	if (parent == 0) {
		childentity.remove(flecs::ChildOf, flecs::Wildcard);
		return;
	}
	flecs::entity const parententity = world->entity(parent);
	if (parententity.is_alive()) {
		childentity.child_of(parententity);
	}
#endif
}


EntityId FlecsWorldClass::Get_Entity_Parent(EntityId child) const
{
#ifdef OPENTS_EC_DISABLED
	(void)child;
	return(0);
#else
	if (!Entity_Is_Alive(child) || World == nullptr) {
		return(0);
	}
	flecs::entity const parent = As_Const_World(World)->entity(child).parent();
	if (!parent.is_valid() || !parent.is_alive()) {
		return(0);
	}
	return(parent.id());
#endif
}


bool FlecsWorldClass::Entity_Is_Alive(EntityId entity) const
{
#ifdef OPENTS_EC_DISABLED
	(void)entity;
	return(false);
#else
	if (entity == 0 || World == nullptr) {
		return(false);
	}
	if (State != nullptr && State->PendingDestroy.find(entity) != State->PendingDestroy.end()) {
		return(false);
	}
	return(As_Const_World(World)->entity(entity).is_alive());
#endif
}


bool FlecsWorldClass::Entity_Has_Tag(EntityId entity, char const * classname) const
{
#ifdef OPENTS_EC_DISABLED
	(void)entity;
	(void)classname;
	return(false);
#else
	if (!Entity_Is_Alive(entity) || World == nullptr || classname == nullptr) {
		return(false);
	}
	flecs::world const * world = As_Const_World(World);
	flecs::entity const tag = world->lookup(classname);
	if (!tag) {
		return(false);
	}
	return(world->entity(entity).has(tag));
#endif
}


void * FlecsWorldClass::Get_Entity_Object(EntityId entity) const
{
#ifdef OPENTS_EC_DISABLED
	(void)entity;
	return(nullptr);
#else
	if (!Entity_Is_Alive(entity) || World == nullptr) {
		return(nullptr);
	}
	flecs::entity const handle = As_Const_World(World)->entity(entity);
	if (LegacyObjectLink const * obj_link = handle.try_get<LegacyObjectLink>()) {
		return(obj_link->Object);
	}
	if (LegacyLocomotionLink const * loco_link = handle.try_get<LegacyLocomotionLink>()) {
		return(loco_link->Locomotion);
	}
	return(nullptr);
#endif
}


int FlecsWorldClass::Live_Entity_Count(void) const
{
	return(LiveCount);
}


void FlecsWorldClass::Set_Object_Inspector(ObjectInspectorFn inspector)
{
	Ensure_State().Inspector = inspector;
}


ObjectInspectorFn FlecsWorldClass::Get_Object_Inspector(void) const
{
	if (State == nullptr) {
		return(nullptr);
	}
	return(State->Inspector);
}


void FlecsWorldClass::Register_Component_Raw(
	ComponentTypeKey key,
	char const * name,
	ComponentOps const & ops)
{
#ifdef OPENTS_EC_DISABLED
	(void)key;
	(void)name;
	(void)ops;
#else
	Ensure_World();
	BridgeState & state = Ensure_State();
	flecs::world * world = As_World(World);

	auto existing = state.IndexByKey.find(key);
	if (existing != state.IndexByKey.end()) {
		BridgeState::RegisteredComponent & entry = *state.Components[existing->second];
		if (name != nullptr && *name != '\0' && entry.Name.empty()) {
			entry.Name = name;
			state.IndexByName[entry.Name] = entry.Index;
			if (entry.FlecsId != 0) {
				ecs_set_name(world->c_ptr(), entry.FlecsId, entry.Name.c_str());
			}
		}
		BridgeInternals::Bind_Component_To_World(world, entry);
		return;
	}

	auto entry = std::make_unique<BridgeState::RegisteredComponent>();
	entry->OwnerBridge = this;
	entry->Key = key;
	entry->Name = (name != nullptr) ? name : "";
	entry->Index = static_cast<std::uint32_t>(state.Components.size());
	entry->Ops = ops;
	if (ops.Detach != nullptr) {
		state.HasAnyDetach = true;
	}

	BridgeInternals::Bind_Component_To_World(world, *entry);

	state.IndexByKey[key] = entry->Index;
	if (!entry->Name.empty()) {
		state.IndexByName[entry->Name] = entry->Index;
	}
	state.Components.push_back(std::move(entry));
#endif
}


bool FlecsWorldClass::Is_Component_Registered(char const * name) const
{
#ifdef OPENTS_EC_DISABLED
	(void)name;
	return(false);
#else
	if (State == nullptr || name == nullptr) {
		return(false);
	}
	return(State->IndexByName.find(name) != State->IndexByName.end());
#endif
}


void * FlecsWorldClass::Ensure_Component_Raw(
	EntityId entity,
	ComponentTypeKey key,
	ComponentOps const & ops,
	bool & is_new)
{
#ifdef OPENTS_EC_DISABLED
	(void)entity;
	(void)key;
	(void)ops;
	is_new = false;
	return(nullptr);
#else
	is_new = false;
	if (!Entity_Is_Alive(entity) || World == nullptr) {
		return(nullptr);
	}

	Register_Component_Raw(key, nullptr, ops);
	BridgeState & state = Ensure_State();
	flecs::world * world = As_World(World);
	BridgeState::RegisteredComponent & entry = *state.Components[state.IndexByKey[key]];
	BridgeInternals::Bind_Component_To_World(world, entry);

	is_new = !ecs_has_id(world->c_ptr(), entity, entry.FlecsId);
	return(ecs_ensure_id(world->c_ptr(), entity, entry.FlecsId, entry.Ops.Size));
#endif
}


void FlecsWorldClass::Finalize_Component_Write_Raw(
	EntityId entity,
	ComponentTypeKey key,
	void * slot,
	bool is_new)
{
#ifdef OPENTS_EC_DISABLED
	(void)entity;
	(void)key;
	(void)slot;
	(void)is_new;
#else
	if (World == nullptr || State == nullptr || slot == nullptr) {
		return;
	}

	flecs::world * world = As_World(World);
	BridgeState::RegisteredComponent & entry = *State->Components[State->IndexByKey[key]];
	ecs_modified_id(world->c_ptr(), entity, entry.FlecsId);

	if (!is_new || entry.Ops.OnReady == nullptr || State->InSaveLoad) {
		return;
	}

	if (State->ScenarioLoading || world->is_deferred()) {
		State->PendingReady.push_back({ entity, entry.Index });
		return;
	}

	EntityContext const context = BridgeInternals::Build_Entity_Context(this, world->c_ptr(), State->Inspector, entity);
	entry.Ops.OnReady(slot, context);
#endif
}


void * FlecsWorldClass::Get_Component_Raw(EntityId entity, ComponentTypeKey key)
{
#ifdef OPENTS_EC_DISABLED
	(void)entity;
	(void)key;
	return(nullptr);
#else
	if (!Entity_Is_Alive(entity) || World == nullptr || State == nullptr) {
		return(nullptr);
	}
	auto found = State->IndexByKey.find(key);
	if (found == State->IndexByKey.end()) {
		return(nullptr);
	}
	BridgeState::RegisteredComponent const & entry = *State->Components[found->second];
	if (entry.FlecsId == 0 || !ecs_has_id(As_World(World)->c_ptr(), entity, entry.FlecsId)) {
		return(nullptr);
	}
	return(ecs_get_mut_id(As_World(World)->c_ptr(), entity, entry.FlecsId));
#endif
}


void const * FlecsWorldClass::Try_Get_Component_Raw(EntityId entity, ComponentTypeKey key) const
{
#ifdef OPENTS_EC_DISABLED
	(void)entity;
	(void)key;
	return(nullptr);
#else
	if (!Entity_Is_Alive(entity) || World == nullptr || State == nullptr) {
		return(nullptr);
	}
	auto found = State->IndexByKey.find(key);
	if (found == State->IndexByKey.end()) {
		return(nullptr);
	}
	BridgeState::RegisteredComponent const & entry = *State->Components[found->second];
	if (entry.FlecsId == 0) {
		return(nullptr);
	}
	return(ecs_get_id(As_Const_World(World)->c_ptr(), entity, entry.FlecsId));
#endif
}


bool FlecsWorldClass::Has_Component_Raw(EntityId entity, ComponentTypeKey key) const
{
#ifdef OPENTS_EC_DISABLED
	(void)entity;
	(void)key;
	return(false);
#else
	if (!Entity_Is_Alive(entity) || World == nullptr || State == nullptr) {
		return(false);
	}
	auto found = State->IndexByKey.find(key);
	if (found == State->IndexByKey.end()) {
		return(false);
	}
	BridgeState::RegisteredComponent const & entry = *State->Components[found->second];
	if (entry.FlecsId == 0) {
		return(false);
	}
	return(ecs_has_id(As_Const_World(World)->c_ptr(), entity, entry.FlecsId));
#endif
}


void FlecsWorldClass::Remove_Component_Raw(EntityId entity, ComponentTypeKey key)
{
#ifdef OPENTS_EC_DISABLED
	(void)entity;
	(void)key;
#else
	if (!Entity_Is_Alive(entity) || World == nullptr || State == nullptr) {
		return;
	}
	auto found = State->IndexByKey.find(key);
	if (found == State->IndexByKey.end()) {
		return;
	}
	BridgeState::RegisteredComponent const & entry = *State->Components[found->second];
	if (entry.FlecsId != 0 && ecs_has_id(As_World(World)->c_ptr(), entity, entry.FlecsId)) {
		ecs_remove_id(As_World(World)->c_ptr(), entity, entry.FlecsId);
	}
#endif
}


void FlecsWorldClass::Begin_Scenario_Load(void)
{
#ifndef OPENTS_EC_DISABLED
	Ensure_State().ScenarioLoading = true;
#endif
}


bool FlecsWorldClass::Is_Scenario_Loading(void) const
{
#ifdef OPENTS_EC_DISABLED
	return(false);
#else
	return(State != nullptr && State->ScenarioLoading);
#endif
}


void FlecsWorldClass::Drain_Pending_Ready(void)
{
#ifndef OPENTS_EC_DISABLED
	if (World == nullptr || State == nullptr || State->ScenarioLoading) {
		return;
	}

	flecs::world * world = As_World(World);
	if (world->is_deferred()) {
		return;
	}

	struct SortedReadyItem {
		std::uint32_t ComponentIndex = 0;
		CanonicalSortKey SortKey;
		EntityId Entity = 0;

		bool operator<(SortedReadyItem const & other) const noexcept
		{
			if (ComponentIndex != other.ComponentIndex) {
				return(ComponentIndex < other.ComponentIndex);
			}
			return(SortKey < other.SortKey);
		}
	};

	while (!State->PendingReady.empty()) {
		std::vector<BridgeState::PendingReadyItem> batch;
		batch.swap(State->PendingReady);

		std::vector<SortedReadyItem> sorted;
		sorted.reserve(batch.size());
		for (BridgeState::PendingReadyItem const & raw : batch) {
			if (!Entity_Is_Alive(raw.Entity)) {
				continue;
			}
			BridgeState::RegisteredComponent const & entry = *State->Components[raw.ComponentIndex];
			if (entry.FlecsId == 0 || !ecs_has_id(world->c_ptr(), raw.Entity, entry.FlecsId)) {
				continue;
			}
			CanonicalSortKey sort_key;
			BridgeInternals::Build_Entity_Context(this, world->c_ptr(), State->Inspector, raw.Entity, nullptr, &sort_key);
			sorted.push_back({ raw.ComponentIndex, sort_key, raw.Entity });
		}

		std::sort(sorted.begin(), sorted.end());

		world->defer_begin();
		for (SortedReadyItem const & item : sorted) {
			if (!Entity_Is_Alive(item.Entity)) {
				continue;
			}
			BridgeState::RegisteredComponent const & entry = *State->Components[item.ComponentIndex];
			void * slot = ecs_get_mut_id(world->c_ptr(), item.Entity, entry.FlecsId);
			if (slot != nullptr && entry.Ops.OnReady != nullptr) {
				EntityContext const context = BridgeInternals::Build_Entity_Context(this, world->c_ptr(), State->Inspector, item.Entity);
				entry.Ops.OnReady(slot, context);
			}
		}
		world->defer_end();
		State->PendingDestroy.clear();
	}
#endif
}


void FlecsWorldClass::Flush_Scenario_Ready(void)
{
#ifndef OPENTS_EC_DISABLED
	if (State == nullptr) {
		return;
	}
	State->ScenarioLoading = false;
	Drain_Pending_Ready();
#endif
}


void FlecsWorldClass::Notify_Enter_World(EntityId entity)
{
#ifdef OPENTS_EC_DISABLED
	(void)entity;
#else
	if (!Entity_Is_Alive(entity) || World == nullptr || State == nullptr) {
		return;
	}

	flecs::world * world = As_World(World);
	bool const nested_defer = world->is_deferred();
	if (!nested_defer) {
		world->defer_begin();
	}

	for (std::unique_ptr<BridgeState::RegisteredComponent> const & entry_ptr : State->Components) {
		if (!Entity_Is_Alive(entity)) {
			break;
		}
		BridgeState::RegisteredComponent const & entry = *entry_ptr;
		if (entry.Ops.OnEnterWorld == nullptr || entry.FlecsId == 0) {
			continue;
		}
		if (!ecs_has_id(world->c_ptr(), entity, entry.FlecsId)) {
			continue;
		}
		void * slot = ecs_get_mut_id(world->c_ptr(), entity, entry.FlecsId);
		if (slot != nullptr) {
			EntityContext const context = BridgeInternals::Build_Entity_Context(this, world->c_ptr(), State->Inspector, entity);
			entry.Ops.OnEnterWorld(slot, context);
		}
	}

	if (!nested_defer) {
		world->defer_end();
		State->PendingDestroy.clear();
		Drain_Pending_Ready();
	}
#endif
}


void FlecsWorldClass::Notify_Exit_World(EntityId entity)
{
#ifdef OPENTS_EC_DISABLED
	(void)entity;
#else
	if (!Entity_Is_Alive(entity) || World == nullptr || State == nullptr) {
		return;
	}

	flecs::world * world = As_World(World);
	bool const nested_defer = world->is_deferred();
	if (!nested_defer) {
		world->defer_begin();
	}

	for (std::size_t idx = State->Components.size(); idx > 0; --idx) {
		if (!Entity_Is_Alive(entity)) {
			break;
		}
		BridgeState::RegisteredComponent const & entry = *State->Components[idx - 1];
		if (entry.Ops.OnExitWorld == nullptr || entry.FlecsId == 0) {
			continue;
		}
		if (!ecs_has_id(world->c_ptr(), entity, entry.FlecsId)) {
			continue;
		}
		void * slot = ecs_get_mut_id(world->c_ptr(), entity, entry.FlecsId);
		if (slot != nullptr) {
			EntityContext const context = BridgeInternals::Build_Entity_Context(this, world->c_ptr(), State->Inspector, entity);
			entry.Ops.OnExitWorld(slot, context);
		}
	}

	if (!nested_defer) {
		world->defer_end();
		State->PendingDestroy.clear();
		Drain_Pending_Ready();
	}
#endif
}


void FlecsWorldClass::Run_Sim_Phase(SimPhase phase)
{
#ifdef OPENTS_EC_DISABLED
	(void)phase;
#else
	if (World == nullptr || State == nullptr) {
		return;
	}

	struct PhaseRow {
		CanonicalSortKey SortKey;
		EntityId Entity = 0;
		void * ComponentPtr = nullptr;

		bool operator<(PhaseRow const & other) const noexcept
		{
			return(SortKey < other.SortKey);
		}
	};

	flecs::world * world = As_World(World);
	std::vector<PhaseRow> rows;

	for (std::size_t comp_idx = 0; comp_idx < State->Components.size(); ++comp_idx) {
		BridgeState::RegisteredComponent const & entry = *State->Components[comp_idx];
		if (entry.Ops.Process == nullptr || entry.Ops.Phase != phase || entry.Query == nullptr) {
			continue;
		}

		rows.clear();
		ecs_iter_t it = ecs_query_iter(world->c_ptr(), entry.Query);
		while (ecs_query_next(&it)) {
			unsigned char * base = static_cast<unsigned char *>(ecs_field_w_size(&it, entry.Ops.Size, 0));
			for (int32_t i = 0; i < it.count; ++i) {
				EntityId const entity_id = it.entities[i];
				ObjectStateInfo info;
				CanonicalSortKey sort_key;
				BridgeInternals::Build_Entity_Context(this, world->c_ptr(), State->Inspector, entity_id, &info, &sort_key);
				if (!info.IsActive) {
					continue;
				}
				if (entry.Ops.Scope == TickScope::WorldActive && info.IsInLimbo) {
					continue;
				}
				rows.push_back({
					sort_key,
					entity_id,
					base + static_cast<std::size_t>(i) * entry.Ops.Size,
				});
			}
		}

		if (rows.empty()) {
			continue;
		}

		std::sort(rows.begin(), rows.end());

		world->defer_begin();
		for (PhaseRow const & row : rows) {
			if (!Entity_Is_Alive(row.Entity)) {
				continue;
			}
			ObjectStateInfo live_info;
			EntityContext const context = BridgeInternals::Build_Entity_Context(
				this,
				world->c_ptr(),
				State->Inspector,
				row.Entity,
				&live_info);
			if (!live_info.IsActive) {
				continue;
			}
			if (entry.Ops.Scope == TickScope::WorldActive && live_info.IsInLimbo) {
				continue;
			}
			entry.Ops.Process(row.ComponentPtr, context);
		}
		world->defer_end();
		State->PendingDestroy.clear();
		Drain_Pending_Ready();
	}
#endif
}


void FlecsWorldClass::Render_Foreground(void * surface) const
{
#ifdef OPENTS_EC_DISABLED
	(void)surface;
#else
	if (World == nullptr || State == nullptr) {
		return;
	}

	struct RenderRow {
		CanonicalSortKey SortKey;
		EntityId Entity = 0;
		void const * ComponentPtr = nullptr;

		bool operator<(RenderRow const & other) const noexcept
		{
			return(SortKey < other.SortKey);
		}
	};

	flecs::world const * world = As_Const_World(World);
	std::vector<RenderRow> rows;

	for (std::unique_ptr<BridgeState::RegisteredComponent> const & entry_ptr : State->Components) {
		BridgeState::RegisteredComponent const & entry = *entry_ptr;
		if (entry.Ops.RenderForeground == nullptr || entry.Query == nullptr) {
			continue;
		}

		rows.clear();
		ecs_iter_t it = ecs_query_iter(As_World(World)->c_ptr(), entry.Query);
		while (ecs_query_next(&it)) {
			unsigned char const * base = static_cast<unsigned char const *>(ecs_field_w_size(&it, entry.Ops.Size, 0));
			for (int32_t i = 0; i < it.count; ++i) {
				EntityId const entity_id = it.entities[i];
				if (!Entity_Is_Alive(entity_id)) {
					continue;
				}
				ObjectStateInfo info;
				CanonicalSortKey sort_key;
				BridgeInternals::Build_Entity_Context(
					const_cast<FlecsWorldClass *>(this),
					world->c_ptr(),
					State->Inspector,
					entity_id,
					&info,
					&sort_key);
				if (!info.IsActive || info.IsInLimbo) {
					continue;
				}
				rows.push_back({
					sort_key,
					entity_id,
					base + static_cast<std::size_t>(i) * entry.Ops.Size,
				});
			}
		}

		std::sort(rows.begin(), rows.end());
		for (RenderRow const & row : rows) {
			EntityContext const context = BridgeInternals::Build_Entity_Context(
				const_cast<FlecsWorldClass *>(this),
				world->c_ptr(),
				State->Inspector,
				row.Entity);
			entry.Ops.RenderForeground(row.ComponentPtr, context, surface);
		}
	}
#endif
}


void FlecsWorldClass::Detach_Target(AbstractClass const * target, bool all)
{
#ifdef OPENTS_EC_DISABLED
	(void)target;
	(void)all;
#else
	if (target == nullptr || World == nullptr || State == nullptr || !State->HasAnyDetach) {
		return;
	}

	flecs::world * world = As_World(World);
	bool const nested_defer = world->is_deferred();
	if (!nested_defer) {
		world->defer_begin();
	}

	for (std::size_t comp_idx = 0; comp_idx < State->Components.size(); ++comp_idx) {
		BridgeState::RegisteredComponent const & entry = *State->Components[comp_idx];
		if (entry.Ops.Detach == nullptr || entry.Query == nullptr) {
			continue;
		}

		ecs_iter_t it = ecs_query_iter(world->c_ptr(), entry.Query);
		while (ecs_query_next(&it)) {
			unsigned char * base = static_cast<unsigned char *>(ecs_field_w_size(&it, entry.Ops.Size, 0));
			for (int32_t i = 0; i < it.count; ++i) {
				EntityId const entity_id = it.entities[i];
				if (!Entity_Is_Alive(entity_id)) {
					continue;
				}
				EntityContext const context = BridgeInternals::Build_Entity_Context(this, world->c_ptr(), State->Inspector, entity_id);
				entry.Ops.Detach(base + static_cast<std::size_t>(i) * entry.Ops.Size, context, target, all);
			}
		}
	}

	if (!nested_defer) {
		world->defer_end();
		State->PendingDestroy.clear();
		Drain_Pending_Ready();
	}
#endif
}


void FlecsWorldClass::Fold_CRCs(CRCEngine & crc) const
{
#ifdef OPENTS_EC_DISABLED
	(void)crc;
#else
	if (World == nullptr || State == nullptr) {
		return;
	}

	struct CrcRow {
		CanonicalSortKey SortKey;
		EntityId Entity = 0;
		void const * ComponentPtr = nullptr;

		bool operator<(CrcRow const & other) const noexcept
		{
			return(SortKey < other.SortKey);
		}
	};

	flecs::world const * world = As_Const_World(World);
	std::vector<CrcRow> rows;

	for (std::unique_ptr<BridgeState::RegisteredComponent> const & entry_ptr : State->Components) {
		BridgeState::RegisteredComponent const & entry = *entry_ptr;
		if (entry.Ops.ComputeCRC == nullptr || entry.Query == nullptr) {
			continue;
		}

		rows.clear();
		ecs_iter_t it = ecs_query_iter(As_World(World)->c_ptr(), entry.Query);
		while (ecs_query_next(&it)) {
			unsigned char const * base = static_cast<unsigned char const *>(ecs_field_w_size(&it, entry.Ops.Size, 0));
			for (int32_t i = 0; i < it.count; ++i) {
				EntityId const entity_id = it.entities[i];
				if (!Entity_Is_Alive(entity_id)) {
					continue;
				}
				ObjectStateInfo info;
				CanonicalSortKey sort_key;
				BridgeInternals::Build_Entity_Context(
					const_cast<FlecsWorldClass *>(this),
					world->c_ptr(),
					State->Inspector,
					entity_id,
					&info,
					&sort_key);
				if (!info.IsActive) {
					continue;
				}
				rows.push_back({
					sort_key,
					entity_id,
					base + static_cast<std::size_t>(i) * entry.Ops.Size,
				});
			}
		}

		std::sort(rows.begin(), rows.end());
		for (CrcRow const & row : rows) {
			EntityContext const context = BridgeInternals::Build_Entity_Context(
				const_cast<FlecsWorldClass *>(this),
				world->c_ptr(),
				State->Inspector,
				row.Entity);
			entry.Ops.ComputeCRC(row.ComponentPtr, context, crc);
		}
	}
#endif
}


bool FlecsWorldClass::Serialize_Entity_Raw(EntityId entity, ComponentStreamAdapter const & io)
{
	if (io.WasError(io.Stream)) {
		return(false);
	}

#ifdef OPENTS_EC_DISABLED
	(void)entity;
	if (io.IsSaving(io.Stream)) {
		std::uint32_t count = 0;
		io.SerializeBytes(io.Stream, &count, sizeof(count));
		return(!io.WasError(io.Stream));
	}
	if (io.IsLoading(io.Stream)) {
		std::uint32_t count = 0;
		io.SerializeBytes(io.Stream, &count, sizeof(count));
		if (io.WasError(io.Stream)) {
			return(false);
		}
		std::uint64_t total_payload = 0;
		for (std::uint32_t i = 0; i < count; ++i) {
			std::uint32_t name_len = 0;
			io.SerializeBytes(io.Stream, &name_len, sizeof(name_len));
			if (io.WasError(io.Stream) || name_len > 4096 || !Skip_Stream_Bytes(io, name_len)) {
				io.Fail(io.Stream);
				return(false);
			}
			std::uint32_t payload_size = 0;
			io.SerializeBytes(io.Stream, &payload_size, sizeof(payload_size));
			if (io.WasError(io.Stream)) {
				return(false);
			}
			total_payload += payload_size;
		}
		if (total_payload > static_cast<std::uint64_t>(io.Size(io.Stream) - io.Offset(io.Stream))) {
			io.Fail(io.Stream);
			return(false);
		}
		return(Skip_Stream_Bytes(io, static_cast<std::uint32_t>(total_payload)));
	}
	return(false);
#else
	if (io.IsSaving(io.Stream)) {
		struct SaveItem {
			BridgeState::RegisteredComponent const * Entry = nullptr;
			void * Ptr = nullptr;
			unsigned int SizeFieldOffset = 0;
		};

		std::vector<SaveItem> items;
		if (Entity_Is_Alive(entity) && World != nullptr && State != nullptr) {
			flecs::world * world = As_World(World);
			for (std::unique_ptr<BridgeState::RegisteredComponent> const & entry_ptr : State->Components) {
				BridgeState::RegisteredComponent const & entry = *entry_ptr;
				if (entry.Ops.Serialize == nullptr || entry.Name.empty() || entry.FlecsId == 0) {
					continue;
				}
				if (!ecs_has_id(world->c_ptr(), entity, entry.FlecsId)) {
					continue;
				}
				void * ptr = ecs_get_mut_id(world->c_ptr(), entity, entry.FlecsId);
				if (ptr != nullptr) {
					items.push_back({ &entry, ptr, 0 });
				}
			}
		}

		std::uint32_t count = static_cast<std::uint32_t>(items.size());
		io.SerializeBytes(io.Stream, &count, sizeof(count));
		if (io.WasError(io.Stream)) {
			return(false);
		}

		for (SaveItem & item : items) {
			std::uint32_t name_len = static_cast<std::uint32_t>(item.Entry->Name.size());
			io.SerializeBytes(io.Stream, &name_len, sizeof(name_len));
			if (name_len > 0) {
				io.SerializeBytes(io.Stream, const_cast<char *>(item.Entry->Name.data()), static_cast<int>(name_len));
			}
			item.SizeFieldOffset = io.Offset(io.Stream);
			std::uint32_t placeholder_size = 0;
			io.SerializeBytes(io.Stream, &placeholder_size, sizeof(placeholder_size));
			if (io.WasError(io.Stream)) {
				return(false);
			}
		}

		for (SaveItem const & item : items) {
			unsigned int const payload_begin = io.Offset(io.Stream);
			item.Entry->Ops.Serialize(item.Ptr, *io.Stream);
			if (io.WasError(io.Stream)) {
				return(false);
			}
			std::uint32_t const payload_size = io.Offset(io.Stream) - payload_begin;
			io.OverwriteBytes(io.Stream, item.SizeFieldOffset, &payload_size, sizeof(payload_size));
		}
		return(!io.WasError(io.Stream));
	}

	if (io.IsLoading(io.Stream)) {
		struct LoadHeader {
			BridgeState::RegisteredComponent * Entry = nullptr;
			std::uint32_t PayloadSize = 0;
		};

		std::uint32_t count = 0;
		io.SerializeBytes(io.Stream, &count, sizeof(count));
		if (io.WasError(io.Stream)) {
			return(false);
		}
		unsigned int const remaining_after_count = io.Size(io.Stream) - io.Offset(io.Stream);
		if (static_cast<std::uint64_t>(count) * 8ULL > remaining_after_count) {
			io.Fail(io.Stream);
			return(false);
		}

		std::vector<LoadHeader> headers;
		headers.reserve(count);
		std::uint64_t total_payload = 0;
		std::string name_buf;

		for (std::uint32_t i = 0; i < count; ++i) {
			std::uint32_t name_len = 0;
			io.SerializeBytes(io.Stream, &name_len, sizeof(name_len));
			if (io.WasError(io.Stream) || name_len > 4096) {
				io.Fail(io.Stream);
				return(false);
			}
			if (name_len > io.Size(io.Stream) - io.Offset(io.Stream)) {
				io.Fail(io.Stream);
				return(false);
			}
			name_buf.resize(name_len);
			if (name_len > 0) {
				io.SerializeBytes(io.Stream, name_buf.data(), static_cast<int>(name_len));
			}

			std::uint32_t payload_size = 0;
			io.SerializeBytes(io.Stream, &payload_size, sizeof(payload_size));
			if (io.WasError(io.Stream)) {
				return(false);
			}

			BridgeState::RegisteredComponent * entry = nullptr;
			if (State != nullptr) {
				auto found = State->IndexByName.find(name_buf);
				if (found != State->IndexByName.end()) {
					entry = State->Components[found->second].get();
				}
			}

			headers.push_back({ entry, payload_size });
			total_payload += payload_size;
		}

		if (total_payload > static_cast<std::uint64_t>(io.Size(io.Stream) - io.Offset(io.Stream))) {
			io.Fail(io.Stream);
			return(false);
		}

		bool const can_attach = Entity_Is_Alive(entity) && World != nullptr && State != nullptr;
		if (can_attach) {
			Ensure_World();
			flecs::world * world = As_World(World);
			State->InSaveLoad = true;
			// Add all known components upfront so the entity settles in its
			// final target archetype before any payload deserializes into it.
			for (LoadHeader & header : headers) {
				if (header.Entry != nullptr && header.Entry->Ops.Serialize != nullptr) {
					BridgeInternals::Bind_Component_To_World(world, *header.Entry);
					(void)ecs_ensure_id(
						world->c_ptr(),
						entity,
						header.Entry->FlecsId,
						header.Entry->Ops.Size);
				}
			}
		}

		for (LoadHeader const & header : headers) {
			unsigned int const expected_end = io.Offset(io.Stream) + header.PayloadSize;
			if (can_attach && header.Entry != nullptr && header.Entry->Ops.Serialize != nullptr) {
				flecs::world * world = As_World(World);
				void * slot = ecs_get_mut_id(world->c_ptr(), entity, header.Entry->FlecsId);
				header.Entry->Ops.Serialize(slot, *io.Stream);
				if (io.WasError(io.Stream) || io.Offset(io.Stream) > expected_end) {
					if (State != nullptr) {
						State->InSaveLoad = false;
					}
					io.Fail(io.Stream);
					return(false);
				}
				unsigned int const leftover = expected_end - io.Offset(io.Stream);
				if (leftover > 0 && !Skip_Stream_Bytes(io, leftover)) {
					if (State != nullptr) {
						State->InSaveLoad = false;
					}
					return(false);
				}
				ecs_modified_id(world->c_ptr(), entity, header.Entry->FlecsId);
			} else {
				if (!Skip_Stream_Bytes(io, header.PayloadSize)) {
					if (State != nullptr) {
						State->InSaveLoad = false;
					}
					return(false);
				}
			}
		}

		if (State != nullptr) {
			State->InSaveLoad = false;
		}
		return(!io.WasError(io.Stream));
	}

	io.Fail(io.Stream);
	return(false);
#endif
}


void FlecsWorldClass::Post_Load_Resolve_All(void)
{
#ifndef OPENTS_EC_DISABLED
	if (World == nullptr || State == nullptr) {
		return;
	}

	struct ResolveRow {
		CanonicalSortKey SortKey;
		EntityId Entity = 0;
		void * ComponentPtr = nullptr;

		bool operator<(ResolveRow const & other) const noexcept
		{
			return(SortKey < other.SortKey);
		}
	};

	flecs::world * world = As_World(World);
	std::vector<ResolveRow> rows;

	for (std::size_t comp_idx = 0; comp_idx < State->Components.size(); ++comp_idx) {
		BridgeState::RegisteredComponent const & entry = *State->Components[comp_idx];
		if (entry.Ops.PostLoadResolve == nullptr || entry.Query == nullptr) {
			continue;
		}

		rows.clear();
		ecs_iter_t it = ecs_query_iter(world->c_ptr(), entry.Query);
		while (ecs_query_next(&it)) {
			unsigned char * base = static_cast<unsigned char *>(ecs_field_w_size(&it, entry.Ops.Size, 0));
			for (int32_t i = 0; i < it.count; ++i) {
				EntityId const entity_id = it.entities[i];
				if (!Entity_Is_Alive(entity_id)) {
					continue;
				}
				CanonicalSortKey sort_key;
				BridgeInternals::Build_Entity_Context(this, world->c_ptr(), State->Inspector, entity_id, nullptr, &sort_key);
				rows.push_back({
					sort_key,
					entity_id,
					base + static_cast<std::size_t>(i) * entry.Ops.Size,
				});
			}
		}

		std::sort(rows.begin(), rows.end());
		world->defer_begin();
		for (ResolveRow const & row : rows) {
			if (!Entity_Is_Alive(row.Entity)) {
				continue;
			}
			EntityContext const context = BridgeInternals::Build_Entity_Context(this, world->c_ptr(), State->Inspector, row.Entity);
			entry.Ops.PostLoadResolve(row.ComponentPtr, context);
		}
		world->defer_end();
		State->PendingDestroy.clear();
		Drain_Pending_Ready();
	}
#endif
}


void FlecsWorldClass::Reset(void)
{
	if (State != nullptr) {
		State->PendingReady.clear();
		State->PendingDestroy.clear();
		State->ScenarioLoading = false;
		State->InSaveLoad = false;
		State->NextAttachSequence = 1;
	}

	if (World == nullptr) {
		LiveCount = 0;
		return;
	}

	flecs::world * world = As_World(World);
	std::vector<flecs::entity> entities;
	world->query<LegacyObjectLink>().each([&entities](flecs::entity entity, LegacyObjectLink &) {
		entities.push_back(entity);
	});
	world->query<LegacyLocomotionLink>().each([&entities](flecs::entity entity, LegacyLocomotionLink &) {
		entities.push_back(entity);
	});
	for (flecs::entity const entity : entities) {
		if (entity.is_alive()) {
			entity.destruct();
		}
	}
	LiveCount = 0;
}


void FlecsWorldClass::Shutdown(void)
{
	if (State != nullptr) {
		State->PendingReady.clear();
		State->PendingDestroy.clear();
		State->ScenarioLoading = false;
		State->InSaveLoad = false;
		State->NextAttachSequence = 1;
		for (std::unique_ptr<BridgeState::RegisteredComponent> & entry : State->Components) {
			if (entry->Query != nullptr) {
				ecs_query_fini(entry->Query);
				entry->Query = nullptr;
			}
			entry->FlecsId = 0;
		}
	}

	delete As_World(World);
	World = nullptr;
	LiveCount = 0;
}
