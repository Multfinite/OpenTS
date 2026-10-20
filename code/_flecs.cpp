/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "flecs/world.h"

// The one EC world object. Marked constinit so static storage initializes it
// before any dynamic global constructor runs, while local world instances in
// tests still zero their members cleanly.
constinit FlecsWorldClass FlecsWorld;
