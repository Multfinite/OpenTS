/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include "flecs/id.hh"

// The EC bridge. No flecs type appears in these signatures, so files that call
// the bridge do not include flecs headers. The world registers no system,
// pipeline, phase, or observer; nothing advances it implicitly.

// Creates the entity for one game object and returns it, or zero when the
// EC build is disabled. A live entity the object already holds is destroyed
// and replaced, which is what placement reconstruction of a cell needs.
EntityId Attach_Entity(void * object, char const * classname);

// Creates the child entity for one locomotor. Link_To_Object parents it to
// its owner's entity; an unparented locomotor entity is still valid.
EntityId Attach_Locomotor_Entity(void * locomotion, char const * classname);

// Destroys the entity the object already holds, if any, then attaches a fresh
// one and returns it. Leaf constructors assign the result to their Entity
// member, so placement reconstruction replaces rather than leaks.
EntityId Replace_Entity(EntityId current, void * object, char const * classname);
EntityId Replace_Locomotor_Entity(EntityId current, void * locomotion, char const * classname);

// Destroys the entity, removing all of its components and pairs. Zero and
// already-destroyed entities are accepted.
void Destroy_Entity(EntityId entity);

// Parents a child entity to a parent entity. Zero or dead entities are
// accepted and do nothing, so an owner without an entity is not an error.
void Set_Entity_Parent(EntityId child, EntityId parent);

bool Entity_Is_Alive(EntityId entity);
bool Entity_Has_Tag(EntityId entity, char const * classname);

// The number of live entities the bridge has created and not yet destroyed.
int Live_Entity_Count(void);

// Destroys every entity the bridge created. Scenario teardown already deletes
// every game object, which destroys its entity; this is for tests and for
// checking that teardown left nothing behind.
void Reset_Flecs_World(void);

// Destroys the world. Called from the game's exit path; attach recreates the
// world on demand, so globals destroyed after shutdown cannot crash on it.
void Shutdown_Flecs_World(void);

// Registers one class tag with the world. register.cpp calls this for every
// class in the rollout table before the first entity is created.
void Register_Class_Tag(char const * classname);

// Registers every class tag. world.cpp calls this once when the world is
// created; registration is explicit and ordered, with no static initializers.
void Register_Flecs_Tags(void);
