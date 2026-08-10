#include "Presentation/SessionState.h"

namespace PowerArmorPipBoyUI::Presentation::SessionState
{
	namespace
	{
		std::atomic_bool g_forced = false;
		std::atomic_bool g_loggedFirstPersonFreeze = false;
	}

	bool IsForced() noexcept
	{
		return g_forced.load(std::memory_order_relaxed);
	}

	void Begin() noexcept
	{
		g_forced.store(true, std::memory_order_relaxed);
	}

	bool Reset() noexcept
	{
		ResetFirstPersonFreeze();
		return g_forced.exchange(false, std::memory_order_relaxed);
	}

	bool MarkFirstPersonFreeze() noexcept
	{
		return !g_loggedFirstPersonFreeze.exchange(true, std::memory_order_relaxed);
	}

	void ResetFirstPersonFreeze() noexcept
	{
		g_loggedFirstPersonFreeze.store(false, std::memory_order_relaxed);
	}
}
