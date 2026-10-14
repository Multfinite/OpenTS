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

#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

flecs::world * World = nullptr;
bool Registering = false;
int LiveCount = 0;
std::unordered_map<std::string, flecs::entity> ClassTags;

flecs::world & Ensure_World(void)
{
	if (World == nullptr) {
		World = new flecs::world();
		World->component<LegacyObjectLink>();
		World->component<LegacyLocomotionLink>();
		Registering = true;
		Register_Flecs_Tags();
		Registering = false;
	}
	return(*World);
}

flecs::entity Tag_For(char const * classname)
{
	auto const found = ClassTags.find(classname);
	if (found != ClassTags.end()) {
		return(found->second);
	}
	// A class that reaches attach without a register.cpp entry still gets a
	// tag, named for it, so a missing entry is visible rather than fatal.
	flecs::entity const tag = Ensure_World().entity(classname);
	ClassTags.emplace(classname, tag);
	return(tag);
}

EntityId Attach(void * object, char const * classname, bool locomotion)
{
#ifdef OPENTS_EC_DISABLED
	(void)object;
	(void)classname;
	(void)locomotion;
	return(0);
#else
	flecs::world & world = Ensure_World();
	flecs::entity const entity = world.entity();
	entity.add(Tag_For(classname));
	if (locomotion) {
		entity.set<LegacyLocomotionLink>({ object });
	} else {
		entity.set<LegacyObjectLink>({ object });
	}
	LiveCount++;
	return(entity.id());
#endif
}

}	// namespace


void Register_Class_Tag(char const * classname)
{
	if (!Registering || World == nullptr) {
		return;
	}
	if (ClassTags.find(classname) == ClassTags.end()) {
		ClassTags.emplace(classname, World->entity(classname));
	}
}


EntityId Attach_Entity(void * object, char const * classname)
{
	return(Attach(object, classname, false));
}


EntityId Attach_Locomotor_Entity(void * locomotion, char const * classname)
{
	return(Attach(locomotion, classname, true));
}


EntityId Replace_Entity(EntityId current, void * object, char const * classname)
{
	Destroy_Entity(current);
	return(Attach_Entity(object, classname));
}


EntityId Replace_Locomotor_Entity(EntityId current, void * locomotion, char const * classname)
{
	Destroy_Entity(current);
	return(Attach_Locomotor_Entity(locomotion, classname));
}


void Destroy_Entity(EntityId entity)
{
	if (entity == 0 || World == nullptr) {
		return;
	}
	flecs::entity const target = World->entity(entity);
	if (target.is_alive()) {
		target.destruct();
		LiveCount--;
	}
}


void Set_Entity_Parent(EntityId child, EntityId parent)
{
	if (child == 0 || parent == 0 || World == nullptr) {
		return;
	}
	flecs::entity const childentity = World->entity(child);
	flecs::entity const parententity = World->entity(parent);
	if (childentity.is_alive() && parententity.is_alive()) {
		childentity.child_of(parententity);
	}
}


bool Entity_Is_Alive(EntityId entity)
{
	if (entity == 0 || World == nullptr) {
		return(false);
	}
	return(World->entity(entity).is_alive());
}


bool Entity_Has_Tag(EntityId entity, char const * classname)
{
	if (!Entity_Is_Alive(entity) || World == nullptr) {
		return(false);
	}
	auto const found = ClassTags.find(classname);
	if (found == ClassTags.end()) {
		return(false);
	}
	return(World->entity(entity).has(found->second));
}


int Live_Entity_Count(void)
{
	return(LiveCount);
}


void Reset_Flecs_World(void)
{
	if (World == nullptr) {
		return;
	}
	std::vector<flecs::entity> entities;
	World->query<LegacyObjectLink>().each([&entities](flecs::entity entity, LegacyObjectLink &) {
		entities.push_back(entity);
	});
	World->query<LegacyLocomotionLink>().each([&entities](flecs::entity entity, LegacyLocomotionLink &) {
		entities.push_back(entity);
	});
	for (flecs::entity const entity : entities) {
		if (entity.is_alive()) {
			entity.destruct();
		}
	}
	LiveCount = 0;
}


void Shutdown_Flecs_World(void)
{
	delete World;
	World = nullptr;
	ClassTags.clear();
	LiveCount = 0;
}
