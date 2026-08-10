#include "Presentation/InputPolicy.h"

#include "Presentation/SessionState.h"

namespace PowerArmorPipBoyUI::Presentation::InputPolicy
{
	bool IsForcedToggle(const RE::InputEvent* a_event)
	{
		if (!SessionState::IsForced() || !a_event) {
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

	bool IsNestedPresentationOpen()
	{
		const auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return false;
		}

		if (ui->GetMenuOpen<RE::PipboyHolotapeMenu>() ||
			ui->GetMenuOpen<RE::TerminalMenu>()) {
			return true;
		}

		const auto pipboyMenu = ui->GetMenu<RE::PipboyMenu>();
		return pipboyMenu &&
		       (pipboyMenu->showingModalMessage || pipboyMenu->pipboyHiddenByAnotherMenu);
	}

	bool IsForcedClose(const RE::InputEvent* a_event)
	{
		if (!IsForcedToggle(a_event) || IsNestedPresentationOpen()) {
			return false;
		}

		// Item inspection clears pipboyExamineMode before it finishes raising the
		// Pip-Boy, but loweringReason remains kInspect until inventory restoration.
		const auto* manager = RE::PipboyManager::GetSingleton();
		return !manager ||
		       (!manager->pipboyExamineMode && !manager->pipboyRaising &&
			   manager->loweringReason.underlying() ==
				   std::to_underlying(RE::PipboyManager::LOWER_REASON::kNone));
	}
}
