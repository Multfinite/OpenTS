/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include <cstdint>

// The bridge link every entity carries. It holds no game state and duplicates
// no class member; a query result reaches its C++ object through it. Components
// for new functionality are added here, one struct per feature, data only.
struct LegacyObjectLink {
	void * Object;
};

// The bridge link every locomotor child entity carries.
struct LegacyLocomotionLink {
	void * Locomotion;
};

// Deterministic scenario-scoped creation index assigned to every attached
// entity so tie-breaking never depends on recycled flecs entity handles.
struct EntityBridgeMeta {
	std::uint32_t AttachSequence;
};
