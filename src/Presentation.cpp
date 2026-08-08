#include "Presentation.h"

#include "Diagnostics.h"
#include "Hooks.h"
#include "Settings.h"

namespace PowerArmorPipBoyUI::Presentation
{
	namespace
	{
		using SetPipboyActive_t = bool (*)(
			RE::BSTValueEventSource<RE::IsPipboyActiveEvent>*,
			const bool*);

		RE::NiPointer<RE::NiNode> g_powerArmorPipboyScreen;
		std::atomic_bool g_forceThisOpen = false;
		std::atomic_bool g_loggedFirstPersonFreeze = false;
		std::atomic_bool g_deferNextScreenReveal = true;
		std::atomic_bool g_ownsPowerArmorPipboyGlass = false;

		bool SetPipboyActive(RE::PipboyManager* a_manager, const bool a_active)
		{
			// BSTValueEventSource's engine setter locks the value and broadcasts an
			// IsPipboyActiveEvent when it changes. This is the same function called by
			// PipboyManager::OnPipboyOpened/Closed in Fallout 4 1.10.163.
			static REL::Relocation<SetPipboyActive_t> setActive{ REL::ID(318434) };
			return setActive(std::addressof(a_manager->pipboyActive), std::addressof(a_active));
		}

		// The PA presentation needs only this screen quad. PowerArmorGeometry normally loads
		// it together with the dashboard and rain geometry after a PreloadPowerArmor event.
		// Loading just the quad avoids invoking that subsystem's completion callback out of
		// sequence and avoids creating any of the PowerArmorHUDMenu geometry.
		[[nodiscard]] bool LoadPowerArmorPipboyScreen()
		{
			if (g_powerArmorPipboyScreen) {
				return true;
			}

			RE::BSModelDB::DBTraits::ArgsType args{};
			args.lodFadeMult = RE::ENUM_LOD_MULT::kNone;
			args.loadLevel = 0;
			args.prepareAfterLoad = true;
			args.faceGenModel = false;
			// A marker would make Demand report a usable node and defeat the wrist-Pip-Boy
			// fallback below when the PA screen resource is missing or corrupt.
			args.useErrorMarker = false;
			args.performProcess = true;
			args.createFadeNode = true;
			args.loadTextures = true;

			const auto result = RE::BSModelDB::Demand(
				"Interface/Objects/PADashPipboyScreen.nif",
				std::addressof(g_powerArmorPipboyScreen),
				args);
			if (result != RE::BSResource::ErrorCode::kNone || !g_powerArmorPipboyScreen) {
				REX::ERROR(
					"Could not load PADashPipboyScreen.nif (BSResource error {})",
					std::to_underlying(result));
				g_powerArmorPipboyScreen.reset();
				return false;
			}

			// These are the exact local-space values applied by
			// PowerArmorGeometry::BackgroundTaskFinishedLoading in 1.10.163.
			g_powerArmorPipboyScreen->SetLocalTranslate({ -0.5F, 325.0F, -37.0F });
			RE::NiUpdateData updateData{};
			g_powerArmorPipboyScreen->Update(updateData);
			// Keep the standalone child hidden until the Pip-Boy renderer has configured
			// its screen-attached surface. PowerArmorGeometry::ShowPipboyPAGeometry will
			// unhide it during open; the first open is reculled for one setup frame below.
			g_powerArmorPipboyScreen->SetAppCulled(true);

			DiagnosticLog("Loaded PADashPipboyScreen.nif without the Power Armor dashboard");
			return true;
		}

		[[nodiscard]] bool EnsurePowerArmorPipboyGeometry()
		{
			if (!LoadPowerArmorPipboyScreen()) {
				return false;
			}

			auto* geometry = RE::PowerArmorGeometry::GetSingleton();
			if (!geometry) {
				REX::ERROR("PowerArmorGeometry is not available; keeping the wrist Pip-Boy");
				return false;
			}

			if (geometry->pipboyPAGlass) {
				return true;
			}

			geometry->pipboyPAGlass = g_powerArmorPipboyScreen;
			g_ownsPowerArmorPipboyGlass.store(true, std::memory_order_relaxed);
			DiagnosticLog("Attached the standalone screen to PowerArmorGeometry::pipboyPAGlass");
			return true;
		}

