#pragma once

namespace PowerArmorPipBoyUI::Presentation::InputPolicy
{
	[[nodiscard]] bool IsForcedToggle(const RE::InputEvent* a_event);
	[[nodiscard]] bool IsForcedClose(const RE::InputEvent* a_event);
	[[nodiscard]] bool IsNestedPresentationOpen();
}
