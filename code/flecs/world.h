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

// The EC layer as one class with one global object, the way LogicClass and
// MapClass are held. No flecs type appears in this header, so files that use
// the layer do not include flecs headers. The world registers no system,
// pipeline, phase, or observer; nothing advances it implicitly.
class FlecsWorldClass
{
	public:
		FlecsWorldClass(void);
		~FlecsWorldClass(void);

		// Creates the entity for one game object and returns it, or zero when
		// the EC build is disabled.
		EntityId Attach_Entity(void * object, char const * classname);

		// Creates the child entity for one locomotor. Link_To_Object parents
		// it to its owner's entity; an unparented locomotor entity is valid.
		EntityId Attach_Locomotor_Entity(void * locomotion, char const * classname);

		// Destroys the entity the object already holds, if any, then attaches
		// a fresh one and returns it. Leaf constructors assign the result to
		// their Entity member, so placement reconstruction of a cell replaces
		// the old entity rather than leaking it.
		EntityId Replace_Entity(EntityId current, void * object, char const * classname);
		EntityId Replace_Locomotor_Entity(EntityId current, void * locomotion, char const * classname);

		// Destroys the entity, removing all of its components and pairs. Zero
		// and already-destroyed entities are accepted.
		void Destroy_Entity(EntityId entity);

		// Parents a child entity to a parent entity. Zero or dead entities
		// are accepted and do nothing, so an owner without an entity is not
		// an error.
		void Set_Entity_Parent(EntityId child, EntityId parent);

		bool Entity_Is_Alive(EntityId entity);
		bool Entity_Has_Tag(EntityId entity, char const * classname);

		// The number of live entities this world has created and not yet
		// destroyed.
		int Live_Entity_Count(void) const;

		// Destroys every entity this world created. Scenario teardown already
		// deletes every game object, which destroys its entity; this is for
		// tests and for checking that teardown left nothing behind.
		void Reset(void);

		// Destroys the flecs world itself. Called from the game's exit path;
		// attach recreates the world on demand, so a global destroyed after
		// shutdown cannot crash on it.
		void Shutdown(void);

	private:
		// Creates the flecs world and registers the bridge components and
		// every class tag. The world is created on first use because the
		// CellClass BlubCell global attaches before any startup code runs.
		void Ensure_World(void);

		EntityId Attach(void * object, char const * classname, bool locomotion);
		void Register_Tag(char const * classname);

		// Defined in register.cpp, which owns the class list, so this list
		// and the rollout table in the adoption plan are checked against
		// each other by the entityinventory test.
		void Register_Tags(void);

	private:
		// The flecs::world, held as void * so this header needs no flecs
		// include. Null until the first attach creates it.
		void * World;

		int LiveCount;
};

extern FlecsWorldClass FlecsWorld;
