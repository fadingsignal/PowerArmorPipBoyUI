#pragma once

namespace PowerArmorPipBoyUI::Settings
{
	void Load() noexcept;
	void ReloadRainOverlaySetting() noexcept;

	[[nodiscard]] bool ForcePowerArmorPipboy() noexcept;
	[[nodiscard]] bool PowerArmorAudio() noexcept;
	[[nodiscard]] bool KeepPipboyLightOn() noexcept;
	[[nodiscard]] bool RainOverlayOutsidePowerArmor() noexcept;
	[[nodiscard]] bool DebugLoggingEnabled() noexcept;
}
