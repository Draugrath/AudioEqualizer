#include "FrameworkMenu.h"
#include "Equalizer.h"
#include "Settings.h"
#include "Version.h"

#include <SKSEMenuFramework.h>

namespace Menu
{
	using namespace ImGuiMCP;

	struct ImDrawList : ImGuiMCP::ImDrawList
	{
		void AddLine(ImVec2 a_from, ImVec2 a_to, ImU32 a_color, float a_thickness = 1.0f) { ImDrawListManager::AddLine(this, a_from, a_to, a_color, a_thickness); }
		void AddRectFilled(ImVec2 a_min, ImVec2 a_max, ImU32 a_color) { ImDrawListManager::AddRectFilled(this, a_min, a_max, a_color, 0.0f, 0); }
		void AddQuadFilled(ImVec2 a_1, ImVec2 a_2, ImVec2 a_3, ImVec2 a_4, ImU32 a_color) { ImDrawListManager::AddQuadFilled(this, a_1, a_2, a_3, a_4, a_color); }
		void AddPolyline(const ImVec2* a_points, int a_count, ImU32 a_color, ImDrawFlags a_flags, float a_thickness) { ImDrawListManager::AddPolyline(this, a_points, a_count, a_color, a_flags, a_thickness); }
		void AddCircle(ImVec2 a_center, float a_radius, ImU32 a_color, int a_segments, float a_thickness) { ImDrawListManager::AddCircle(this, a_center, a_radius, a_color, a_segments, a_thickness); }
		void AddCircleFilled(ImVec2 a_center, float a_radius, ImU32 a_color, int a_segments = 0) { ImDrawListManager::AddCircleFilled(this, a_center, a_radius, a_color, a_segments); }
		void AddText(ImVec2 a_position, ImU32 a_color, const char* a_text) { ImDrawListManager::AddText(this, a_position, a_color, a_text); }
		void AddText(const ImFont* a_font, float a_size, ImVec2 a_position, ImU32 a_color, const char* a_text) { ImDrawListManager::AddText(this, a_font, a_size, a_position, a_color, a_text, nullptr, 0.0f, nullptr); }
		void PathArcTo(ImVec2 a_center, float a_radius, float a_from, float a_to, int a_segments = 0) { ImDrawListManager::PathArcTo(this, a_center, a_radius, a_from, a_to, a_segments); }
		void PathStroke(ImU32 a_color, ImDrawFlags a_flags, float a_thickness) { ImDrawListManager::PathStroke(this, a_color, a_flags, a_thickness); }
		void PushClipRect(ImVec2 a_min, ImVec2 a_max, bool a_intersect) { ImDrawListManager::PushClipRect(this, a_min, a_max, a_intersect); }
		void PopClipRect() { ImDrawListManager::PopClipRect(this); }
	};
}

namespace ImGui
{
	using namespace ImGuiMCP;

	inline ImGuiIO& GetIO() { return *ImGuiMCP::GetIO(); }
	inline Menu::ImDrawList* GetWindowDrawList() { return static_cast<Menu::ImDrawList*>(ImGuiMCP::GetWindowDrawList()); }
	inline ImVec2 CalcTextSize(const char* a_text) { return ImGuiMCP::CalcTextSize(a_text, nullptr, false, -1.0f); }

	inline float windowFontScale = 0.0f;

	inline void Tooltip(const char* a_text)
	{
		if (!ImGuiMCP::IsItemHovered(ImGuiHoveredFlags_ForTooltip) || !ImGuiMCP::BeginTooltip()) {
			return;
		}
		ImGuiMCP::SetWindowFontScale(1.0f);
		const float size = ImGuiMCP::GetFontSize();
		if (size > 18.0f) {
			ImGuiMCP::SetWindowFontScale(18.0f / size);
		}
		ImGuiMCP::TextUnformatted(a_text);
		ImGuiMCP::EndTooltip();
	}
}

namespace Menu
{
#include "EqWindow.inl"

	namespace
	{
		MENU_WINDOW       window = nullptr;
		bool              wasOpen = false;
		std::atomic<bool> waitingForKey{ false };

		std::uint32_t KeyToScan(std::uint32_t a_key)
		{
			const auto scan = MapVirtualKeyW(a_key, MAPVK_VK_TO_VSC_EX);
			return (scan >> 8) == 0xE0 ? (scan & 0xFF) | 0x80 : scan & 0xFF;
		}

		std::uint32_t ScanToKey(std::uint32_t a_scan)
		{
			return MapVirtualKeyW(a_scan >= 0x80 ? 0xE000 | (a_scan & 0x7F) : a_scan, MAPVK_VSC_TO_VK_EX);
		}

		std::string KeyName(std::uint32_t a_key)
		{
			const auto scan = KeyToScan(a_key);
			char       name[64]{};
			const LONG code = static_cast<LONG>(((scan & 0x7F) << 16) | (scan >= 0x80 ? 1 << 24 : 0));
			return GetKeyNameTextA(code, name, sizeof(name)) > 0 ? std::string(name) : Settings::GetKeyName(a_key);
		}

