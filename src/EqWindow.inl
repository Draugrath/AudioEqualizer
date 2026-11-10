	namespace
	{
		float U(float a_pixels) { return a_pixels * ImGui::windowScale; }
		float GraphWidth() { return U(860.0f); }
		float GraphHeight() { return U(360.0f); }
		constexpr float kTopDb = 24.0f;
		constexpr float kBottomDb = -36.0f;
		constexpr float kRangeDb = 18.0f;
		constexpr double kSpectrumTilt = 4.5;
		constexpr ImU32 kColors[Eq::kBands]{
			IM_COL32(235, 90, 90, 255), IM_COL32(240, 160, 60, 255), IM_COL32(230, 215, 80, 255), IM_COL32(110, 215, 100, 255),
			IM_COL32(80, 205, 200, 255), IM_COL32(90, 150, 240, 255), IM_COL32(165, 110, 240, 255), IM_COL32(235, 100, 190, 255)
		};

		float FrequencyToX(float a_frequency) { return std::log(a_frequency / Eq::kGraphLowHz) / std::log(Eq::kGraphHighHz / Eq::kGraphLowHz) * GraphWidth(); }
		float XToFrequency(float a_x) { return Eq::kGraphLowHz * std::pow(Eq::kGraphHighHz / Eq::kGraphLowHz, std::clamp(a_x / GraphWidth(), 0.0f, 1.0f)); }
		float DbToY(float a_db) { return (kTopDb - a_db) / (kTopDb - kBottomDb) * GraphHeight(); }
		float YToDb(float a_y) { return std::clamp(kTopDb - a_y / GraphHeight() * (kTopDb - kBottomDb), -kRangeDb, kRangeDb); }
		bool  HasGain(const Eq::Band& a_band) { return a_band.type != Eq::Type::kLowPass && a_band.type != Eq::Type::kHighPass; }

		bool DrawGraph(Settings& a_settings, int& a_selected)
		{
			bool         changed = false;
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			ImGui::InvisibleButton("##graph", ImVec2(GraphWidth(), GraphHeight()));
			const bool hovered = ImGui::IsItemHovered();
			const bool active = ImGui::IsItemActive();
			const auto mouse = ImGui::GetIO().MousePos;
			auto*      draw = ImGui::GetWindowDrawList();
			draw->AddRectFilled(origin, ImVec2(origin.x + GraphWidth(), origin.y + GraphHeight()), IM_COL32(24, 24, 28, 170));
			const struct { float frequency; const char* label; } kMarks[]{ { 10.0f, "10" }, { 20.0f, "20" }, { 50.0f, "50" }, { 100.0f, "100" }, { 200.0f, "200" }, { 500.0f, "500" }, { 1000.0f, "1k" }, { 2000.0f, "2k" }, { 5000.0f, "5k" }, { 10000.0f, "10k" }, { 20000.0f, "20k" } };
			for (const auto& mark : kMarks) {
				const float x = origin.x + FrequencyToX(mark.frequency);
				draw->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + GraphHeight()), IM_COL32(70, 70, 76, 120));
				draw->AddText(ImVec2(x + U(3.0f), origin.y + GraphHeight() - U(16.0f)), IM_COL32(135, 135, 140, 255), mark.label);
			}
			for (float db : { 18.0f, 12.0f, 6.0f, 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -30.0f }) {
				const float y = origin.y + DbToY(db);
				draw->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + GraphWidth(), y), db == 0.0f ? IM_COL32(170, 170, 175, 210) : IM_COL32(70, 70, 76, 120));
				const auto text = std::format("{:+.0f}", db);
				draw->AddText(ImVec2(origin.x + GraphWidth() - U(30.0f), y - U(14.0f)), db == 0.0f ? IM_COL32(190, 190, 195, 255) : IM_COL32(135, 135, 140, 255), text.c_str());
			}

			static bool dragging = false;
			if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				float nearest = U(15.0f) * U(15.0f);
				for (int band = 0; band < static_cast<int>(Eq::kBands); ++band) {
					const auto& item = a_settings.bands[band];
					const float dx = origin.x + FrequencyToX(item.frequency) - mouse.x;
					const float dy = origin.y + DbToY(HasGain(item) ? item.gain : 0.0f) - mouse.y;
					if (dx * dx + dy * dy < nearest) {
						nearest = dx * dx + dy * dy;
						a_selected = band;
						dragging = true;
					}
				}
				if (dragging && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
					a_settings.bands[a_selected] = Settings::DefaultBands()[a_selected];
					dragging = false;
					changed = true;
				}
			}
			if (!active) {
				dragging = false;
			}
			auto& selected = a_settings.bands[a_selected];
			if (dragging) {
				selected.frequency = std::clamp(XToFrequency(mouse.x - origin.x), 20.0f, 20000.0f);
				if (HasGain(selected)) {
					selected.gain = YToDb(mouse.y - origin.y);
				}
				changed = true;
			}
			if (hovered && ImGui::GetIO().MouseWheel != 0.0f) {
				selected.q = std::clamp(selected.q * std::pow(1.15f, ImGui::GetIO().MouseWheel), 0.1f, 10.0f);
				changed = true;
			}

			static auto                                   shown = [] {
				std::array<float, Eq::kSpectrumPoints> values{};
				values.fill(-0.15f);
				return values;
			}();
			static const auto                             tilt = [] {
				std::array<float, Eq::kSpectrumPoints> offsets{};
				for (std::size_t point = 0; point < Eq::kSpectrumPoints; ++point) {
					const double hz = Eq::kGraphLowHz * std::pow(static_cast<double>(Eq::kGraphHighHz) / Eq::kGraphLowHz, (static_cast<double>(point) + 0.5) / Eq::kSpectrumPoints);
					offsets[point] = static_cast<float>(kSpectrumTilt * std::log2(hz / 1000.0));
				}
				return offsets;
			}();
			std::array<float, Eq::kSpectrumPoints>        fresh{};
			if (a_settings.enabled) {
				Equalizer::ReadSpectrum(fresh);
			} else {
				shown.fill(-0.15f);
			}
			ImVec2 outline[Eq::kSpectrumPoints];
			draw->PushClipRect(origin, ImVec2(origin.x + GraphWidth(), origin.y + GraphHeight()), true);
			const auto fillFlags = draw->Flags;
			draw->Flags &= ~ImDrawListFlags_AntiAliasedFill;
			for (std::size_t point = 0; point < Eq::kSpectrumPoints; ++point) {
				if (a_settings.enabled) {
					const float value = fresh[point] <= -119.0f ? -0.15f : std::clamp((fresh[point] + tilt[point] + 100.0f) / 80.0f, -0.15f, 1.0f);
					shown[point] = value > shown[point] ? value : shown[point] + (value - shown[point]) * 0.08f;
				}
				const float left = origin.x + GraphWidth() * static_cast<float>(point) / Eq::kSpectrumPoints;
				const float right = origin.x + GraphWidth() * static_cast<float>(point + 1) / Eq::kSpectrumPoints;
				outline[point] = ImVec2((left + right) * 0.5f, origin.y + GraphHeight() * (1.0f - shown[point]));
				if (point > 0) {
					draw->AddQuadFilled(outline[point - 1], outline[point], ImVec2(outline[point].x, origin.y + GraphHeight()), ImVec2(outline[point - 1].x, origin.y + GraphHeight()), IM_COL32(90, 120, 130, 90));
				}
			}
			draw->Flags = fillFlags;
			for (std::size_t point = 1; point < Eq::kSpectrumPoints; ++point) {
				draw->AddLine(outline[point - 1], outline[point], IM_COL32(118, 142, 150, 255), U(1.0f));
			}
			draw->PopClipRect();

			const float rate = static_cast<float>(Equalizer::SampleRate());
			const float topHz = rate * 0.499f;
			const float zero = origin.y + DbToY(0.0f);
			const float floorY = origin.y + GraphHeight();
			static Eq::Bands                                      cachedBands{};
			static float                                          cachedRate = 0.0f;
			static std::array<std::array<float, 256>, Eq::kBands + 1> curves{};
			if (rate != cachedRate || a_settings.bands != cachedBands) {
				cachedRate = rate;
				cachedBands = a_settings.bands;
				const auto store = [&](const Eq::Filter& a_filter, std::size_t a_slot) {
					for (int i = 0; i < 256; ++i) {
						const float x = GraphWidth() * static_cast<float>(i) / 255.0f;
						curves[a_slot][i] = static_cast<float>(Eq::ResponseDb(a_filter, (std::min)(XToFrequency(x), topHz), rate));
					}
				};
				for (std::size_t band = 0; band < Eq::kBands; ++band) {
					Eq::Bands only{};
					for (auto& other : only) {
						other.on = false;
					}
					only[band] = a_settings.bands[band];
					store(Eq::Design(only, rate), band);
				}
				store(Eq::Design(a_settings.bands, rate), Eq::kBands);
			}
			draw->PushClipRect(origin, ImVec2(origin.x + GraphWidth(), floorY), true);
			for (int band = 0; band < static_cast<int>(Eq::kBands); ++band) {
				const auto& item = a_settings.bands[band];
				if (!item.on || (HasGain(item) && item.gain == 0.0f)) {
					continue;
				}
				const ImU32 color = kColors[band];
				const ImU32 fill = (color & 0x00FFFFFFu) | (band == a_selected ? 0x50000000u : 0x28000000u);
				ImVec2      line[256];
				const auto  drawFlags = draw->Flags;
				draw->Flags &= ~ImDrawListFlags_AntiAliasedFill;
				for (int i = 0; i < 256; ++i) {
					const float x = GraphWidth() * static_cast<float>(i) / 255.0f;
					const float db = curves[band][i];
					line[i] = ImVec2(origin.x + x, origin.y + (std::min)(DbToY(db), GraphHeight() + U(40.0f)));
					if (i > 0) {
						draw->AddQuadFilled(ImVec2(line[i - 1].x, zero), line[i - 1], line[i], ImVec2(line[i].x, zero), fill);
					}
				}
				draw->Flags = drawFlags;
				draw->AddPolyline(line, 256, (color & 0x00FFFFFFu) | 0x90000000u, 0, U(1.0f));
			}

			ImVec2 points[256];
			for (int i = 0; i < 256; ++i) {
				const float x = GraphWidth() * static_cast<float>(i) / 255.0f;
				const float db = curves[Eq::kBands][i];
				points[i] = ImVec2(origin.x + x, origin.y + (std::min)(DbToY(db), GraphHeight() + U(40.0f)));
			}
			draw->AddPolyline(points, 256, IM_COL32(245, 195, 70, 255), 0, U(2.5f));
			if (a_settings.enabled && a_settings.balance > 0.0f) {
				constexpr int                               kCurvePoints = 160;
				static std::array<float, Eq::kBalanceBands> drawnGains{};
				static std::array<float, kCurvePoints>      drawnDb{};
				static float                                drawnRate = 0.0f;
				std::array<float, Eq::kBalanceBands>        balance{};
				const std::size_t                           count = Equalizer::ReadBalance(balance);
				bool                                        stale = rate != drawnRate;
				for (std::size_t band = 0; band < Eq::kBalanceBands; ++band) {
					stale |= std::fabs(balance[band] - drawnGains[band]) > 0.005f;
				}
				if (stale) {
					std::array<Eq::Coefficients, Eq::kBalanceBands> bells{};
					std::size_t                                     working = 0;
					for (std::size_t band = 0; band < count; ++band) {
						if (std::fabs(balance[band]) >= 0.01f) {
							bells[working++] = Eq::BalanceBell(band, balance[band], rate);
						}
					}
					for (int i = 0; i < kCurvePoints; ++i) {
						const float  x = GraphWidth() * static_cast<float>(i) / (kCurvePoints - 1);
						const double w = 2.0 * Eq::kPi * (std::min)(XToFrequency(x), topHz) / rate;
						double       db = 0.0;
						for (std::size_t band = 0; band < working; ++band) {
							db += Eq::BiquadDb(bells[band], w);
						}
						drawnDb[i] = static_cast<float>(db);
					}
					drawnGains = balance;
					drawnRate = rate;
				}
				ImVec2 curve[kCurvePoints];
				for (int i = 0; i < kCurvePoints; ++i) {
					curve[i] = ImVec2(origin.x + GraphWidth() * static_cast<float>(i) / (kCurvePoints - 1), origin.y + DbToY(drawnDb[i]));
				}
				if (count > 0) {
					draw->AddPolyline(curve, kCurvePoints, IM_COL32(150, 200, 235, 210), 0, U(1.5f));
				}
			}
			draw->PopClipRect();

			for (int band = 0; band < static_cast<int>(Eq::kBands); ++band) {
				const auto&  item = a_settings.bands[band];
				const float  height = HasGain(item) ? item.gain : 0.0f;
				const ImVec2 point(origin.x + FrequencyToX(item.frequency), origin.y + DbToY(height));
				const ImU32  color = item.on ? kColors[band] : IM_COL32(95, 95, 100, 255);
				if (band == a_selected) {
					draw->AddCircle(point, U(10.0f), IM_COL32(255, 255, 255, 230), 0, U(1.5f));
				}
				draw->AddCircleFilled(point, U(6.5f), color);
			}
			return changed;
		}
	}

	namespace
	{
		struct Meter
		{
			float level{ -120.0f };
			float hold{ -120.0f };
			float holdTime{ 0.0f };
		};

		struct KnobSetup
		{
			float min;
			float max;
			float reset;
			bool  logarithmic;
			bool  centered;
			float radius;
			ImU32 color;
			bool  usable;
		};

		float KnobPosition(float a_value, const KnobSetup& a_knob)
		{
			const float position = a_knob.logarithmic ? std::log(a_value / a_knob.min) / std::log(a_knob.max / a_knob.min) : (a_value - a_knob.min) / (a_knob.max - a_knob.min);
			return std::clamp(position, 0.0f, 1.0f);
		}

		float KnobValue(float a_position, const KnobSetup& a_knob)
		{
			return a_knob.logarithmic ? a_knob.min * std::pow(a_knob.max / a_knob.min, a_position) : a_knob.min + (a_knob.max - a_knob.min) * a_position;
		}

		bool Knob(const char* a_id, float& a_value, const KnobSetup& a_knob)
		{
			constexpr float kStart = 2.3561945f;
			constexpr float kSweep = 4.712389f;
			const ImVec2    origin = ImGui::GetCursorScreenPos();
			const float     size = a_knob.radius * 2.0f;
			ImGui::InvisibleButton(a_id, ImVec2(size, size));
			bool changed = false;
			if (a_knob.usable) {
				if (ImGui::IsItemActive() && ImGui::GetIO().MouseDelta.y != 0.0f) {
					a_value = KnobValue(std::clamp(KnobPosition(a_value, a_knob) - ImGui::GetIO().MouseDelta.y * 0.004f / ImGui::windowScale, 0.0f, 1.0f), a_knob);
					changed = true;
				}
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
					a_value = a_knob.reset;
					changed = true;
				}
			}

			auto*        draw = ImGui::GetWindowDrawList();
			const ImVec2 center(origin.x + a_knob.radius, origin.y + a_knob.radius);
			const float  position = KnobPosition(a_value, a_knob);
			const float  angle = kStart + kSweep * position;
			const ImU32  color = a_knob.usable ? a_knob.color : IM_COL32(95, 95, 100, 255);
			const bool   hot = a_knob.usable && (ImGui::IsItemHovered() || ImGui::IsItemActive());
			draw->PathArcTo(center, a_knob.radius - U(1.5f), kStart, kStart + kSweep, 40);
			draw->PathStroke(IM_COL32(70, 70, 78, 255), 0, U(3.0f));
			const float from = a_knob.centered ? kStart + kSweep * 0.5f : kStart;
			if (std::fabs(angle - from) > 0.01f) {
				draw->PathArcTo(center, a_knob.radius - U(1.5f), (std::min)(from, angle), (std::max)(from, angle), 40);
				draw->PathStroke(color, 0, U(3.0f));
			}
			draw->AddCircleFilled(center, a_knob.radius - U(5.5f), hot ? IM_COL32(86, 86, 96, 255) : IM_COL32(64, 64, 72, 255), 32);
			const ImVec2 direction(std::cos(angle), std::sin(angle));
			draw->AddLine(ImVec2(center.x + direction.x * a_knob.radius * 0.28f, center.y + direction.y * a_knob.radius * 0.28f),
				ImVec2(center.x + direction.x * (a_knob.radius - U(6.5f)), center.y + direction.y * (a_knob.radius - U(6.5f))), a_knob.usable ? IM_COL32(240, 240, 240, 255) : IM_COL32(125, 125, 130, 255), U(2.0f));
			return changed;
		}

		void GreyOut(ImDrawList* a_draw, int a_first)
		{
			for (int vertex = a_first; vertex < a_draw->VtxBuffer.Size; ++vertex) {
				auto&      color = a_draw->VtxBuffer.Data[vertex].col;
				const auto red = (color >> IM_COL32_R_SHIFT) & 0xFF;
				const auto green = (color >> IM_COL32_G_SHIFT) & 0xFF;
				const auto blue = (color >> IM_COL32_B_SHIFT) & 0xFF;
				const auto grey = ((red * 77 + green * 150 + blue * 29) >> 8) * 3 / 5;
				color = (color & IM_COL32_A_MASK) | IM_COL32(grey, grey, grey, 0);
			}
		}

		void CenteredText(ImDrawList* a_draw, float a_center, float a_y, ImU32 a_color, const char* a_text)
		{
			a_draw->AddText(ImVec2(a_center - ImGui::CalcTextSize(a_text).x * 0.5f, a_y), a_color, a_text);
		}

		bool LevelGroup(const char* a_id, const char* a_label, float a_x, float a_y, float& a_gain, float a_peak, Meter& a_meter, float a_warning, bool a_live, bool* a_auto = nullptr)
		{
			constexpr float kLow = -60.0f;
			constexpr float kHigh = 6.0f;
			const float     kWidth = U(80.0f);
			const float     kHeight = U(14.0f);
			ImGui::PushID(a_id);
			bool changed = false;
			if (a_auto) {
				ImGui::SetCursorPos(ImVec2(a_x, a_y + U(9.0f)));
				const ImVec2 corner = ImGui::GetCursorScreenPos();
				if (ImGui::InvisibleButton("##auto", ImVec2(U(18.0f), U(18.0f)))) {
					*a_auto = !*a_auto;
					changed = true;
				}
				const auto autoTip = *a_auto ? std::format("Auto gain: compensates the level change of the EQ and Balance.\nCurrent compensation: {:+.1f} dB.", Equalizer::AutoGainDb()) : std::string("Auto gain: compensates the level change of the EQ and Balance.\nOff.");
				ImGui::Tooltip(autoTip.c_str());
				const bool   hot = ImGui::IsItemHovered();
				auto*        icon = ImGui::GetWindowDrawList();
				const ImVec2 center(corner.x + U(9.0f), corner.y + U(9.0f));
				icon->AddCircleFilled(center, U(9.0f), *a_auto ? IM_COL32(245, 195, 70, 255) : (hot ? IM_COL32(72, 72, 84, 255) : IM_COL32(52, 52, 60, 255)), 48);
				constexpr float kLetterScale = 0.72f;
				const ImVec2    letter = ImGui::CalcTextSize("A");
				icon->AddText(ImGui::GetFont(), ImGui::GetFontSize() * kLetterScale, ImVec2(center.x - letter.x * kLetterScale * 0.5f, center.y - letter.y * kLetterScale * 0.5f), *a_auto ? IM_COL32(20, 20, 20, 255) : IM_COL32(200, 200, 205, 255), "A");
				a_x += U(22.0f);
			}
			ImGui::SetCursorPos(ImVec2(a_x, a_y + U(1.0f)));
			ImGui::TextUnformatted(a_label);
			const auto gainText = std::format("{:+.1f} dB", a_gain);
			ImGui::SetCursorPos(ImVec2(a_x, a_y + U(18.0f)));
			ImGui::TextDisabled("%s", gainText.c_str());
			ImGui::SetCursorPos(ImVec2(a_x + U(56.0f), a_y));
			changed |= Knob("##gain", a_gain, { -24.0f, 24.0f, 0.0f, false, true, U(18.0f), IM_COL32(245, 195, 70, 255), true });
			ImGui::Tooltip(a_auto ? "Output gain after the EQ.\nDrag to change, double-click to reset." : "Input gain before the EQ.\nDrag to change, double-click to reset.");

			if (a_live) {
				const float delta = ImGui::GetIO().DeltaTime;
				const float db = 20.0f * std::log10((std::max)(a_peak, 0.000001f));
				a_meter.level = db > a_meter.level ? db : (std::max)(db, a_meter.level - 30.0f * delta);
				a_meter.holdTime += delta;
				if (a_meter.level >= a_meter.hold) {
					a_meter.hold = a_meter.level;
					a_meter.holdTime = 0.0f;
				} else if (a_meter.holdTime > 1.5f) {
					a_meter.hold = (std::max)(a_meter.level, a_meter.hold - 30.0f * delta);
				}
			} else {
				a_meter = {};
			}

			ImGui::SetCursorPos(ImVec2(a_x + U(102.0f), a_y + U(11.0f)));
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(kWidth, kHeight));
			auto*       draw = ImGui::GetWindowDrawList();
			const auto  place = [&](float a_db) { return origin.x + std::clamp((a_db - kLow) / (kHigh - kLow), 0.0f, 1.0f) * kWidth; };
			const float bottom = origin.y + kHeight;
			draw->AddRectFilled(origin, ImVec2(origin.x + kWidth, bottom), IM_COL32(24, 24, 28, 235));
			const ImU32 color = a_meter.level >= a_warning ? IM_COL32(230, 80, 70, 255) : (a_meter.level >= -12.0f ? IM_COL32(230, 200, 80, 255) : IM_COL32(100, 200, 120, 255));
			draw->AddRectFilled(origin, ImVec2(place(a_meter.level), bottom), color);
			if (a_meter.hold > kLow) {
				const float x = place(a_meter.hold);
				draw->AddLine(ImVec2(x, origin.y), ImVec2(x, bottom), IM_COL32(240, 240, 240, 230), U(1.5f));
			}
			const float zero = place(0.0f);
			draw->AddLine(ImVec2(zero, origin.y), ImVec2(zero, bottom), IM_COL32(190, 190, 195, 200));
			ImGui::SetCursorPos(ImVec2(a_x + U(188.0f), a_y + U(9.0f)));
			if (a_meter.hold > -100.0f) {
				ImGui::Text("%+.1f dB", a_meter.hold);
			} else {
				ImGui::TextDisabled("no signal");
			}
			ImGui::PopID();
			return changed;
		}

		bool BalanceGroup(float a_x, float a_y, float& a_amount)
		{
			ImGui::PushID("balance");
			ImGui::SetCursorPos(ImVec2(a_x, a_y + U(1.0f)));
			ImGui::TextUnformatted("Balance");
			ImGui::SetCursorPos(ImVec2(a_x, a_y + U(18.0f)));
			if (a_amount > 0.0f) {
				ImGui::TextDisabled("%.0f%%", a_amount);
			} else {
				ImGui::TextDisabled("Off");
			}
			ImGui::SetCursorPos(ImVec2(a_x + U(56.0f), a_y));
			const bool changed = Knob("##amount", a_amount, { 0.0f, 100.0f, 0.0f, false, false, U(18.0f), IM_COL32(245, 195, 70, 255), true });
			ImGui::Tooltip("Balance: spectral balance toward a pink noise curve, zero latency.\nCuts up to 6 dB, boosts up to 3 dB. 0% is off.\nDrag to change, double-click to reset.");
			ImGui::PopID();
			return changed;
		}

		bool BandPanel(Settings& a_settings, int& a_selected, const ImVec2& a_graph)
		{
			const float     kPanelWidth = U(374.0f);
			const float     kPanelHeight = U(76.0f);
			const float     kKnobRadius = U(18.0f);
			const float     kKnobTop = U(5.0f);
			bool            changed = false;

			ImGui::SetCursorScreenPos(ImVec2(a_graph.x + (GraphWidth() - kPanelWidth) * 0.5f, a_graph.y + GraphHeight() - kPanelHeight - U(26.0f)));
			ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(40, 40, 48, 60));
			ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(46, 46, 54, 90));
			ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, U(8.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(U(6.0f), U(2.0f)));
			ImGui::BeginChild("##bandPanel", ImVec2(kPanelWidth, kPanelHeight), 0, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

			auto&                        band = a_settings.bands[a_selected];
			static constexpr const char* kTypes[]{ "Bell", "Low shelf", "High shelf", "Low pass", "High pass" };
			static constexpr int         kSlopes[]{ 12, 24, 48, 96 };
			static constexpr const char* kSlopeNames[]{ "12 dB/oct", "24 dB/oct", "48 dB/oct", "96 dB/oct" };
			int                          type = static_cast<int>(band.type);
			int                          slope = band.slope >= 96 ? 3 : (band.slope >= 48 ? 2 : (band.slope >= 24 ? 1 : 0));

			ImGui::SetCursorPos(ImVec2(U(8.0f), U(6.0f)));
			changed |= ImGui::Checkbox("Band on", &band.on);
			ImGui::Tooltip("Turns this band on or off.");
			ImGui::SetCursorPos(ImVec2(U(8.0f), U(29.0f)));
			ImGui::SetNextItemWidth(U(96.0f));
			const bool typeChanged = ImGui::Combo("##type", &type, kTypes, 5);
			ImGui::Tooltip("Filter type of this band.");
			if (typeChanged) {
				band.type = static_cast<Eq::Type>(type);
				changed = true;
			}
			ImGui::BeginDisabled(HasGain(band));
			ImGui::SetCursorPos(ImVec2(U(8.0f), U(52.0f)));
			ImGui::SetNextItemWidth(U(96.0f));
			const bool slopeChanged = ImGui::Combo("##slope", &slope, kSlopeNames, 4);
			ImGui::Tooltip("Slope of the low and high pass filters.");
			if (slopeChanged) {
				band.slope = kSlopes[slope];
				changed = true;
			}
			ImGui::EndDisabled();

			const ImU32 accent = kColors[a_selected];
			const auto  defaults = Settings::DefaultBands();
			const float knobX[]{ U(118.0f), U(176.0f), U(230.0f) };
			ImGui::SetCursorPos(ImVec2(knobX[0], kKnobTop));
			changed |= Knob("##frequency", band.frequency, { 20.0f, 20000.0f, defaults[a_selected].frequency, true, false, kKnobRadius, accent, true });
			ImGui::Tooltip("Frequency of this band.\nDrag to change, double-click to reset.");
			ImGui::SetCursorPos(ImVec2(knobX[1], kKnobTop));
			changed |= Knob("##bandGain", band.gain, { -kRangeDb, kRangeDb, 0.0f, false, true, kKnobRadius, accent, HasGain(band) });
			ImGui::Tooltip("Gain of this band. Not used by low and high pass.\nDrag to change, double-click to reset.");
			ImGui::SetCursorPos(ImVec2(knobX[2], kKnobTop));
			changed |= Knob("##q", band.q, { 0.1f, 10.0f, 1.0f, true, false, kKnobRadius, accent, true });
			ImGui::Tooltip("Width of this band. Higher Q is narrower.\nDrag to change, double-click to reset.");

			auto*        draw = ImGui::GetWindowDrawList();
			const ImVec2 corner = ImGui::GetWindowPos();
			const float  valueY = corner.y + kKnobTop + kKnobRadius * 2.0f + U(2.0f);
			const float  labelY = valueY + (std::max)(U(13.0f), ImGui::GetFontSize() * 0.8f);
			const auto   text = IM_COL32(225, 225, 228, 255);
			const auto   dim = IM_COL32(150, 150, 156, 255);
			const auto   frequency = band.frequency >= 10000.0f ? std::format("{:.1f} kHz", band.frequency / 1000.0f) : band.frequency >= 1000.0f ? std::format("{:.2f} kHz", band.frequency / 1000.0f) : std::format("{:.0f} Hz", band.frequency);
			const auto   gain = HasGain(band) ? std::format("{:+.1f} dB", band.gain) : std::string("-");
			const auto   q = std::format("{:.2f}", band.q);
			CenteredText(draw, corner.x + knobX[0] + kKnobRadius, valueY, text, frequency.c_str());
			CenteredText(draw, corner.x + knobX[1] + kKnobRadius, valueY, HasGain(band) ? text : dim, gain.c_str());
			CenteredText(draw, corner.x + knobX[2] + kKnobRadius, valueY, text, q.c_str());
			CenteredText(draw, corner.x + knobX[0] + kKnobRadius, labelY, dim, "FREQ");
			CenteredText(draw, corner.x + knobX[1] + kKnobRadius, labelY, dim, "GAIN");
			CenteredText(draw, corner.x + knobX[2] + kKnobRadius, labelY, dim, "Q");

			for (int index = 0; index < static_cast<int>(Eq::kBands); ++index) {
				const ImU32 color = a_settings.bands[index].on ? kColors[index] : IM_COL32(95, 95, 100, 255);
				const bool  current = index == a_selected;
				ImGui::PushID(index);
				ImGui::PushStyleColor(ImGuiCol_Button, current ? color : (color & 0x00FFFFFFu) | 0x45000000u);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (color & 0x00FFFFFFu) | 0x90000000u);
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, color);
				ImGui::PushStyleColor(ImGuiCol_Text, current ? IM_COL32(15, 15, 15, 255) : IM_COL32(230, 230, 230, 255));
				ImGui::SetCursorPos(ImVec2(U(274.0f + static_cast<float>(index % 4) * 24.0f), U(15.0f + static_cast<float>(index / 4) * 24.0f)));
				const bool pick = ImGui::Button(std::to_string(index + 1).c_str(), ImVec2(U(21.0f), U(21.0f)));
				const auto pickTip = std::format("Select band {}.", index + 1);
				ImGui::Tooltip(pickTip.c_str());
				if (pick) {
					a_selected = index;
				}
				ImGui::PopStyleColor(4);
				ImGui::PopID();
			}

			if (!a_settings.enabled) {
				GreyOut(ImGui::GetWindowDrawList(), 0);
			}
			ImGui::EndChild();
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(2);
			return changed;
		}
	}

	void Render()
	{
		auto&      settings = Settings::GetSingleton();
		static int selected = 3;
		ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(46, 46, 54, 235));
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(60, 60, 70, 245));
		ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(70, 70, 82, 255));
		ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(52, 52, 60, 240));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(72, 72, 84, 250));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(90, 90, 104, 255));

		const float left = ImGui::GetCursorPosX();
		const float top = ImGui::GetCursorPosY();
		ImGui::SetCursorPosY(top + U(6.0f));
		bool changed = ImGui::Checkbox("Enabled", &settings.enabled);
		ImGui::Tooltip("Turns all processing on or off. Switching is instant and click-free.");
		if (changed) {
			Equalizer::SetCapture(settings.enabled);
		}
		ImGui::SameLine();
		const bool reset = ImGui::Button("Reset");
		ImGui::Tooltip("Resets the bands, Input, Output, Balance and auto gain.");
		if (reset) {
			settings.bands = Settings::DefaultBands();
			settings.inputGain = 0.0f;
			settings.outputGain = 0.0f;
			settings.balance = 0.0f;
			settings.autoGain = false;
			changed = true;
		}
		ImGui::SameLine();
		const bool save = ImGui::Button("Save Preset");
		ImGui::Tooltip("Saves all settings to AudioEqualizer.ini.");
		if (save) {
			settings.Save();
		}

		static Meter inputMeter;
		static Meter outputMeter;
		float        inputPeak = 0.0f;
		float        outputPeak = 0.0f;
		if (settings.enabled) {
			Equalizer::ReadLevels(inputPeak, outputPeak);
		}
		ImGui::BeginDisabled(!settings.enabled);
		auto*     draw = ImGui::GetWindowDrawList();
		const int firstVertex = draw->VtxBuffer.Size;
		changed |= BalanceGroup(left + U(238.0f), top, settings.balance);
		changed |= LevelGroup("input", "Input", left + U(338.0f), top, settings.inputGain, inputPeak, inputMeter, 0.0f, settings.enabled);
		changed |= LevelGroup("output", "Output", left + U(593.0f), top, settings.outputGain, outputPeak, outputMeter, -1.9f, settings.enabled, &settings.autoGain);

		ImGui::SetCursorPos(ImVec2(left, top + U(42.0f)));
		const ImVec2 graph = ImGui::GetCursorScreenPos();
		changed |= DrawGraph(settings, selected);
		if (!settings.enabled) {
			GreyOut(draw, firstVertex);
		}
		const ImVec2 below = ImGui::GetCursorScreenPos();
		changed |= BandPanel(settings, selected, graph);
		ImGui::EndDisabled();
		ImGui::SetCursorScreenPos(below);

		const bool zoomed = ImGui::Zoom(settings.windowScale, U(80.0f));
		ImGui::Tooltip("Size of this window. 200% suits 4K screens.");
		if (zoomed) {
			settings.Save();
		}

		ImGui::TextDisabled("Drag a point to set frequency and gain. Mouse wheel over the graph sets Q. Double-click a point or knob to reset it.");
		ImGui::TextDisabled("%s", Equalizer::Status());
		ImGui::PopStyleColor(6);
		if (changed) {
			Equalizer::Apply();
		}
	}
