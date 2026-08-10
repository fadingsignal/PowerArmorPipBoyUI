#pragma once

namespace PowerArmorPipBoyUI::Presentation::SessionState
{
	[[nodiscard]] bool IsForced() noexcept;
	void Begin() noexcept;
	[[nodiscard]] bool Reset() noexcept;

	[[nodiscard]] bool MarkFirstPersonFreeze() noexcept;
	void ResetFirstPersonFreeze() noexcept;
}
