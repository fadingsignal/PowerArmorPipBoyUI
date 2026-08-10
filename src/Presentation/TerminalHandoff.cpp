#include "Presentation/TerminalHandoff.h"

#include "Diagnostics.h"
#include "Hooks.h"
#include "Presentation/SessionState.h"

namespace PowerArmorPipBoyUI::Presentation::TerminalHandoff
{
	namespace
	{
		std::atomic_bool g_sinkRegistered = false;
		std::atomic_uint64_t g_generation = 0;
		std::atomic_uintptr_t g_screen = 0;
		std::atomic_uint32_t g_frames = 0;

		[[nodiscard]] RE::NiNode* GetReturnScreen()
		{
			auto* manager = RE::PipboyManager::GetSingleton();
			auto* ui = RE::UI::GetSingleton();
			const auto* player = RE::PlayerCharacter::GetSingleton();
			if (!manager || !manager->QPipboyActive() || !ui ||
				!ui->GetMenuOpen<RE::PipboyMenu>() || !player ||
				(!SessionState::IsForced() && !Hooks::ActorInPowerArmor(*player))) {
				return nullptr;
			}

			auto* geometry = RE::PowerArmorGeometry::GetSingleton();
			return geometry ? geometry->pipboyPAGlass.get() : nullptr;
		}

		void SetScreenCulled(RE::NiNode& a_screen, const bool a_culled)
		{
			a_screen.SetAppCulled(a_culled);
			RE::NiUpdateData updateData{};
			a_screen.Update(updateData);
		}

		void SuppressFlash()
		{
			auto* screen = GetReturnScreen();
			if (!screen) {
				return;
			}

			g_generation.fetch_add(1, std::memory_order_relaxed);
			g_screen.store(reinterpret_cast<std::uintptr_t>(screen), std::memory_order_relaxed);
			g_frames.store(2, std::memory_order_release);
			SetScreenCulled(*screen, true);
			DiagnosticLog(
				"Concealed PA Pip-Boy screen for terminal render-target handoff (screen={:p})",
				static_cast<const void*>(screen));
		}

		class MenuOpenCloseSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(
				const RE::MenuOpenCloseEvent& a_event,
				RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (!a_event.opening && a_event.menuName == RE::TerminalMenu::MENU_NAME) {
					SuppressFlash();
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		MenuOpenCloseSink g_sink;
	}

	void RegisterSink()
	{
		if (g_sinkRegistered.load(std::memory_order_relaxed)) {
			return;
		}

		if (auto* ui = RE::UI::GetSingleton()) {
			ui->RegisterSink(std::addressof(g_sink));
			g_sinkRegistered.store(true, std::memory_order_relaxed);
			DiagnosticLog("Registered PipboyMenu-frame terminal-return flash suppression");
		}
	}

	void Reset() noexcept
	{
		g_generation.fetch_add(1, std::memory_order_relaxed);
		g_frames.store(0, std::memory_order_relaxed);
		g_screen.store(0, std::memory_order_relaxed);
	}

	void Advance()
	{
		const auto frames = g_frames.load(std::memory_order_acquire);
		if (frames == 0) {
			return;
		}

		const auto generation = g_generation.load(std::memory_order_relaxed);
		const auto expectedScreen = g_screen.load(std::memory_order_relaxed);
		auto* screen = GetReturnScreen();
		if (!screen || reinterpret_cast<std::uintptr_t>(screen) != expectedScreen) {
			g_frames.store(0, std::memory_order_release);
			g_screen.store(0, std::memory_order_relaxed);
			return;
		}

		if (frames > 1) {
			SetScreenCulled(*screen, true);
			if (g_generation.load(std::memory_order_relaxed) == generation) {
				g_frames.store(frames - 1, std::memory_order_release);
				DiagnosticLog(
					"Held PA Pip-Boy screen concealed across a PipboyMenu frame (screen={:p})",
					static_cast<const void*>(screen));
			}
			return;
		}

		auto* ui = RE::UI::GetSingleton();
		if (!ui || ui->GetMenuOpen<RE::TerminalMenu>()) {
			SetScreenCulled(*screen, true);
			return;
		}

		SetScreenCulled(*screen, false);
		if (g_generation.load(std::memory_order_relaxed) == generation) {
			g_frames.store(0, std::memory_order_release);
			g_screen.store(0, std::memory_order_relaxed);
			DiagnosticLog(
				"Revealed PA Pip-Boy screen after terminal render-target handoff (screen={:p})",
				static_cast<const void*>(screen));
		}
	}
}
