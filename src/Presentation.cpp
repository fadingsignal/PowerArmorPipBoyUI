#include "Presentation.h"

#include "Diagnostics.h"
#include "Hooks.h"
#include "Presentation/InputPolicy.h"
#include "Presentation/ScreenGeometry.h"
#include "Presentation/SessionState.h"
#include "Presentation/TerminalHandoff.h"
#include "Settings.h"

namespace PowerArmorPipBoyUI::Presentation
{
	namespace
	{
		using SetPipboyActive_t = bool (*)(
			RE::BSTValueEventSource<RE::IsPipboyActiveEvent>*,
			const bool*);

		bool SetPipboyActive(RE::PipboyManager* a_manager, const bool a_active)
		{
			// BSTValueEventSource's engine setter locks the value and broadcasts an
			// IsPipboyActiveEvent when it changes. This is the same function called by
			// PipboyManager::OnPipboyOpened/Closed in Fallout 4 1.10.163.
			static REL::Relocation<SetPipboyActive_t> setActive{ REL::ID(318434) };
			return setActive(std::addressof(a_manager->pipboyActive), std::addressof(a_active));
		}

		void ResetForcedPresentation(const std::string_view a_reason)
		{
			TerminalHandoff::Reset();
			const bool wasForced = SessionState::Reset();
			ScreenGeometry::Reset();
			if (wasForced) {
				DiagnosticLog("Reset forced Pip-Boy presentation ({})", a_reason);
			}
		}

		void CompleteForcedPipboyClose(RE::PipboyManager* a_manager)
		{
			// Pick up an audio-setting edit made while this menu was open. Presentation
			// mode remains latched until teardown so changing the master switch mid-menu
			// cannot strand the session in a half-forced state.
			Settings::Load();

			if (!a_manager->QPipboyActive()) {
				SetPipboyActive(a_manager, true);
				REX::WARN("Repaired missing Pip-Boy active state before close");
			}

			const bool hadPendingItemAnimation = a_manager->itemAnimOnClose != nullptr;
			const bool hadPendingFastTravel = static_cast<bool>(a_manager->fastTravelLocation);

			// Start the normal PA close and then deliver its completion synchronously.
			// The non-PA player graph cannot report the PA completion event itself.
			a_manager->PlayPipboyCloseAnim(false);
			if (a_manager->QPipboyActive()) {
				a_manager->OnPipboyCloseAnim();
			}

			// The closedown hook normally resets the session. Keep this idempotent guard
			// for early-return paths in the native close implementation.
			ResetForcedPresentation("synthetic close completion"sv);
			DiagnosticLog(
				"Completed synthetic PA close: active={} opening={} closing={} pendingItem={} pendingFastTravel={}",
				a_manager->QPipboyActive(),
				a_manager->pipboyOpening,
				a_manager->pipboyClosing,
				hadPendingItemAnimation,
				hadPendingFastTravel);
		}
	}

	bool UsePowerArmorPipboy(const RE::Actor& a_actor)
	{
		return Hooks::ActorInPowerArmor(a_actor) || SessionState::IsForced();
	}

	bool UsePowerArmorPipboyAudio(const RE::Actor& a_actor)
	{
		return Hooks::ActorInPowerArmor(a_actor) ||
		       (Settings::PowerArmorAudio() && SessionState::IsForced());
	}

	bool UsePowerArmorPipboyLight(const RE::Actor& a_actor)
	{
		return Hooks::ActorInPowerArmor(a_actor) ||
		       (Settings::KeepPipboyLightOn() && SessionState::IsForced());
	}

	bool ShouldHandleForcedPipboyClose(
		RE::BSInputEventUser* a_inputUser,
		const RE::InputEvent* a_event)
	{
		// Only bypass vanilla's transition gate when this event can actually complete
		// a stable top-level forced close. All other events retain vanilla admission.
		return InputPolicy::IsForcedClose(a_event) ||
		       Hooks::PipboyMenuShouldHandleEvent(a_inputUser, a_event);
	}

	void PlayPipboyCloseForForcedPresentation(
		RE::PipboyManager* a_manager,
		const bool a_noAnim)
	{
		if (SessionState::IsForced() && !a_noAnim) {
			CompleteForcedPipboyClose(a_manager);
			return;
		}

		a_manager->PlayPipboyCloseAnim(a_noAnim);
	}

	void PlayPipboyLoadHolotapeForForcedPresentation(
		RE::PipboyManager* a_manager,
		RE::BGSNote* a_holotape,
		const bool a_noAnim)
	{
		const bool forceNoAnimation = SessionState::IsForced() && !a_noAnim;
		if (forceNoAnimation) {
			DiagnosticLog("Using the native no-animation path for a forced holotape load");
		}

		a_manager->PlayPipboyLoadHolotapeAnim(a_holotape, a_noAnim || forceNoAnimation);
	}

	void ClosedownPipboyAndReset(RE::PipboyManager* a_manager)
	{
		// Keep the forced state latched through ClosedownPipboy itself: its audio and
		// presentation checks still need to see the PA branch.
		Hooks::ClosedownPipboy(a_manager);
		ResetForcedPresentation("engine closedown"sv);
	}

