#pragma once

namespace PowerArmorPipBoyUI::Runtime
{
	struct HookAddresses
	{
		std::uintptr_t actorInPowerArmor;
		std::vector<std::uintptr_t> presentation;
		std::vector<std::uintptr_t> openAudio;
		std::vector<std::uintptr_t> closeAudio;
		std::vector<std::uintptr_t> light;
		std::vector<std::uintptr_t> closedown;
		std::uintptr_t tabHandlerCall;
		std::uintptr_t companionUseItemCall;
		std::vector<std::uintptr_t> pipboyCloseCalls;
		std::vector<std::uintptr_t> pipboyLoadHolotapeCalls;
		std::uintptr_t pipboyMenuInputVtable;
		std::uintptr_t firstPersonStateVtable;
		std::uintptr_t hudMenuVtable;
		std::uintptr_t powerArmorHUDRainModifierGetter;
		std::uintptr_t referenceIsInterior;
		std::uintptr_t getSubmergeLevel;
	};

	inline constexpr std::size_t kShouldHandleEventIndex = 1;
	inline constexpr std::size_t kOnButtonEventIndex = 8;
	inline constexpr std::size_t kFirstPersonStateUpdateIndex = 0x0B;
	inline constexpr std::size_t kHUDMenuAdvanceMovieIndex = 0x04;

	[[nodiscard]] std::optional<HookAddresses> ResolveHookAddresses();
}
