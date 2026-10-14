/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "flecs/world.h"

// Every class that gets an entity, in rollout order. Excluded classes
// (AnimClass, IsometricTileClass, LogicClass, TActionClass, TEventClass)
// and the undecided Tactical are absent on purpose; the inventory check
// in tests/flecs keeps this list matched to the plan's rollout table.
void Register_Flecs_Tags(void)
{
	Register_Class_Tag("AITriggerTypeClass");
	Register_Class_Tag("AircraftTypeClass");
	Register_Class_Tag("AnimTypeClass");
	Register_Class_Tag("BuildingTypeClass");
	Register_Class_Tag("BulletTypeClass");
	Register_Class_Tag("CampaignClass");
	Register_Class_Tag("HouseTypeClass");
	Register_Class_Tag("InfantryTypeClass");
	Register_Class_Tag("IsometricTileTypeClass");
	Register_Class_Tag("OverlayTypeClass");
	Register_Class_Tag("ParticleSystemTypeClass");
	Register_Class_Tag("ParticleTypeClass");
	Register_Class_Tag("ScriptTypeClass");
	Register_Class_Tag("SideClass");
	Register_Class_Tag("SmudgeTypeClass");
	Register_Class_Tag("SuperWeaponTypeClass");
	Register_Class_Tag("TagTypeClass");
	Register_Class_Tag("TaskForceClass");
	Register_Class_Tag("TeamTypeClass");
	Register_Class_Tag("TerrainTypeClass");
	Register_Class_Tag("TiberiumClass");
	Register_Class_Tag("TriggerTypeClass");
	Register_Class_Tag("UnitTypeClass");
	Register_Class_Tag("VoxelAnimTypeClass");
	Register_Class_Tag("WarheadTypeClass");
	Register_Class_Tag("WeaponTypeClass");
	Register_Class_Tag("AircraftClass");
	Register_Class_Tag("BuildingClass");
	Register_Class_Tag("InfantryClass");
	Register_Class_Tag("UnitClass");
	Register_Class_Tag("BuildingLightClass");
	Register_Class_Tag("BulletClass");
	Register_Class_Tag("OverlayClass");
	Register_Class_Tag("ParticleClass");
	Register_Class_Tag("ParticleSystemClass");
	Register_Class_Tag("SmudgeClass");
	Register_Class_Tag("TerrainClass");
	Register_Class_Tag("VeinholeMonsterClass");
	Register_Class_Tag("VoxelAnimClass");
	Register_Class_Tag("WaveClass");
	Register_Class_Tag("AlphaShapeClass");
	Register_Class_Tag("CellClass");
	Register_Class_Tag("EMPulseClass");
	Register_Class_Tag("FactoryClass");
	Register_Class_Tag("FoggedObjectClass");
	Register_Class_Tag("HouseClass");
	Register_Class_Tag("LightSourceClass");
	Register_Class_Tag("NeuronClass");
	Register_Class_Tag("ScriptClass");
	Register_Class_Tag("SuperClass");
	Register_Class_Tag("TagClass");
	Register_Class_Tag("TeamClass");
	Register_Class_Tag("TriggerClass");
	Register_Class_Tag("TubeClass");
	Register_Class_Tag("WaypointPathClass");
	Register_Class_Tag("DriveLocomotionClass");
	Register_Class_Tag("DropPodLocomotionClass");
	Register_Class_Tag("FlyLocomotionClass");
	Register_Class_Tag("HoverLocomotionClass");
	Register_Class_Tag("JumpjetLocomotionClass");
	Register_Class_Tag("LevitateLocomotionClass");
	Register_Class_Tag("MechLocomotionClass");
	Register_Class_Tag("TeleportLocomotionClass");
	Register_Class_Tag("TunnelLocomotionClass");
	Register_Class_Tag("WalkLocomotionClass");
}
