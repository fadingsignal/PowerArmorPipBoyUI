#pragma once

#include "Runtime/Profiles.h"

namespace PowerArmorPipBoyUI::Runtime
{
	struct HookAddresses
	{
		std::uintptr_t actorInPowerArmor;
		std::vector<std::uintptr_t> presentation;
		std::vector<std::uintptr_t> openAudio;
		std::vector<std::uintptr_t> closeAudio;
		std::vector<std::uintptr_t> light;
		std::vector<std::uintptr_t> pipboyClosedCalls;
		std::uintptr_t tabHandlerCall;
		std::uintptr_t companionUseItemCall;
		std::vector<std::uintptr_t> pipboyCloseCalls;
		std::vector<std::uintptr_t> pipboyLoadHolotapeCalls;
		struct VtableHook
		{
			std::uintptr_t address;
			std::size_t slot;
		};
		VtableHook pipboyFrame;
		VtableHook inputShouldHandle;
		VtableHook inputButton;
		VtableHook firstPersonUpdate;
		struct PipboyActiveSetter
		{
			using ABI = Profiles::ActiveSetterABI;

			std::uintptr_t address;
			ABI abi;
		};
		std::optional<PipboyActiveSetter> setPipboyActive;

		struct Rain
		{
			VtableHook hudFrame;
			std::uintptr_t powerArmorHUDRainModifierGetter;
			std::uintptr_t referenceIsInterior;
			std::uintptr_t getSubmergeLevel;
		};
		std::optional<Rain> rain;
	};

	[[nodiscard]] bool IsSupported(const REL::Version& a_runtime);
	[[nodiscard]] std::optional<HookAddresses> ResolveHookAddresses(
		const REL::Version& a_runtime);
}
