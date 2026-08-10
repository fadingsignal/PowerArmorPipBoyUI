#include "Lifecycle.h"

#include "Presentation.h"
#include "RainOverlay.h"

namespace PowerArmorPipBoyUI::Lifecycle
{
	namespace
	{
		void OnF4SEMessage(F4SE::MessagingInterface::Message* a_message)
		{
			if (!a_message) {
				REX::WARN("Received an empty F4SE lifecycle message");
				return;
			}

			RainOverlay::OnF4SEMessage(a_message);
			Presentation::OnF4SEMessage(a_message);
		}
	}

	bool Register() noexcept
	{
		try {
			const auto* messaging = F4SE::GetMessagingInterface();
			if (!messaging) {
				REX::ERROR("F4SE messaging is unavailable; refusing to install lifecycle-dependent hooks");
				return false;
			}

			if (!messaging->RegisterListener(OnF4SEMessage)) {
				REX::ERROR("Could not register the F4SE lifecycle listener; refusing to install hooks");
				return false;
			}

			return true;
		} catch (const std::exception& exception) {
			try {
				REX::ERROR("Could not register the F4SE lifecycle listener: {}", exception.what());
			} catch (...) {
			}
		} catch (...) {
			try {
				REX::ERROR("Could not register the F4SE lifecycle listener: unknown exception");
			} catch (...) {
			}
		}

		return false;
	}
}