		void DetachStandalonePowerArmorPipboyGeometry()
		{
			// Vanilla may reuse the same cached NIF node for the genuine power-armor
			// presentation. Pointer identity therefore does not establish ownership.
			// Detach only when this plugin explicitly populated the slot.
			if (!g_ownsPowerArmorPipboyGlass.exchange(false, std::memory_order_relaxed)) {
				return;
			}

			auto* geometry = RE::PowerArmorGeometry::GetSingleton();
			if (g_powerArmorPipboyScreen && geometry &&
				geometry->pipboyPAGlass.get() == g_powerArmorPipboyScreen.get()) {
				g_powerArmorPipboyScreen->SetAppCulled(true);
				geometry->pipboyPAGlass.reset();
				DiagnosticLog("Detached the standalone screen after the forced Pip-Boy session");
			} else {
				DiagnosticLog("Released standalone-screen ownership after vanilla replaced the geometry");
			}
		}

		void ResetForcedPresentation(const std::string_view a_reason)
		{
			const bool wasForced = g_forceThisOpen.exchange(false, std::memory_order_relaxed);
			g_loggedFirstPersonFreeze.store(false, std::memory_order_relaxed);
			DetachStandalonePowerArmorPipboyGeometry();
			if (wasForced) {
				DiagnosticLog("Reset forced Pip-Boy presentation ({})", a_reason);
			}
		}

		void RevealPowerArmorPipboyScreen()
		{
			g_powerArmorPipboyScreen->SetAppCulled(false);
			RE::NiUpdateData updateData{};
			g_powerArmorPipboyScreen->Update(updateData);
		}

		void DeferFirstScreenReveal()
		{
			auto* geometry = RE::PowerArmorGeometry::GetSingleton();
			if (!g_deferNextScreenReveal.exchange(false, std::memory_order_relaxed) ||
				!g_powerArmorPipboyScreen || !geometry ||
				geometry->pipboyPAGlass.get() != g_powerArmorPipboyScreen.get()) {
				return;
			}

			// On the first open, the Interface3D renderer and Scaleform render target are
			// initialized in the same call that unhides this quad. Re-cull it before that
			// frame is presented, then reveal it on the following update once the texture
			// has valid contents. Subsequent opens use the warmed renderer immediately.
			g_powerArmorPipboyScreen->SetAppCulled(true);
			if (const auto* tasks = F4SE::GetTaskInterface()) {
				tasks->AddTask([] {
					auto* manager = RE::PipboyManager::GetSingleton();
					if (g_forceThisOpen.load(std::memory_order_relaxed) && manager &&
						manager->QPipboyActive() && g_powerArmorPipboyScreen) {
						RevealPowerArmorPipboyScreen();
						DiagnosticLog("Revealed the warmed Pip-Boy screen after its setup frame");
					}
				});
			} else {
				RevealPowerArmorPipboyScreen();
			}
		}

		[[nodiscard]] bool IsForcedPipboyToggleEvent(const RE::InputEvent* a_event)
		{
			if (!g_forceThisOpen.load(std::memory_order_relaxed) || !a_event) {
				return false;
			}

			const auto* buttonEvent = a_event->As<RE::ButtonEvent>();
			if (!buttonEvent || !buttonEvent->QJustPressed()) {
				return false;
			}

			// Gameplay calls the toggle "Pipboy", while the BasicMenuNav context can
			// remap the same control to "Cancel" after the menu is on the stack. Match
			// both semantic events, and retain the physical Tab identity as a fallback.
			const auto& userEvent = buttonEvent->QUserEvent();
			return userEvent == "Pipboy"sv || userEvent == "Cancel"sv ||
			       buttonEvent->GetBSButtonCode() == RE::BS_BUTTON_CODE::kTab;
		}

		[[nodiscard]] bool IsNestedPipboyPresentationOpen()
		{
			const auto* ui = RE::UI::GetSingleton();
			if (!ui) {
				return false;
			}

			if (ui->GetMenuOpen<RE::PipboyHolotapeMenu>()) {
				return true;
			}
			if (ui->GetMenuOpen<RE::TerminalMenu>()) {
				return true;
			}

			const auto pipboyMenu = ui->GetMenu<RE::PipboyMenu>();
			return pipboyMenu &&
			       (pipboyMenu->showingModalMessage || pipboyMenu->pipboyHiddenByAnotherMenu);
		}

