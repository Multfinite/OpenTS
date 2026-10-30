/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

// Internal header confined to code/ec/ and translation units that include
// code/ec/entity_inl.h. Exposes the underlying flecs::world storage instance.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <flecs.h>

#include <cstddef>

namespace EC {

// Accesses the active flecs::world storage, lazily constructing it on first call.
flecs::world & World(void) noexcept;

// Returns true when an active flecs::world currently exists and has not been torn down.
bool Is_World_Alive(void) noexcept;

//Recreates a clean flecs::world (used on Clear_Scenario and before loading a save).
void World_Reset(void) noexcept;

// Destroys the flecs::world at engine shutdown; subsequent Destroy_Entity_Handle
// calls from late static destructors safely become no-ops.
void World_Shutdown(void) noexcept;

// Number of currently materialized entities across ObjectClass, CellClass, and LocomotionClass.
std::size_t Live_Entity_Count(void) noexcept;

} // namespace EC
