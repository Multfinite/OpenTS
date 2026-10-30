/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

// Canonical compile-time list of all gameplay components registered in the engine.
// Every component type listed in GameComponents participates in compile-time
// hook discovery, dense flecs table iteration, EC01 .SAV serialization, and CRC.

#if !defined(EC_CUSTOM_REGISTRY_COMPONENTS)
#include "ec/components/produce_cash.h"
#endif

#include <concepts>

namespace EC {

template<typename... Ts>
struct ComponentRegistry;

namespace Detail {

template<typename... Ts>
struct TypeList
{
	template<typename C>
	static constexpr bool contains = (std::same_as<C, Ts> || ...);
};

} // namespace Detail

#if defined(EC_CUSTOM_REGISTRY_COMPONENTS)
using GameComponents = ComponentRegistry<EC_CUSTOM_REGISTRY_COMPONENTS>;
using RegisteredComponentTypes = Detail::TypeList<EC_CUSTOM_REGISTRY_COMPONENTS>;
#else
using GameComponents = ComponentRegistry<
	ProduceCashComponent
>;
using RegisteredComponentTypes = Detail::TypeList<
	ProduceCashComponent
>;
#endif

template<typename C>
inline constexpr bool IsRegisteredComponent = RegisteredComponentTypes::template contains<C>;

} // namespace EC