		[[nodiscard]] bool IsForcedPipboyCloseEvent(const RE::InputEvent* a_event)
		{
			if (!IsForcedPipboyToggleEvent(a_event) || IsNestedPipboyPresentationOpen()) {
				return false;
			}

			// PipboyMenu owns Tab/Cancel while a nested presentation is active. Item
			// inspection clears pipboyExamineMode before it finishes raising the Pip-Boy,
			// but loweringReason remains kInspect until the inventory has been restored.
			// Closing during either phase bypasses that unwind and leaves its input layer
			// enabled, locking gameplay controls.
			const auto* manager = RE::PipboyManager::GetSingleton();
			return !manager ||
			       (!manager->pipboyExamineMode && !manager->pipboyRaising &&
				   manager->loweringReason.underlying() ==
					   std::to_underlying(RE::PipboyManager::LOWER_REASON::kNone));
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

			// Start the normal PA close. This selects the PA completion event and keeps
			// itemAnimOnClose/fastTravelLocation intact. The native a_noAnim=true path
			// cannot be used: OnPipboyCloseAnim deliberately clears both deferred fields
			// when it sees that event. Since the player's non-PA behavior graph cannot
			// report the PA completion event, deliver it synchronously ourselves.
			a_manager->PlayPipboyCloseAnim(false);
			if (a_manager->QPipboyActive()) {
				a_manager->OnPipboyCloseAnim();
			}

			// OnPipboyCloseAnim normally reaches the authoritative closedown hook. Keep
			// this idempotent reset as a final guard if the engine returned early.
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
		return Hooks::ActorInPowerArmor(a_actor) ||
		       g_forceThisOpen.load(std::memory_order_relaxed);
	}

	bool UsePowerArmorPipboyAudio(const RE::Actor& a_actor)
	{
		return Hooks::ActorInPowerArmor(a_actor) ||
		       (Settings::PowerArmorAudio() &&
			   g_forceThisOpen.load(std::memory_order_relaxed));
	}

	bool UsePowerArmorPipboyLight(const RE::Actor& a_actor)
	{
		return Hooks::ActorInPowerArmor(a_actor) ||
		       (Settings::KeepPipboyLightOn() &&
			   g_forceThisOpen.load(std::memory_order_relaxed));
	}

	bool ShouldHandleForcedPipboyClose(
		RE::BSInputEventUser* a_inputUser,
		const RE::InputEvent* a_event)
	{
		// PipboyMenu normally rejects every event while any PipboyManager transition
		// flag is set. Always admit the toggle button for our immediate presentation
		// so it can reach the native close fallback below.
		return (IsForcedPipboyToggleEvent(a_event) && !IsNestedPipboyPresentationOpen()) ||
		       Hooks::PipboyMenuShouldHandleEvent(a_inputUser, a_event);
	}

