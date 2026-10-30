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
	WorldInitCallback InitHook = nullptr;
	bool Alive = false;
	bool HookApplied = false;

	~WorldHolder(void) noexcept
	{
		Alive = false;
		HookApplied = false;
		Instance.reset();
	}
};

// Construct-on-first-use avoids static initialization order hazards (e.g. BlubCell)
// while ensuring ~WorldHolder sets Alive=false before freeing flecs::world.
WorldHolder & Holder(void) noexcept
{
	static WorldHolder state;
	return state;
}

void Init_World_Instance(WorldHolder & state) noexcept
{
	state.Instance = std::make_unique<flecs::world>();
	state.Alive = true;
	state.HookApplied = false;
	state.Instance->component<OwnerLink<ObjectClass>>();
	state.Instance->component<OwnerLink<CellClass>>();
	state.Instance->component<OwnerLink<LocomotionClass>>();
	if (state.InitHook != nullptr) {
		state.HookApplied = true;
		state.InitHook(*state.Instance);
	}
}

} // namespace

flecs::world & World(void) noexcept
{
	WorldHolder & state = Holder();
	if (!state.Instance) {
		Init_World_Instance(state);
	}
	return *state.Instance;
}

bool Is_World_Alive(void) noexcept
{
	WorldHolder const & state = Holder();
	return state.Alive && state.Instance != nullptr;
}

void Set_World_Init_Hook(WorldInitCallback callback) noexcept
{
	WorldHolder & state = Holder();
	state.InitHook = callback;
	if (state.Alive && state.Instance != nullptr && callback != nullptr && !state.HookApplied) {
		state.HookApplied = true;
		callback(*state.Instance);
	}
}

void World_Shutdown(void) noexcept
{
	WorldHolder & state = Holder();
	state.Alive = false;
	state.HookApplied = false;
	state.Instance.reset();
}

void Destroy_Entity(flecs::entity & entity) noexcept
{
	if (entity.id() != 0 && Is_World_Alive()) {
		if (entity.is_alive()) {
			entity.destruct();
		}
	}
	entity = flecs::entity::null();
}

} // namespace EC
