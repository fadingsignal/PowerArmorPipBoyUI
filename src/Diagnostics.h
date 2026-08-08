#pragma once

#include "Settings.h"

namespace PowerArmorPipBoyUI
{
	template <class... Args>
	void DiagnosticLog(const std::format_string<Args...> a_format, Args&&... a_args) noexcept
	{
		if (!Settings::DebugLoggingEnabled()) {
			return;
		}

		// Diagnostics must never become a new exception path through an engine hook.
		try {
			REX::Impl::Log(
				std::source_location::current(),
				REX::ELogLevel::Info,
				a_format,
				std::forward<Args>(a_args)...);
		} catch (...) {
		}
	}
}