	void PlayPipboyCloseForForcedPresentation(RE::PipboyManager* a_manager, const bool a_noAnim)
	{
		if (g_forceThisOpen.load(std::memory_order_relaxed) && !a_noAnim) {
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
		const bool forceNoAnimation =
			g_forceThisOpen.load(std::memory_order_relaxed) && !a_noAnim;
		if (forceNoAnimation) {
			DiagnosticLog("Using the native no-animation path for a forced holotape load");
		}

		a_manager->PlayPipboyLoadHolotapeAnim(a_holotape, a_noAnim || forceNoAnimation);
	}

	void ClosedownPipboyAndReset(RE::PipboyManager* a_manager)
	{
		// Keep the forced state latched through ClosedownPipboy itself: its audio and
		// presentation checks still need to see the PA branch. Reset only after vanilla
		// has finished restoring menu, input, light, and deferred-action state.
		Hooks::ClosedownPipboy(a_manager);
		ResetForcedPresentation("engine closedown"sv);
	}

	void HandleForcedPipboyClose(
		RE::BSInputEventUser* a_inputUser,
		const RE::ButtonEvent* a_event)
	{
		if (IsForcedPipboyToggleEvent(a_event) && !IsForcedPipboyCloseEvent(a_event)) {
			if (const auto* manager = RE::PipboyManager::GetSingleton()) {
				DiagnosticLog(
					"Passing Pip-Boy toggle to nested presentation: examine={} raising={} loweringReason={}",
					manager->pipboyExamineMode,
					manager->pipboyRaising,
					manager->loweringReason.underlying());
			}
			Hooks::PipboyMenuOnButtonEvent(a_inputUser, a_event);
			return;
		}

		if (IsForcedPipboyCloseEvent(a_event)) {
			if (auto* manager = RE::PipboyManager::GetSingleton()) {
				const bool activeBefore = manager->QPipboyActive();
				DiagnosticLog(
					"Forced close input: event='{}' code=0x{:X} active={} opening={} closing={} loweringReason={}",
					a_event->QUserEvent().c_str(),
					static_cast<std::uint32_t>(a_event->GetBSButtonCode()),
					activeBefore,
					manager->pipboyOpening,
					manager->pipboyClosing,
					manager->loweringReason.underlying());

				CompleteForcedPipboyClose(manager);
				// PipboyMenu receives a const event even though Bethesda's input dispatch
				// convention marks handled state in the engine-owned event object itself.
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
		if (g_forceThisOpen.load(std::memory_order_relaxed)) {
			// The first-person state samples the animated camera node and applies its
			// locomotion smoothing every frame, even while the Pip-Boy menu has paused
			// the world. A real PA graph supplies a stationary camera pose here. Freeze
			// only this state for our screen-only presentation instead of entering a
			// Pip-Boy camera mode, which changes the camera stack and return view.
			if (!g_loggedFirstPersonFreeze.exchange(true, std::memory_order_relaxed)) {
				DiagnosticLog("Froze first-person camera updates for the forced Pip-Boy session");
			}
			return;
		}

		Hooks::FirstPersonStateUpdate(a_state, a_nextState);
	}

	void OpenPipboyWithoutWristAnimation(
		RE::PipboyManager* a_manager,
		const RE::BSFixedString& a_menuName)
	{
		// INI changes become effective at the next stable presentation boundary;
		// Fallout 4 does not need to be restarted.
		Settings::Load();

		// Repair any presentation state left behind by a nonstandard menu teardown
		// before deciding how this new session should open.
		ResetForcedPresentation("new open boundary"sv);

		if (!Settings::ForcePowerArmorPipboy()) {
			a_manager->PlayPipboyOpenAnim(a_menuName);
			return;
		}

		const auto* player = RE::PlayerCharacter::GetSingleton();
		const bool playerInPowerArmor = player && Hooks::ActorInPowerArmor(*player);
		DiagnosticLog("Pip-Boy open classified as genuine power armor: {}", playerInPowerArmor);
		if (playerInPowerArmor) {
			// Preserve the genuine PA behavior graph and presentation unchanged.
			a_manager->PlayPipboyOpenAnim(a_menuName);
			return;
		}

		if (!EnsurePowerArmorPipboyGeometry()) {
			a_manager->PlayPipboyOpenAnim(a_menuName);
			return;
		}

		g_forceThisOpen.store(true, std::memory_order_relaxed);

		// PlayPipboyOpenAnim always queues the arm-raise idle and waits for the graph to
		// report back; there is no code branch for power armor, the PA skeleton simply
		// resolves it instantly. Going through the generic path with a_noAnim set skips
		// the wait and hands straight off to OnPipboyOpenAnim.
		const RE::BSFixedString noAnimationEvent{};
		a_manager->PlayPipboyGenericOpenAnim(a_menuName, noAnimationEvent, true);
		DeferFirstScreenReveal();

		// The immediate generic path should establish this in OnPipboyOpened. Repair
		// it through the engine's event-producing setter if that handoff was skipped;
		// PlayPipboyCloseAnim refuses to run at all while this value is false.
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
				g_powerArmorPipboyScreen.reset();
				DiagnosticLog("Released the standalone Pip-Boy screen before game-data teardown");
				return;
			}

			if (!Settings::ForcePowerArmorPipboy()) {
				return;
			}

			// The resource database is ready here, but PowerArmorGeometry is not created
			// until the playable game/HUD exists. Warm only the NIF at this stage.
			(void)LoadPowerArmorPipboyScreen();
			return;
		}

		if (a_message->type == F4SE::MessagingInterface::kPostLoadGame ||
			a_message->type == F4SE::MessagingInterface::kNewGame) {
			ResetForcedPresentation(
				a_message->type == F4SE::MessagingInterface::kPostLoadGame ?
					"post-load game"sv :
					"new game"sv);
			g_deferNextScreenReveal.store(true, std::memory_order_relaxed);
			if (!Settings::ForcePowerArmorPipboy()) {
				return;
			}
			// Do not occupy PowerArmorGeometry::pipboyPAGlass outside a forced menu
			// session. The genuine PA preload owns that slot. The NIF itself is already
			// cached; first-frame conceal/reveal handles render-target initialization.
			(void)LoadPowerArmorPipboyScreen();
		}
	}
}
