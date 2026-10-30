/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "ec/world.h"
#include "ec/entity.h"

#include <memory>

namespace EC {

namespace {

struct WorldHolder
{
	std::unique_ptr<flecs::world> Instance;
	std::size_t LiveEntities = 0;
	bool Alive = false;

	~WorldHolder(void) noexcept
	{
		Alive = false;
		LiveEntities = 0;
		Instance.reset();
	}
};

// Construct-on-first-use avoids static initialization order hazards while ensuring
// ~WorldHolder sets Alive=false if torn down at process exit.
WorldHolder & Holder(void) noexcept
{
	static WorldHolder state;
	return state;
}

void Register_Core_Owner_Links(flecs::world & world) noexcept
{
	world.component<OwnerLink<ObjectClass>>();
	world.component<OwnerLink<CellClass>>();
	world.component<OwnerLink<LocomotionClass>>();
}

void Bind_Owner_Component(flecs::entity entity, RootKind kind, void * owner) noexcept
{
	switch (kind) {
		case RootKind::Object:
			entity.set<OwnerLink<ObjectClass>>(OwnerLink<ObjectClass>{static_cast<ObjectClass *>(owner)});
			break;
		case RootKind::Cell:
			entity.set<OwnerLink<CellClass>>(OwnerLink<CellClass>{static_cast<CellClass *>(owner)});
			break;
		case RootKind::Locomotor:
			entity.set<OwnerLink<LocomotionClass>>(OwnerLink<LocomotionClass>{static_cast<LocomotionClass *>(owner)});
			break;
	}
}

} // namespace

flecs::world & World(void) noexcept
{
	WorldHolder & state = Holder();
	if (!state.Instance) {
		state.Instance = std::make_unique<flecs::world>();
		state.LiveEntities = 0;
		state.Alive = true;
		Register_Core_Owner_Links(*state.Instance);
	}
	return *state.Instance;
}

bool Is_World_Alive(void) noexcept
{
	WorldHolder const & state = Holder();
	return state.Alive && state.Instance != nullptr;
}

void World_Reset(void) noexcept
{
	WorldHolder & state = Holder();
	state.Alive = false;
	state.LiveEntities = 0;
	state.Instance.reset();
	state.Instance = std::make_unique<flecs::world>();
	state.Alive = true;
	Register_Core_Owner_Links(*state.Instance);
}

void World_Shutdown(void) noexcept
{
	WorldHolder & state = Holder();
	state.Alive = false;
	state.LiveEntities = 0;
	state.Instance.reset();
}

std::size_t Live_Entity_Count(void) noexcept
{
	return Holder().LiveEntities;
}

namespace Detail {

std::uint64_t Materialize_Entity_Handle(RootKind kind, void * owner, bool disabled) noexcept
{
	flecs::world & world = World();
	flecs::entity entity = world.entity();
	Bind_Owner_Component(entity, kind, owner);
	if (disabled) {
		entity.add(flecs::Disabled);
	}
	++Holder().LiveEntities;
	return static_cast<std::uint64_t>(entity.id());
}

void Destroy_Entity_Handle(std::uint64_t id) noexcept
{
	if (id == 0 || !Is_World_Alive()) {
		return;
	}
	WorldHolder & state = Holder();
	flecs::entity entity(*state.Instance, static_cast<flecs::entity_t>(id));
	if (entity.is_alive()) {
		entity.destruct();
		if (state.LiveEntities > 0) {
			--state.LiveEntities;
		}
	}
}

void Rebind_Entity_Owner(std::uint64_t id, RootKind kind, void * owner) noexcept
{
	if (id == 0 || !Is_World_Alive()) {
		return;
	}
	flecs::entity entity(World(), static_cast<flecs::entity_t>(id));
	if (entity.is_alive()) {
		Bind_Owner_Component(entity, kind, owner);
	}
}

void Set_Entity_Disabled_Flag(std::uint64_t id, bool disabled) noexcept
{
	if (id == 0 || !Is_World_Alive()) {
		return;
	}
	flecs::entity entity(World(), static_cast<flecs::entity_t>(id));
	if (!entity.is_alive()) {
		return;
	}
	if (disabled) {
		entity.add(flecs::Disabled);
	} else {
		entity.remove(flecs::Disabled);
	}
}

bool Get_Entity_Disabled_Flag(std::uint64_t id) noexcept
{
	if (id == 0 || !Is_World_Alive()) {
		return false;
	}
	flecs::entity entity(World(), static_cast<flecs::entity_t>(id));
	return entity.is_alive() && entity.has(flecs::Disabled);
}

} // namespace Detail

} // namespace EC
