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
void FlecsWorldClass::Register_Tags(void)
{
	Register_Tag("AITriggerTypeClass");
	Register_Tag("AircraftTypeClass");
	Register_Tag("AnimTypeClass");
	Register_Tag("BuildingTypeClass");
	Register_Tag("BulletTypeClass");
	Register_Tag("CampaignClass");
	Register_Tag("HouseTypeClass");
	Register_Tag("InfantryTypeClass");
	Register_Tag("IsometricTileTypeClass");
	Register_Tag("OverlayTypeClass");
	Register_Tag("ParticleSystemTypeClass");
	Register_Tag("ParticleTypeClass");
	Register_Tag("ScriptTypeClass");
	Register_Tag("SideClass");
	Register_Tag("SmudgeTypeClass");
	Register_Tag("SuperWeaponTypeClass");
	Register_Tag("TagTypeClass");
	Register_Tag("TaskForceClass");
	Register_Tag("TeamTypeClass");
	Register_Tag("TerrainTypeClass");
	Register_Tag("TiberiumClass");
	Register_Tag("TriggerTypeClass");
	Register_Tag("UnitTypeClass");
	Register_Tag("VoxelAnimTypeClass");
	Register_Tag("WarheadTypeClass");
	Register_Tag("WeaponTypeClass");
	Register_Tag("AircraftClass");
	Register_Tag("BuildingClass");
	Register_Tag("InfantryClass");
	Register_Tag("UnitClass");
	Register_Tag("BuildingLightClass");
	Register_Tag("BulletClass");
	Register_Tag("OverlayClass");
	Register_Tag("ParticleClass");
	Register_Tag("ParticleSystemClass");
	Register_Tag("SmudgeClass");
	Register_Tag("TerrainClass");
	Register_Tag("VeinholeMonsterClass");
	Register_Tag("VoxelAnimClass");
	Register_Tag("WaveClass");
	Register_Tag("AlphaShapeClass");
	Register_Tag("CellClass");
	Register_Tag("EMPulseClass");
	Register_Tag("FactoryClass");
	Register_Tag("FoggedObjectClass");
	Register_Tag("HouseClass");
	Register_Tag("LightSourceClass");
	Register_Tag("NeuronClass");
	Register_Tag("ScriptClass");
	Register_Tag("SuperClass");
	Register_Tag("TagClass");
	Register_Tag("TeamClass");
	Register_Tag("TriggerClass");
	Register_Tag("TubeClass");
	Register_Tag("WaypointPathClass");
	Register_Tag("DriveLocomotionClass");
	Register_Tag("DropPodLocomotionClass");
	Register_Tag("FlyLocomotionClass");
	Register_Tag("HoverLocomotionClass");
	Register_Tag("JumpjetLocomotionClass");
	Register_Tag("LevitateLocomotionClass");
	Register_Tag("MechLocomotionClass");
	Register_Tag("TeleportLocomotionClass");
	Register_Tag("TunnelLocomotionClass");
	Register_Tag("WalkLocomotionClass");
}
