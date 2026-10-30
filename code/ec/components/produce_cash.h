/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

// Interval cash production / upkeep drain and neutral-capture startup bonus
// component for BuildingClass instances whose BuildingTypeClass configures
// ProduceCashDelay + ProduceCashAmount or ProduceCashStartup.

#include "crc.h"
#include "ec/concepts.h"
#include "ftimer.h"
#include "mission.hh"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include "timer.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <concepts>
#include <limits>
#include <string_view>

class BuildingClass;

namespace EC {

struct ProduceCashComponent
{
	using OwnerType = BuildingClass;
	static constexpr std::string_view SaveName = "OpenTS.ProduceCash";

	CDTimerClass<FrameTimerClass> Timer{0};
	int Remaining{0};          // 0 = spent, < 0 = unlimited, > 0 = budget left to move
	bool IsStartupPaid{false};

	// Runs inside BuildingClass::Grand_Opening(bool captured) BEFORE HasOpened is set.
	template<std::same_as<BuildingClass> BuildingT = BuildingClass>
	void Grand_Opening(BuildingT & building, bool captured)
	{
		if (building.Class == nullptr) {
			return;
		}
		if (!building.HasOpened || (captured && building.Class->IsProduceCashResetOnCapture)) {
			Remaining = (building.Class->ProduceCashBudget > 0) ? building.Class->ProduceCashBudget : -1;
		}
		Timer = building.Class->ProduceCashDelay;
	}

	// Runs inside BuildingClass::Captured after ownership switches to newowner.
	template<std::same_as<BuildingClass> BuildingT = BuildingClass, typename HouseT>
	void Captured(BuildingT & building, HouseT * oldowner, HouseT * newowner)
	{
		(void)newowner;
		if (oldowner != nullptr && oldowner->Class != nullptr && oldowner->Class->IsMultiplayPassive) {
			Pay_Startup(building);
		}
	}

	// Runs in LogicClass::AI() PostAI pass over active (non-Limbo) entities only.
	template<std::same_as<BuildingClass> BuildingT = BuildingClass>
	void PostAI(BuildingT & building)
	{
		if (!building.IsActive || building.Strength <= 0 || !building.HasOpened) {
			return;
		}
		if (building.Class == nullptr || building.House == nullptr || building.House->Class == nullptr) {
			return;
		}
		if (building.Class->ProduceCashDelay <= 0 || Remaining == 0) {
			return;
		}

		int amount = building.Class->ProduceCashAmount;
		if (amount == 0 || amount == std::numeric_limits<int>::min()) {
			return;
		}

		if (building.Mission == MISSION_DECONSTRUCTION || building.MissionQueue == MISSION_DECONSTRUCTION) {
			return;
		}

		if (building.House->Class->IsMultiplayPassive) {
			return;
		}

		if (building.Class->IsPowered && (!building.Is_Powered_On() || building.House->Power_Fraction() < 1.0)) {
			Timer.Stop();
			return;
		}

		Timer.Start();

		if (Timer.Value() != 0) {
			return;
		}

		Timer = building.Class->ProduceCashDelay;

		if (Remaining > 0) {
			amount = std::clamp(amount, -Remaining, Remaining);
		}

		if (amount > 0 && building.House->Credits > std::numeric_limits<int>::max() - amount) {
			return;
		}

		if (Remaining > 0) {
			Remaining -= (amount < 0) ? -amount : amount;
		}

		if (amount > 0) {
			building.House->Refund_Money(amount);
		} else {
			building.House->Spend_Money(-amount);
		}
	}

	template<typename StreamT>
	void Serialize(StreamT & stream)
	{
		stream.Serialize(Timer);
		stream.Serialize(Remaining);
		stream.Serialize(IsStartupPaid);
	}

	void Compute_CRC(CRCEngine & crc) const
	{
		crc(Timer.Value());
		crc(Remaining);
		crc(IsStartupPaid);
	}

private:
	template<std::same_as<BuildingClass> BuildingT>
	void Pay_Startup(BuildingT & building)
	{
		if (building.Class == nullptr || building.House == nullptr || building.House->Class == nullptr) {
			return;
		}
		if (building.Class->ProduceCashStartup <= 0) {
			return;
		}
		if (building.House->Class->IsMultiplayPassive) {
			return;
		}
		if (building.Class->IsProduceCashStartupOneTime && IsStartupPaid) {
			return;
		}
		if (building.House->Credits > std::numeric_limits<int>::max() - building.Class->ProduceCashStartup) {
			return;
		}

		IsStartupPaid = true;
		building.House->Refund_Money(building.Class->ProduceCashStartup);
	}
};

} // namespace EC