		void Follow()
		{
			if (!window) {
				return;
			}
			const bool open = window->IsOpen.load();
			if (open != wasOpen) {
				wasOpen = open;
				Equalizer::SetCapture(open && Settings::GetSingleton().enabled);
			}
		}

		void __stdcall RenderWindow()
		{
			const auto  version = Plugin::VERSION;
			const auto  key = KeyName(Settings::GetSingleton().toggleKey);
			const auto  title = std::format("AudioEqualizer v{}.{}.{} ({} to close)###AudioEqualizerMF", version.major(), version.minor(), version.patch(), key);
			const auto* display = ImGuiMCP::GetIO();
			ImGuiMCP::SetNextWindowSize(ImVec2((std::min)(display->DisplaySize.x - 80.0f, 880.0f), 0.0f), ImGuiCond_Always);
			ImGuiMCP::SetNextWindowPos(ImVec2(40.0f, display->DisplaySize.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.0f, 0.5f));
			ImGuiMCP::SetNextWindowBgAlpha(Settings::GetSingleton().windowAlpha);
			ImGuiMCP::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
			ImGuiMCP::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
			ImGuiMCP::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
			ImGuiMCP::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
			ImGuiMCP::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
			ImGuiMCP::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
			ImGuiMCP::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));
			ImGuiMCP::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 4.0f));
			ImGuiMCP::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(6.0f, 4.0f));
			ImGuiMCP::PushStyleColor(ImGuiCol_Text, IM_COL32(204, 204, 199, 255));
			ImGuiMCP::PushStyleColor(ImGuiCol_TextDisabled, IM_COL32(165, 165, 160, 255));
			ImGuiMCP::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(5, 5, 5, 255));
			ImGuiMCP::PushStyleColor(ImGuiCol_TitleBg, IM_COL32(0, 0, 0, 204));
			ImGuiMCP::PushStyleColor(ImGuiCol_TitleBgActive, IM_COL32(5, 5, 5, 204));
			ImGuiMCP::PushStyleColor(ImGuiCol_CheckMark, IM_COL32(199, 166, 92, 255));
			ImGuiMCP::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
			if (ImGuiMCP::Begin(title.c_str(), nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
				if (ImGui::windowFontScale <= 0.0f) {
					ImGuiMCP::SetWindowFontScale(1.0f);
					const float width = ImGuiMCP::CalcTextSize("Save Preset", nullptr, false, -1.0f).x;
					ImGui::windowFontScale = (std::min)({ 1.0f, 77.0f / width, 18.0f / ImGuiMCP::GetFontSize() });
				}
				ImGuiMCP::SetWindowFontScale(ImGui::windowFontScale);
				Render();
			}
			ImGuiMCP::End();
			ImGuiMCP::PopStyleColor(7);
			ImGuiMCP::PopStyleVar(9);
		}

		void __stdcall RenderPage()
		{
			auto& settings = Settings::GetSingleton();
			if (ImGuiMCP::Button("Open equalizer")) {
				window->IsOpen = true;
			}

			const bool waiting = waitingForKey.load();
			ImGuiMCP::Text("Open key");
			ImGuiMCP::SameLine();
			const auto label = std::format("{}###OpenKey", waiting ? std::string("Press a key...") : KeyName(settings.toggleKey));
			if (ImGuiMCP::Button(label.c_str())) {
				waitingForKey = true;
			}
			if (waiting) {
				ImGuiMCP::SameLine();
				ImGuiMCP::TextDisabled("Esc cancels.");
			}

			if (ImGuiMCP::Checkbox("Enabled", &settings.enabled)) {
				Equalizer::Apply();
				Equalizer::SetCapture(window->IsOpen.load() && settings.enabled);
				settings.Save();
			}
		}

		bool __stdcall OnInput(RE::InputEvent* a_event)
		{
			if (!a_event || a_event->device != RE::INPUT_DEVICE::kKeyboard) {
				return false;
			}
			const auto* button = a_event->AsButtonEvent();
			if (!button || !button->IsDown()) {
				return false;
			}
			auto&      settings = Settings::GetSingleton();
			const auto code = button->GetIDCode();
			if (waitingForKey.exchange(false)) {
				const auto key = ScanToKey(code);
				if (code != RE::BSWin32KeyboardDevice::Key::kEscape && key != 0) {
					settings.toggleKey = key;
					settings.Save();
				}
				return true;
			}
			if (code != KeyToScan(settings.toggleKey)) {
				return false;
			}
			window->IsOpen = !window->IsOpen.load();
			return true;
		}

		void __stdcall OnHud()
		{
			Follow();
		}
	}

	void Register()
	{
		if (!SKSEMenuFramework::IsInstalled() || !GetMenuFrameworkModule()) {
			logger::error("SKSE Menu Framework is not installed. The EQ keeps running from the saved settings, without a window.");
			return;
		}
		SKSEMenuFramework::SetSection("Audio Equalizer");
		SKSEMenuFramework::AddSectionItem("Equalizer", RenderPage);
		window = SKSEMenuFramework::AddWindow(RenderWindow, true);
		SKSEMenuFramework::AddInputEvent(OnInput);
		SKSEMenuFramework::AddHudElement(OnHud);
		logger::info("Registered with SKSE Menu Framework.");
	}
}
