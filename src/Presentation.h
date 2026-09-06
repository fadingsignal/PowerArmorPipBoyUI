#pragma once

namespace PowerArmorPipBoyUI::Presentation
{
	bool UsePowerArmorPipboy(const RE::Actor& a_actor);
	bool UsePowerArmorPipboyAudio(const RE::Actor& a_actor);
	bool UsePowerArmorPipboyLight(const RE::Actor& a_actor);

	void OpenPipboyWithoutWristAnimation(
		RE::PipboyManager* a_manager,
		const RE::BSFixedString& a_menuName);
	void PlayPipboyCloseForForcedPresentation(
		RE::PipboyManager* a_manager,
		bool a_noAnim);
	void PlayPipboyLoadHolotapeForForcedPresentation(
		RE::PipboyManager* a_manager,
		RE::BGSNote* a_holotape,
		bool a_noAnim);
	void OnPipboyClosedAndReset(RE::PipboyManager* a_manager);

	bool ShouldHandleForcedPipboyClose(
		RE::BSInputEventUser* a_inputUser,
		const RE::InputEvent* a_event);
	void HandleForcedPipboyClose(
		RE::BSInputEventUser* a_inputUser,
		const RE::ButtonEvent* a_event);
	void UpdateFirstPersonCameraForForcedPresentation(
		RE::TESCameraState* a_state,
		RE::BSTSmartPointer<RE::TESCameraState>& a_nextState);
	void AdvancePipboyMenuForTerminalReturn(
		RE::IMenu* a_menu,
		float a_timeDelta,
		std::uint64_t a_time);

	void OnF4SEMessage(F4SE::MessagingInterface::Message* a_message);
}
