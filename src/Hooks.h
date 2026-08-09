#pragma once

namespace PowerArmorPipBoyUI::Hooks
{
	[[nodiscard]] bool Install();

	bool ActorInPowerArmor(const RE::Actor& a_actor);
	void ClosedownPipboy(RE::PipboyManager* a_manager);
	bool PipboyMenuShouldHandleEvent(
		RE::BSInputEventUser* a_inputUser,
		const RE::InputEvent* a_event);
	void PipboyMenuOnButtonEvent(
		RE::BSInputEventUser* a_inputUser,
		const RE::ButtonEvent* a_event);
	void FirstPersonStateUpdate(
		RE::TESCameraState* a_state,
		RE::BSTSmartPointer<RE::TESCameraState>& a_nextState);
	void PipboyMenuAdvanceMovie(
		RE::IMenu* a_menu,
		float a_timeDelta,
		std::uint64_t a_time);
	void HUDMenuAdvanceMovie(
		RE::IMenu* a_menu,
		float a_timeDelta,
		std::uint64_t a_time);

	[[nodiscard]] RE::TESImageSpaceModifier* GetPowerArmorHUDRainModifier();
	[[nodiscard]] bool ReferenceIsInterior(const RE::TESObjectREFR& a_reference);
	[[nodiscard]] float GetSubmergeLevel(const RE::TESObjectREFR& a_reference);
}
