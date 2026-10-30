/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <flecs.h>

namespace EC {

using WorldInitCallback = void (*)(flecs::world &);

// Accesses the active flecs::world storage, constructing it on first call.
flecs::world & World(void) noexcept;

// Returns true when an active flecs::world currently exists and has not been torn down.
bool Is_World_Alive(void) noexcept;

// Registers the component-registration callback invoked whenever a flecs::world
// is created (and immediately if World() is already alive).
void Set_World_Init_Hook(WorldInitCallback callback) noexcept;

// Destroys the flecs::world at engine shutdown; subsequent Destroy_Entity calls
// from late static destructors (such as BlubCell) safely become no-ops.
void World_Shutdown(void) noexcept;

} // namespace EC
