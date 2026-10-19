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

#include <vector>

namespace {

// The world behind the void * the class holds.
flecs::world * As_World(void * world)
{
	return(static_cast<flecs::world *>(world));
}

}	// namespace


// Intentionally does not reset the members: static storage has already
// zeroed them, and a global that attached before this object was constructed
// (the CellClass BlubCell sentinel) has already created the world through
// them. Resetting here would lose that world and its class tags.
FlecsWorldClass::FlecsWorldClass(void)
{
}


FlecsWorldClass::~FlecsWorldClass(void)
{
	Shutdown();
}


void FlecsWorldClass::Ensure_World(void)
{
	if (World == nullptr) {
		flecs::world * world = new flecs::world();
		World = world;
		world->component<LegacyObjectLink>();
		world->component<LegacyLocomotionLink>();
		Register_Tags();
	}
}


void FlecsWorldClass::Register_Tag(char const * classname)
{
	flecs::world * world = As_World(World);
	if (world != nullptr && !world->lookup(classname)) {
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
	flecs::world * world = As_World(World);
	flecs::entity const entity = world->entity();

	flecs::entity const tag = world->lookup(classname);
	if (tag) {
		entity.add(tag);
	}
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
	if (entity == 0 || World == nullptr) {
		return;
	}
	flecs::entity const target = As_World(World)->entity(entity);
	if (target.is_alive()) {
		target.destruct();
		LiveCount--;
	}
}


void FlecsWorldClass::Set_Entity_Parent(EntityId child, EntityId parent)
{
	if (child == 0 || parent == 0 || World == nullptr) {
		return;
	}
	flecs::world * world = As_World(World);
	flecs::entity const childentity = world->entity(child);
	flecs::entity const parententity = world->entity(parent);
	if (childentity.is_alive() && parententity.is_alive()) {
		childentity.child_of(parententity);
	}
}


bool FlecsWorldClass::Entity_Is_Alive(EntityId entity)
{
	if (entity == 0 || World == nullptr) {
		return(false);
	}
	return(As_World(World)->entity(entity).is_alive());
}


bool FlecsWorldClass::Entity_Has_Tag(EntityId entity, char const * classname)
{
	if (!Entity_Is_Alive(entity) || World == nullptr) {
		return(false);
	}
	flecs::world * world = As_World(World);
	flecs::entity const tag = world->lookup(classname);
	if (!tag) {
		return(false);
	}
	return(world->entity(entity).has(tag));
}


int FlecsWorldClass::Live_Entity_Count(void) const
{
	return(LiveCount);
}


void FlecsWorldClass::Reset(void)
{
	if (World == nullptr) {
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
	delete As_World(World);
	World = nullptr;
	LiveCount = 0;
}
