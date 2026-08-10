#pragma once

namespace PowerArmorPipBoyUI::Rain::RendererLease
{
	[[nodiscard]] bool OwnsRenderer() noexcept;
	void Start(std::uint64_t a_time);
	void Relinquish(std::string_view a_reason);
	void Reset(std::string_view a_reason, bool a_releaseGeometry);
}
