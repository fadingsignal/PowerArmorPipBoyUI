#pragma once

namespace PowerArmorPipBoyUI::Presentation::ScreenGeometry
{
	void Prewarm();
	[[nodiscard]] bool Attach();
	void Reset();

	void DeferFirstReveal();
	void AdvanceFirstReveal(bool a_forcedSession);

	[[nodiscard]] RE::NiNode* GetPluginScreen() noexcept;
	[[nodiscard]] bool OwnsPowerArmorGlass() noexcept;
}
