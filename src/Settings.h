#pragma once

#include "Eq.h"
#include "PCH.h"

#include <cstdio>

class Settings
{
public:
	static Settings& GetSingleton()
	{
		static Settings instance;
		return instance;
	}

	static Settings& GetFastSnapshot() { return GetSingleton(); }

	static std::string GetKeyName(std::uint32_t a_key)
	{
		if (a_key >= VK_F1 && a_key <= VK_F12) {
			return std::format("F{}", a_key - VK_F1 + 1);
		}
		return std::format("0x{:02X}", a_key);
	}

	static Eq::Bands DefaultBands()
	{
		Eq::Bands bands{};
		const std::array<float, Eq::kBands> frequencies{ 80.0f, 200.0f, 500.0f, 1000.0f, 2500.0f, 5000.0f, 10000.0f, 14000.0f };
		for (std::size_t band = 0; band < Eq::kBands; ++band) {
			bands[band].frequency = frequencies[band];
		}
		bands.front().type = Eq::Type::kLowShelf;
		bands.back().type = Eq::Type::kHighShelf;
		return bands;
	}

	void Load()
	{
		std::ifstream file(kPath);
		std::string   line;
		while (std::getline(file, line)) {
			const auto split = line.find('=');
			if (split == std::string::npos || line.starts_with('#')) {
				continue;
			}
			auto trim = [](std::string a_text) {
				const auto first = a_text.find_first_not_of(" \t\r");
				const auto last = a_text.find_last_not_of(" \t\r");
				return first == std::string::npos ? std::string{} : a_text.substr(first, last - first + 1);
			};
			const auto key = trim(line.substr(0, split));
			const auto value = trim(line.substr(split + 1));
			try {
				if (key == "bEnabled") {
					enabled = value == "true" || value == "1";
				} else if (key == "fInputGain") {
					const float gain = std::stof(value);
					inputGain = std::isfinite(gain) ? std::clamp(gain, -24.0f, 24.0f) : 0.0f;
				} else if (key == "fOutputGain") {
					const float gain = std::stof(value);
					outputGain = std::isfinite(gain) ? std::clamp(gain, -24.0f, 24.0f) : 0.0f;
				} else if (key == "bAutoGain") {
					autoGain = value == "true" || value == "1";
				} else if (key == "fBalance") {
					const float amount = std::stof(value);
					balance = std::isfinite(amount) ? std::clamp(amount, 0.0f, 100.0f) : 0.0f;
				} else if (key == "fWindowScale") {
					const float scale = std::stof(value);
					windowScale = std::isfinite(scale) ? std::clamp(scale, 0.5f, 3.0f) : 1.0f;
				} else if (key == "iToggleKey") {
					toggleKey = static_cast<std::uint32_t>(std::stoul(value));
				} else if (key.starts_with("Band")) {
					const auto index = static_cast<std::size_t>(std::stoul(key.substr(4)));
					int        on = 1, type = 0;
					float      frequency = 1000.0f, gain = 0.0f, q = 1.0f;
					int        slope = 12;
					if (index < Eq::kBands && std::sscanf(value.c_str(), "%d, %d, %f, %f, %f, %d", &on, &type, &frequency, &gain, &q, &slope) >= 5 &&
						std::isfinite(frequency) && std::isfinite(gain) && std::isfinite(q)) {
						auto& band = bands[index];
						band.on = on != 0;
						band.type = static_cast<Eq::Type>(std::clamp(type, 0, 4));
						band.frequency = std::clamp(frequency, 20.0f, 20000.0f);
						band.gain = std::clamp(gain, -18.0f, 18.0f);
						band.q = std::clamp(q, 0.1f, 10.0f);
						band.slope = slope >= 96 ? 96 : (slope >= 48 ? 48 : (slope >= 24 ? 24 : 12));
					}
				}
			} catch (...) {
			}
		}
	}

	void Save() const
	{
		std::ofstream file(kPath, std::ios::trunc);
		file << "# Audio Equalizer settings. The in-game Save button rewrites this file.\n\n";
		file << "# Turns the equalizer on or off.\nbEnabled = " << (enabled ? "true" : "false") << "\n\n";
		file << "# Input gain in dB before the EQ, from -24 to 24.\nfInputGain = " << inputGain << "\n\n";
		file << "# Output gain in dB after the EQ, from -24 to 24.\nfOutputGain = " << outputGain << "\n\n";
		file << "# Spectral balance toward pink noise in percent, 0 turns it off.\nfBalance = " << balance << "\n\n";
		file << "# Automatic gain, keeps the loudness level when the EQ or Balance changes it.\nbAutoGain = " << (autoGain ? "true" : "false") << "\n\n";
		file << "# Key that opens the menu, as a Windows key code (115 = F4).\niToggleKey = " << toggleKey << "\n\n";
		file << "# Size of the equalizer window: 0.75, 1, 1.25, 1.5, 1.75, 2, 2.5 or 3 (2 suits 4K screens).\nfWindowScale = " << windowScale << "\n\n";
		file << "# Each band: on (0 or 1), type (0 bell, 1 low shelf, 2 high shelf, 3 low pass, 4 high pass), frequency in Hz, gain in dB, Q, slope for low and high pass (12, 24, 48 or 96 dB per octave).\n";
		for (std::size_t index = 0; index < Eq::kBands; ++index) {
			const auto& band = bands[index];
			file << "Band" << index << " = " << (band.on ? 1 : 0) << ", " << static_cast<int>(band.type) << ", " << band.frequency << ", " << band.gain << ", " << band.q << ", " << band.slope << "\n";
		}
	}

	bool          enabled{ true };
	float         inputGain{ 0.0f };
	float         outputGain{ 0.0f };
	float         balance{ 0.0f };
	bool          autoGain{ false };
	std::uint32_t toggleKey{ VK_F4 };
	float         windowAlpha{ 0.70f };
	float         windowScale{ 1.0f };
	Eq::Bands     bands{ DefaultBands() };

private:
	static constexpr const char* kPath = "Data/SKSE/Plugins/AudioEqualizer.ini";
};
