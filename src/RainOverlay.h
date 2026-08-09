#pragma once

namespace PowerArmorPipBoyUI::RainOverlay
{
	void AdvanceHUDMenu(
		RE::IMenu* a_menu,
		float a_timeDelta,
		std::uint64_t a_time);

	void OnF4SEMessage(F4SE::MessagingInterface::Message* a_message);
}