	void HandleForcedPipboyClose(
		RE::BSInputEventUser* a_inputUser,
		const RE::ButtonEvent* a_event)
	{
		if (InputPolicy::IsForcedToggle(a_event) && !InputPolicy::IsForcedClose(a_event)) {
			if (const auto* manager = RE::PipboyManager::GetSingleton()) {
				DiagnosticLog(
					"Passing Pip-Boy toggle to vanilla presentation: examine={} raising={} loweringReason={}",
					manager->pipboyExamineMode,
					manager->pipboyRaising,
					manager->loweringReason.underlying());
			}
			Hooks::PipboyMenuOnButtonEvent(a_inputUser, a_event);
			return;
		}

		if (InputPolicy::IsForcedClose(a_event)) {
			if (auto* manager = RE::PipboyManager::GetSingleton()) {
				DiagnosticLog(
					"Forced close input: event='{}' code=0x{:X} active={} opening={} closing={} loweringReason={}",
					a_event->QUserEvent().c_str(),
					static_cast<std::uint32_t>(a_event->GetBSButtonCode()),
					manager->QPipboyActive(),
					manager->pipboyOpening,
					manager->pipboyClosing,
					manager->loweringReason.underlying());

				CompleteForcedPipboyClose(manager);
				const_cast<RE::ButtonEvent*>(a_event)->handled =
					RE::InputEvent::HANDLED_RESULT::kStop;
				return;
			}

			REX::ERROR("PipboyManager is unavailable; passing the Pip-Boy toggle to vanilla");
		}

		Hooks::PipboyMenuOnButtonEvent(a_inputUser, a_event);
	}

	void UpdateFirstPersonCameraForForcedPresentation(
		RE::TESCameraState* a_state,
		RE::BSTSmartPointer<RE::TESCameraState>& a_nextState)
	{
		if (SessionState::IsForced()) {
			if (SessionState::MarkFirstPersonFreeze()) {
				DiagnosticLog("Froze first-person camera updates for the forced Pip-Boy session");
			}
			return;
		}

		Hooks::FirstPersonStateUpdate(a_state, a_nextState);
	}

	void AdvancePipboyMenuForTerminalReturn(
		RE::IMenu* a_menu,
		const float a_timeDelta,
		const std::uint64_t a_time)
	{
		Hooks::PipboyMenuAdvanceMovie(a_menu, a_timeDelta, a_time);
		ScreenGeometry::AdvanceFirstReveal(SessionState::IsForced());
		TerminalHandoff::Advance();
	}

	void OpenPipboyWithoutWristAnimation(
		RE::PipboyManager* a_manager,
		const RE::BSFixedString& a_menuName)
	{
		Settings::Load();
		ResetForcedPresentation("new open boundary"sv);

		if (!Settings::ForcePowerArmorPipboy()) {
			a_manager->PlayPipboyOpenAnim(a_menuName);
			return;
		}

		const auto* player = RE::PlayerCharacter::GetSingleton();
		const bool playerInPowerArmor = player && Hooks::ActorInPowerArmor(*player);
		DiagnosticLog("Pip-Boy open classified as genuine power armor: {}", playerInPowerArmor);
		if (playerInPowerArmor) {
			const auto* geometry = RE::PowerArmorGeometry::GetSingleton();
			DiagnosticLog(
				"Genuine PA geometry identity: pipboyPAGlass={:p} pluginClone={:p} ownsSlot={}",
				static_cast<const void*>(geometry ? geometry->pipboyPAGlass.get() : nullptr),
				static_cast<const void*>(ScreenGeometry::GetPluginScreen()),
				ScreenGeometry::OwnsPowerArmorGlass());
			a_manager->PlayPipboyOpenAnim(a_menuName);
			return;
		}

		if (!ScreenGeometry::Attach()) {
			a_manager->PlayPipboyOpenAnim(a_menuName);
			return;
		}

		SessionState::Begin();
		const RE::BSFixedString noAnimationEvent{};
		a_manager->PlayPipboyGenericOpenAnim(a_menuName, noAnimationEvent, true);
		ScreenGeometry::DeferFirstReveal();

		if (!a_manager->QPipboyActive()) {
			SetPipboyActive(a_manager, true);
			REX::WARN("Generic open omitted Pip-Boy active state; repaired it");
		}
		DiagnosticLog("Forced Pip-Boy open completed: active={}", a_manager->QPipboyActive());
	}

	void OnF4SEMessage(F4SE::MessagingInterface::Message* a_message)
	{
		if (a_message->type == F4SE::MessagingInterface::kPreLoadGame) {
			ResetForcedPresentation("pre-load game"sv);
			return;
		}

		if (a_message->type == F4SE::MessagingInterface::kGameDataReady) {
			if (!a_message->data) {
				ResetForcedPresentation("game data unloaded"sv);
				DiagnosticLog("Released forced presentation state before game-data teardown");
				return;
			}

			TerminalHandoff::RegisterSink();
			if (Settings::ForcePowerArmorPipboy()) {
				ScreenGeometry::Prewarm();
			}
			return;
		}

		if (a_message->type == F4SE::MessagingInterface::kPostLoadGame ||
			a_message->type == F4SE::MessagingInterface::kNewGame) {
			TerminalHandoff::RegisterSink();
			ResetForcedPresentation(
				a_message->type == F4SE::MessagingInterface::kPostLoadGame ?
					"post-load game"sv :
					"new game"sv);
			if (Settings::ForcePowerArmorPipboy()) {
				ScreenGeometry::Prewarm();
			}
		}
	}
}
