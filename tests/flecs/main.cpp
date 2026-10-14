/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "flecs/world.h"

#include <cstdio>

namespace {

int Failures = 0;

void Check(bool condition, char const * what)
{
	if (!condition) {
		std::printf("FAIL: %s\n", what);
		Failures++;
	}
}

}	// namespace


int main(void)
{
	int unitobject = 0;
	int buildingobject = 0;
	int locomotorobject = 0;

	// Attach creates a tagged, live entity and counts it.
	EntityId const unit = Attach_Entity(&unitobject, "UnitClass");
	Check(unit != 0, "attach returns an entity");
	Check(Entity_Is_Alive(unit), "attached entity is alive");
	Check(Entity_Has_Tag(unit, "UnitClass"), "entity carries its class tag");
	Check(!Entity_Has_Tag(unit, "BuildingClass"), "entity does not carry another class tag");
	Check(Live_Entity_Count() == 1, "one live entity after one attach");

	// A second object of another class is a different entity.
	EntityId const building = Attach_Entity(&buildingobject, "BuildingClass");
	Check(building != 0 && building != unit, "second attach is a distinct entity");
	Check(Live_Entity_Count() == 2, "two live entities after two attaches");

	// Locomotor entities parent to their owner's entity.
	EntityId const locomotor = Attach_Locomotor_Entity(&locomotorobject, "DriveLocomotionClass");
	Check(Entity_Has_Tag(locomotor, "DriveLocomotionClass"), "locomotor carries its class tag");
	Set_Entity_Parent(locomotor, unit);
	Check(Live_Entity_Count() == 3, "locomotor counted");

	// Destroy removes exactly one entity and tolerates repeats and zero.
	Destroy_Entity(building);
	Check(!Entity_Is_Alive(building), "destroyed entity is dead");
	Check(Live_Entity_Count() == 2, "destroy decrements the count");
	Destroy_Entity(building);
	Destroy_Entity(0);
	Check(Live_Entity_Count() == 2, "repeated and zero destroys are no-ops");

	// Replacement destroys the old entity instead of leaking it, which is
	// what placement reconstruction of a cell relies on.
	EntityId const replaced = Replace_Entity(unit, &unitobject, "UnitClass");
	Check(replaced != 0 && replaced != unit, "replace returns a fresh entity");
	Check(!Entity_Is_Alive(unit), "replaced entity is dead");
	Check(Entity_Has_Tag(replaced, "UnitClass"), "replacement carries the tag");
	Check(Live_Entity_Count() == 2, "replace keeps the count");

	// Every registered class in the rollout table gets a working tag.
	static char const * const classes[] = {
		"HouseClass", "CellClass", "BulletClass", "WeaponTypeClass",
		"WalkLocomotionClass", "WaypointPathClass",
	};
	for (char const * const classname : classes) {
		int object = 0;
		EntityId const entity = Attach_Entity(&object, classname);
		Check(Entity_Has_Tag(entity, classname), "registered class tag attaches");
		Destroy_Entity(entity);
	}
	Check(Live_Entity_Count() == 2, "tag sweep leaves the count unchanged");

	// Reset destroys everything the bridge created.
	Reset_Flecs_World();
	Check(Live_Entity_Count() == 0, "reset clears the count");
	Check(!Entity_Is_Alive(replaced), "reset kills remaining entities");

	// Shutdown and recreation on demand work, as globals at exit require.
	Shutdown_Flecs_World();
	EntityId const after = Attach_Entity(&unitobject, "UnitClass");
	Check(Entity_Is_Alive(after), "attach recreates the world after shutdown");
	Shutdown_Flecs_World();

	if (Failures == 0) {
		std::printf("flecs: all checks passed\n");
		return(0);
	}
	std::printf("flecs: %d check(s) failed\n", Failures);
	return(1);
}
