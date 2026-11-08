#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <complex>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Eq
{
	inline constexpr std::size_t kBands = 8;
	inline constexpr std::size_t kMaxSections = 8;
	inline constexpr std::size_t kStages = kBands * kMaxSections;
	inline constexpr std::size_t kMaxChannels = 8;
	inline constexpr double      kPi = 3.14159265358979323846;

	enum class Type : std::int32_t
	{
		kBell,
		kLowShelf,
		kHighShelf,
		kLowPass,
		kHighPass
	};

	struct Band
	{
		bool  on{ true };
		Type  type{ Type::kBell };
		float frequency{ 1000.0f };
		float gain{ 0.0f };
		float q{ 1.0f };
		int   slope{ 12 };

		bool operator==(const Band&) const = default;
	};

	using Bands = std::array<Band, kBands>;

	struct Coefficients
	{
		double b0{ 1.0 };
		double b1{ 0.0 };
		double b2{ 0.0 };
		double a1{ 0.0 };
		double a2{ 0.0 };
	};

	struct Recipe
	{
		std::uint8_t method{ 0 };
		std::uint8_t inverted{ 0 };
		float        ratio{ 1.0f };
	};

	struct Filter
	{
		std::array<Coefficients, kStages>  stages{};
		std::array<std::uint32_t, kBands> sections{};
		double                            inputGain{ 1.0 };
		double                            outputGain{ 1.0 };
		Bands                             bands{};
		std::array<Recipe, kStages>       recipes{};
		double                            rate{ 48000.0 };
	};

	inline constexpr double kButterworthQ = 0.70710678118654752;

	inline double ResponseDb(const Coefficients& a_c, double a_frequency, double a_sampleRate);

	inline double AnalogPower(Type a_type, double a_ratio, double a_q, double a_a)
	{
		const double r2 = a_ratio * a_ratio;
		const double bend = (1.0 - r2) * (1.0 - r2);
		switch (a_type) {
		case Type::kLowShelf:
			return a_a * a_a * ((a_a - r2) * (a_a - r2) + a_a * r2 / (a_q * a_q)) / ((1.0 - a_a * r2) * (1.0 - a_a * r2) + a_a * r2 / (a_q * a_q));
		case Type::kHighShelf:
			return a_a * a_a * ((1.0 - a_a * r2) * (1.0 - a_a * r2) + a_a * r2 / (a_q * a_q)) / ((a_a - r2) * (a_a - r2) + a_a * r2 / (a_q * a_q));
		case Type::kLowPass:
			return 1.0 / (bend + r2 / (a_q * a_q));
		case Type::kHighPass:
			return r2 * r2 / (bend + r2 / (a_q * a_q));
		default:
			return (bend + a_a * a_a * r2 / (a_q * a_q)) / (bend + r2 / (a_a * a_a * a_q * a_q));
		}
	}

	inline bool IsShelf(Type a_type) { return a_type == Type::kLowShelf || a_type == Type::kHighShelf; }
	inline bool IsCut(Type a_type) { return a_type == Type::kLowPass || a_type == Type::kHighPass; }

	inline void Poles(Type a_type, double a_frequency, double a_q, double a_a, double a_sampleRate, double& a_a1, double& a_a2)
	{
		double poleQ = a_q;
		double poleFrequency = a_frequency;
		if (a_type == Type::kBell) {
			poleQ = a_a * a_q;
		} else if (a_type == Type::kLowShelf) {
			poleFrequency = a_frequency / std::sqrt(a_a);
		} else if (a_type == Type::kHighShelf) {
			poleFrequency = a_frequency * std::sqrt(a_a);
		}
		const double w = (std::min)(2.0 * kPi * poleFrequency / a_sampleRate, 0.95 * kPi);
		const double zeta = 1.0 / (2.0 * poleQ);
		const double decay = std::exp(-zeta * w);
		a_a1 = zeta < 1.0 ? -2.0 * decay * std::cos(w * std::sqrt(1.0 - zeta * zeta)) : -2.0 * decay * std::cosh(w * std::sqrt(zeta * zeta - 1.0));
		a_a2 = decay * decay;
	}

	inline Coefficients Numerator(double a_B0, double a_B1, double a_B2, double a_a1, double a_a2, bool& a_ok)
	{
		const double plus = std::sqrt((std::max)(a_B0, 0.0));
		const double minus = std::sqrt((std::max)(a_B1, 0.0));
		const double sum = 0.5 * (plus + minus);
		const double root = std::sqrt((std::max)(sum * sum + a_B2 * 0.25, 0.0));
		const Coefficients c{ 0.5 * (sum + root), 0.5 * (plus - minus), 0.5 * (sum - root), a_a1, a_a2 };
		a_ok = std::isfinite(c.b0) && std::isfinite(c.b1) && std::isfinite(c.b2) && std::isfinite(c.a1) && std::isfinite(c.a2) && c.b0 > 0.0;
		return c;
	}

	inline Coefficients Matched(Type a_type, double a_frequency, double a_q, double a_a, double a_sampleRate, double a_match, bool& a_ok)
	{
		double a1 = 0.0, a2 = 0.0;
		Poles(a_type, a_frequency, a_q, a_a, a_sampleRate, a1, a2);
		const double A0 = (1.0 + a1 + a2) * (1.0 + a1 + a2);
		const double A1 = (1.0 - a1 + a2) * (1.0 - a1 + a2);
		const double A2 = -16.0 * a2;
		const double wm = 2.0 * kPi * a_match / a_sampleRate;
		const double p1 = std::sin(wm * 0.5) * std::sin(wm * 0.5);
		const double p0 = 1.0 - p1;
		const double target = AnalogPower(a_type, a_match / a_frequency, a_q, a_a) * (A0 * p0 + A1 * p1 + A2 * p0 * p1);
		if (a_type == Type::kHighPass) {
			const double gain = std::sqrt((std::max)(target, 0.0)) / (4.0 * p1);
			a_ok = std::isfinite(gain) && gain > 0.0 && std::isfinite(a1) && std::isfinite(a2);
			return { gain, -2.0 * gain, gain, a1, a2 };
		}
		const double B0 = AnalogPower(a_type, 0.0, a_q, a_a) * A0;
		const double B1 = AnalogPower(a_type, a_sampleRate * 0.5 / a_frequency, a_q, a_a) * A1;
		const double B2 = (target - B0 * p0 - B1 * p1) / (p0 * p1);
		return Numerator(B0, B1, B2, a1, a2, a_ok);
	}

	inline double GridFrequency(int a_index, double a_frequency, double a_sampleRate)
	{
		constexpr int kWide = 48;
		constexpr int kNear = 96;
		const double  top = (std::min)(a_sampleRate * 0.49, 20000.0);
		const double  low = a_frequency * 0.5;
		const double  high = (std::min)(a_frequency * 2.0, top);
		return a_index < kWide ? 20.0 * std::pow(top / 20.0, static_cast<double>(a_index) / (kWide - 1)) : low * std::pow(high / low, static_cast<double>(a_index - kWide) / (kNear - 1));
	}

	inline constexpr int kGridPoints = 144;

	inline Coefficients Fitted(Type a_type, double a_frequency, double a_q, double a_a, double a_sampleRate, bool& a_ok)
	{
		double a1 = 0.0, a2 = 0.0;
		Poles(a_type, a_frequency, a_q, a_a, a_sampleRate, a1, a2);
		const double A0 = (1.0 + a1 + a2) * (1.0 + a1 + a2);
		const double A1 = (1.0 - a1 + a2) * (1.0 - a1 + a2);
		const double A2 = -16.0 * a2;
		double       m[3][3]{};
		double       v[3]{};
		for (int i = 0; i < kGridPoints; ++i) {
			const double frequency = GridFrequency(i, a_frequency, a_sampleRate);
			const double analog = AnalogPower(a_type, frequency / a_frequency, a_q, a_a);
			if (analog < 1e-6) {
				continue;
			}
			const double half = kPi * frequency / a_sampleRate;
			const double p1 = std::sin(half) * std::sin(half);
			const double p0 = 1.0 - p1;
			const double want = analog * (A0 * p0 + A1 * p1 + A2 * p0 * p1);
			const double x[3]{ p0 / want, p1 / want, p0 * p1 / want };
			for (int r = 0; r < 3; ++r) {
				for (int c = 0; c < 3; ++c) {
					m[r][c] += x[r] * x[c];
				}
				v[r] += x[r];
			}
		}
		const double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
		if (!(std::fabs(det) > 1e-300)) {
			a_ok = false;
			return {};
		}
		double B[3];
		for (int k = 0; k < 3; ++k) {
			double t[3][3];
			for (int r = 0; r < 3; ++r) {
				for (int c = 0; c < 3; ++c) {
					t[r][c] = c == k ? v[r] : m[r][c];
				}
			}
			B[k] = (t[0][0] * (t[1][1] * t[2][2] - t[1][2] * t[2][1]) - t[0][1] * (t[1][0] * t[2][2] - t[1][2] * t[2][0]) + t[0][2] * (t[1][0] * t[2][1] - t[1][1] * t[2][0])) / det;
		}
		return Numerator(B[0], B[1], B[2], a1, a2, a_ok);
	}

	inline Coefficients Bilinear(Type a_type, double a_frequency, double a_q, double a_a, double a_sampleRate)
	{
		const double w = 2.0 * kPi * a_frequency / a_sampleRate;
		const double cosine = std::cos(w);
		const double alpha = std::sin(w) / (2.0 * a_q);
		const double a = a_a;
		const double root = 2.0 * std::sqrt(a) * alpha;
		double       b0{}, b1{}, b2{}, a0{}, a1{}, a2{};
		switch (a_type) {
		case Type::kLowShelf:
			b0 = a * ((a + 1) - (a - 1) * cosine + root);
			b1 = 2 * a * ((a - 1) - (a + 1) * cosine);
			b2 = a * ((a + 1) - (a - 1) * cosine - root);
			a0 = (a + 1) + (a - 1) * cosine + root;
			a1 = -2 * ((a - 1) + (a + 1) * cosine);
			a2 = (a + 1) + (a - 1) * cosine - root;
			break;
		case Type::kHighShelf:
			b0 = a * ((a + 1) + (a - 1) * cosine + root);
			b1 = -2 * a * ((a - 1) + (a + 1) * cosine);
			b2 = a * ((a + 1) + (a - 1) * cosine - root);
			a0 = (a + 1) - (a - 1) * cosine + root;
			a1 = 2 * ((a - 1) - (a + 1) * cosine);
			a2 = (a + 1) - (a - 1) * cosine - root;
			break;
		case Type::kLowPass:
			b0 = (1 - cosine) / 2;
			b1 = 1 - cosine;
			b2 = b0;
			a0 = 1 + alpha;
			a1 = -2 * cosine;
			a2 = 1 - alpha;
			break;
		case Type::kHighPass:
			b0 = (1 + cosine) / 2;
			b1 = -(1 + cosine);
			b2 = b0;
			a0 = 1 + alpha;
			a1 = -2 * cosine;
			a2 = 1 - alpha;
			break;
		default:
			b0 = 1 + alpha * a;
			b1 = -2 * cosine;
			b2 = 1 - alpha * a;
			a0 = 1 + alpha / a;
			a1 = -2 * cosine;
			a2 = 1 - alpha / a;
			break;
		}
		return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
	}

	inline Coefficients Build(Type a_type, double a_frequency, double a_q, double a_gainDb, double a_sampleRate, const Recipe& a_recipe, bool& a_ok)
	{
		const double frequency = (std::min)(a_frequency, a_sampleRate * 0.45);
		const double q = (std::max)(a_q, 0.05);
		const double a = std::pow(10.0, a_gainDb / 40.0);
		const bool   invert = a_recipe.inverted && !IsCut(a_type) && a_gainDb != 0.0;
		const double used = invert ? 1.0 / a : a;
		a_ok = true;
		Coefficients c;
		if (a_recipe.method == 0) {
			c = Bilinear(a_type, frequency, q, used, a_sampleRate);
		} else if (a_recipe.method == 1) {
			c = Matched(a_type, frequency, q, used, a_sampleRate, frequency * a_recipe.ratio, a_ok);
		} else {
			c = Fitted(a_type, frequency, q, used, a_sampleRate, a_ok);
		}
		if (a_ok && invert) {
			c = { 1.0 / c.b0, c.a1 / c.b0, c.a2 / c.b0, c.b1 / c.b0, c.b2 / c.b0 };
		}
		a_ok = a_ok && std::isfinite(c.b0) && std::isfinite(c.b1) && std::isfinite(c.b2) && std::isfinite(c.a1) && std::isfinite(c.a2) && std::fabs(c.a2) < 0.99999 && std::fabs(c.a1) < 1.0 + c.a2 - 1e-9;
		return c;
	}

	inline double ShapeError(const Coefficients& a_c, Type a_type, double a_frequency, double a_q, double a_a, double a_sampleRate)
	{
		double worst = 0.0;
		for (int i = 0; i < kGridPoints; ++i) {
			const double frequency = GridFrequency(i, a_frequency, a_sampleRate);
			const double target = 10.0 * std::log10((std::max)(AnalogPower(a_type, frequency / a_frequency, a_q, a_a), 1e-12));
			if (target < -60.0) {
				continue;
			}
			worst = (std::max)(worst, std::fabs(ResponseDb(a_c, frequency, a_sampleRate) - target));
		}
		return worst;
	}

	inline Coefficients Design(const Band& a_band, double a_q, double a_sampleRate, Recipe* a_chosen = nullptr)
	{
		const bool passes = IsCut(a_band.type);
		if (!a_band.on || (!passes && a_band.gain == 0.0f)) {
			return {};
		}
		const double frequency = (std::min)(static_cast<double>(a_band.frequency), a_sampleRate * 0.45);
		const double q = (std::max)(a_q, 0.05);
		const double a = std::pow(10.0, a_band.gain / 40.0);
		Recipe       candidates[16];
		int          count = 0;
		candidates[count++] = { 0, 0, 1.0f };
		for (const std::uint8_t inverted : { std::uint8_t{ 0 }, std::uint8_t{ 1 } }) {
			if (inverted && passes) {
				break;
			}
			for (const float ratio : { 1.0f, 0.8f, 1.25f, 0.65f, 1.5f }) {
				if ((passes && ratio != 1.0f) || frequency * ratio >= a_sampleRate * 0.49) {
					continue;
				}
				candidates[count++] = { 1, inverted, ratio };
			}
			if (!passes) {
				candidates[count++] = { 2, inverted, 1.0f };
			}
		}
		Coefficients best{};
		Recipe       bestRecipe{};
		double       bestError = 1e300;
		for (int i = 0; i < count; ++i) {
			bool               ok = false;
			const Coefficients c = Build(a_band.type, frequency, q, a_band.gain, a_sampleRate, candidates[i], ok);
			if (!ok) {
				continue;
			}
			const double error = ShapeError(c, a_band.type, frequency, q, a, a_sampleRate);
			if (error < bestError) {
				best = c, bestRecipe = candidates[i], bestError = error;
			}
		}
		if (a_chosen) {
			*a_chosen = bestRecipe;
		}
		return best;
	}

	inline double ResponseDb(const Coefficients& a_c, double a_frequency, double a_sampleRate)
	{
		const double w = 2.0 * kPi * a_frequency / a_sampleRate;
		const double c1 = std::cos(w), s1 = std::sin(w), c2 = std::cos(2 * w), s2 = std::sin(2 * w);
		const double nr = a_c.b0 + a_c.b1 * c1 + a_c.b2 * c2;
		const double ni = a_c.b1 * s1 + a_c.b2 * s2;
		const double dr = 1.0 + a_c.a1 * c1 + a_c.a2 * c2;
		const double di = a_c.a1 * s1 + a_c.a2 * s2;
		const double power = (nr * nr + ni * ni) / (std::max)(dr * dr + di * di, 1e-30);
		return 10.0 * std::log10((std::max)(power, 1e-12));
	}

	inline double ResponseDb(const Filter& a_filter, double a_frequency, double a_sampleRate)
	{
		double total = 0.0;
		for (std::size_t band = 0; band < kBands; ++band) {
			for (std::size_t section = 0; section < a_filter.sections[band]; ++section) {
				total += ResponseDb(a_filter.stages[band * kMaxSections + section], a_frequency, a_sampleRate);
			}
		}
		return total;
	}

	inline std::size_t SectionCount(const Band& a_band)
	{
		return IsCut(a_band.type) ? static_cast<std::size_t>(std::clamp(a_band.slope / 12, 1, static_cast<int>(kMaxSections))) : std::size_t{ 1 };
	}

	inline double SectionQ(const Band& a_band, std::size_t a_sections, std::size_t a_section)
	{
		if (a_sections > 1) {
			double q = 1.0 / (2.0 * std::cos(kPi * static_cast<double>(2 * a_sections - 1 - 2 * a_section) / (4.0 * static_cast<double>(a_sections))));
			return a_section + 1 == a_sections ? q * a_band.q : q;
		}
		return a_band.q * kButterworthQ;
	}

	inline Filter Design(const Bands& a_bands, double a_sampleRate, double a_inputDb = 0.0, double a_outputDb = 0.0)
	{
		Filter filter;
		filter.inputGain = std::pow(10.0, a_inputDb / 20.0);
		filter.outputGain = std::pow(10.0, a_outputDb / 20.0);
		filter.bands = a_bands;
		filter.rate = a_sampleRate;
		for (std::size_t index = 0; index < kBands; ++index) {
			const auto& band = a_bands[index];
			if (!band.on || (!IsCut(band.type) && band.gain == 0.0f)) {
				continue;
			}
			const auto sections = SectionCount(band);
			filter.sections[index] = static_cast<std::uint32_t>(sections);
			for (std::size_t section = 0; section < sections; ++section) {
				filter.stages[index * kMaxSections + section] = Design(band, SectionQ(band, sections, section), a_sampleRate, &filter.recipes[index * kMaxSections + section]);
			}
		}
		return filter;
	}

	inline float PeakOf(const float* a_samples, std::uint32_t a_frames, std::uint32_t a_channels)
	{
		float peak = 0.0f;
		const std::size_t count = static_cast<std::size_t>(a_frames) * a_channels;
		for (std::size_t i = 0; i < count; ++i) {
			const float size = std::fabs(a_samples[i]);
			if (size > peak) {
				peak = size;
			}
		}
		return peak;
	}

	inline float SoftLimit(float a_sample)
	{
		constexpr float kKnee = 0.95f;
		const float     size = std::fabs(a_sample);
		if (!(size > kKnee)) {
			return a_sample;
		}
		const float limited = kKnee + (1.0f - kKnee) * std::tanh((size - kKnee) / (1.0f - kKnee));
		return a_sample < 0.0f ? -limited : limited;
	}

	inline void Limit(float* a_samples, std::uint32_t a_frames, std::uint32_t a_channels)
	{
		const std::size_t count = static_cast<std::size_t>(a_frames) * a_channels;
		for (std::size_t i = 0; i < count; ++i) {
			a_samples[i] = SoftLimit(a_samples[i]);
		}
	}

	class Processor
	{
	public:
		void Reset()
		{
			state = {};
			active = target;
			motion = {};
			fading = {};
			fadeLeft = 0;
			gainMoving = false;
			Index();
		}

		void Snap(const Filter& a_filter)
		{
			target = a_filter;
			active = a_filter;
			rate = a_filter.rate;
			state = {};
			motion = {};
			fading = {};
			fadeLeft = 0;
			gainMoving = false;
			Index();
		}

		void Process(const Filter& a_filter, float* a_samples, std::uint32_t a_frames, std::uint32_t a_channels)
		{
			if (!Same(a_filter, target)) {
				Retarget(a_filter);
			}
			const std::uint32_t channels = (std::min)(a_channels, static_cast<std::uint32_t>(kMaxChannels));
			std::uint32_t       done = 0;
			while (done < a_frames && !Bypassed()) {
				const bool          moving = Moving();
				const std::uint32_t chunk = (std::min)(a_frames - done, moving ? kStep : kChunk);
				startGain[0] = active.inputGain;
				startGain[1] = active.outputGain;
				gliding = moving;
				if (moving) {
					startStages = active.stages;
					Advance(chunk);
				}
				for (std::uint32_t channel = 0; channel < channels; ++channel) {
					Run(a_samples + done * a_channels + channel, chunk, a_channels, channel);
				}
				done += chunk;
			}
		}

		[[nodiscard]] bool Working() const { return !Bypassed(); }

	private:
		static constexpr std::uint32_t kChunk = 512;
		static constexpr std::uint32_t kStep = 32;
		static constexpr double        kGlideSeconds = 0.035;
		static constexpr double        kFadeSeconds = 0.0076;
		static constexpr double        kFadeLength = 0.093;
		static constexpr double        kSettleLength = 0.02;

		struct Motion
		{
			bool   on{ false };
			double progress{ 0.0 };
			Band   from{};
			Band   to{};
		};

		static bool Same(const Filter& a_left, const Filter& a_right)
		{
			return a_left.inputGain == a_right.inputGain && a_left.outputGain == a_right.outputGain && a_left.rate == a_right.rate && a_left.bands == a_right.bands &&
			       a_left.sections == a_right.sections && std::memcmp(a_left.stages.data(), a_right.stages.data(), sizeof(a_left.stages)) == 0;
		}

		static Band Effective(const Band& a_band)
		{
			Band band = a_band;
			if (!band.on && !IsCut(band.type)) {
				band.gain = 0.0f;
			}
			band.on = true;
			return band;
		}

		static bool Glides(const Band& a_before, const Band& a_after)
		{
			if (a_before.type != a_after.type || SectionCount(a_before) != SectionCount(a_after)) {
				return false;
			}
			return !IsCut(a_after.type) || (a_before.on && a_after.on);
		}

		[[nodiscard]] Band Current(std::size_t a_band) const
		{
			const auto&  m = motion[a_band];
			const double t = (std::min)(m.progress, 1.0);
			Band         band = m.to;
			band.frequency = static_cast<float>(std::exp(std::log(m.from.frequency) + (std::log(m.to.frequency) - std::log(m.from.frequency)) * t));
			band.gain = static_cast<float>(m.from.gain + (m.to.gain - m.from.gain) * t);
			band.q = static_cast<float>(std::exp(std::log(m.from.q) + (std::log(m.to.q) - std::log(m.from.q)) * t));
			return band;
		}

		[[nodiscard]] bool Moving() const
		{
			if (fadeLeft > 0 || gainMoving) {
				return true;
			}
			for (const auto& m : motion) {
				if (m.on) {
					return true;
				}
			}
			return false;
		}

		[[nodiscard]] bool Bypassed() const
		{
			return used == 0 && !Moving() && active.inputGain == 1.0 && active.outputGain == 1.0;
		}

		void Grow(std::size_t a_band, std::size_t a_sections)
		{
			for (std::size_t section = active.sections[a_band]; section < a_sections; ++section) {
				active.stages[a_band * kMaxSections + section] = {};
				for (auto& channel : state) {
					channel[a_band * kMaxSections + section] = {};
				}
			}
			active.sections[a_band] = (std::max)(active.sections[a_band], static_cast<std::uint32_t>(a_sections));
		}

		void Retarget(const Filter& a_filter)
		{
			rate = a_filter.rate;
			for (std::size_t band = 0; band < kBands; ++band) {
				const Band& before = target.bands[band];
				const Band& after = a_filter.bands[band];
				const bool  unchanged = before == after && target.sections[band] == a_filter.sections[band] &&
				                       std::memcmp(&target.stages[band * kMaxSections], &a_filter.stages[band * kMaxSections], sizeof(Coefficients) * kMaxSections) == 0;
				if (unchanged) {
					continue;
				}
				if (Glides(before, after)) {
					const Band start = motion[band].on ? Current(band) : Effective(before);
					motion[band] = { true, 0.0, start, Effective(after) };
					Grow(band, SectionCount(after));
					for (std::size_t section = 0; section < kMaxSections; ++section) {
						fading[band * kMaxSections + section] = false;
					}
				} else {
					motion[band].on = false;
					Grow(band, a_filter.sections[band]);
					for (std::size_t section = 0; section < active.sections[band]; ++section) {
						fading[band * kMaxSections + section] = true;
					}
					fadeLeft = (std::max)(fadeLeft, static_cast<std::int64_t>(kFadeLength * rate));
				}
			}
			if (a_filter.inputGain != target.inputGain || a_filter.outputGain != target.outputGain) {
				gainFrom[0] = 20.0 * std::log10(active.inputGain);
				gainFrom[1] = 20.0 * std::log10(active.outputGain);
				gainTo[0] = 20.0 * std::log10(a_filter.inputGain);
				gainTo[1] = 20.0 * std::log10(a_filter.outputGain);
				gainProgress = 0.0;
				gainMoving = true;
			}
			target = a_filter;
			Index();
		}

		void Advance(std::uint32_t a_frames)
		{
			const double step = static_cast<double>(a_frames) / (kGlideSeconds * rate);
			for (std::size_t band = 0; band < kBands; ++band) {
				auto& m = motion[band];
				if (!m.on) {
					continue;
				}
				m.progress += step;
				if (m.progress >= 1.0) {
					m.on = false;
					for (std::size_t section = 0; section < active.sections[band]; ++section) {
						fading[band * kMaxSections + section] = true;
					}
					fadeLeft = (std::max)(fadeLeft, static_cast<std::int64_t>(kSettleLength * rate));
				}
				const Band        now = Current(band);
				const std::size_t sections = SectionCount(now);
				for (std::size_t section = 0; section < sections; ++section) {
					const Recipe recipe{ 1, static_cast<std::uint8_t>(now.gain < 0.0f && !IsCut(now.type) ? 1 : 0), 1.0f };
					bool         ok = false;
					Coefficients c = Build(now.type, now.frequency, SectionQ(now, sections, section), now.gain, rate, recipe, ok);
					if (!ok) {
						c = Build(now.type, now.frequency, SectionQ(now, sections, section), now.gain, rate, Recipe{}, ok);
					}
					active.stages[band * kMaxSections + section] = c;
				}
			}
			if (fadeLeft > 0) {
				const double blend = 1.0 - std::exp(-static_cast<double>(a_frames) / (kFadeSeconds * rate));
				fadeLeft -= a_frames;
				for (std::size_t stage = 0; stage < kStages; ++stage) {
					if (!fading[stage]) {
						continue;
					}
					auto&       from = active.stages[stage];
					const auto& to = target.stages[stage];
					if (fadeLeft <= 0) {
						from = to;
						continue;
					}
					from.b0 += (to.b0 - from.b0) * blend;
					from.b1 += (to.b1 - from.b1) * blend;
					from.b2 += (to.b2 - from.b2) * blend;
					from.a1 += (to.a1 - from.a1) * blend;
					from.a2 += (to.a2 - from.a2) * blend;
				}
				if (fadeLeft <= 0) {
					fadeLeft = 0;
					fading = {};
					for (std::size_t band = 0; band < kBands; ++band) {
						if (!motion[band].on && active.sections[band] > target.sections[band]) {
							for (std::size_t section = target.sections[band]; section < active.sections[band]; ++section) {
								for (auto& channel : state) {
									channel[band * kMaxSections + section] = {};
								}
							}
							active.sections[band] = target.sections[band];
						}
					}
					Index();
				}
			}
			if (gainMoving) {
				gainProgress += step;
				if (gainProgress >= 1.0) {
					active.inputGain = target.inputGain;
					active.outputGain = target.outputGain;
					gainMoving = false;
				} else {
					active.inputGain = std::pow(10.0, (gainFrom[0] + (gainTo[0] - gainFrom[0]) * gainProgress) / 20.0);
					active.outputGain = std::pow(10.0, (gainFrom[1] + (gainTo[1] - gainFrom[1]) * gainProgress) / 20.0);
				}
			}
		}

		void Index()
		{
			used = 0;
			for (std::size_t band = 0; band < kBands; ++band) {
				for (std::size_t section = 0; section < active.sections[band]; ++section) {
					order[used++] = static_cast<std::uint16_t>(band * kMaxSections + section);
				}
			}
		}

		void Run(float* a_samples, std::uint32_t a_frames, std::uint32_t a_stride, std::uint32_t a_channel)
		{
			const bool   ramp = startGain[0] != active.inputGain || startGain[1] != active.outputGain;
			const double inStep = (active.inputGain - startGain[0]) / a_frames;
			const double outStep = (active.outputGain - startGain[1]) / a_frames;
			for (std::uint32_t i = 0; i < a_frames; ++i) {
				scratch[i] = a_samples[static_cast<std::size_t>(i) * a_stride] * (ramp ? startGain[0] + inStep * (i + 1) : active.inputGain);
			}
			auto& delays = state[a_channel];
			for (std::size_t n = 0; n < used; ++n) {
				const auto   stage = order[n];
				const auto   c = active.stages[stage];
				double       z1 = delays[stage][0];
				double       z2 = delays[stage][1];
				if (gliding && std::memcmp(&startStages[stage], &c, sizeof(Coefficients)) != 0) {
					const auto&  s0 = startStages[stage];
					const double inv = 1.0 / a_frames;
					const double d0 = (c.b0 - s0.b0) * inv, d1 = (c.b1 - s0.b1) * inv, d2 = (c.b2 - s0.b2) * inv, e1 = (c.a1 - s0.a1) * inv, e2 = (c.a2 - s0.a2) * inv;
					double       b0 = s0.b0, b1 = s0.b1, b2 = s0.b2, a1 = s0.a1, a2 = s0.a2;
					for (std::uint32_t i = 0; i < a_frames; ++i) {
						b0 += d0, b1 += d1, b2 += d2, a1 += e1, a2 += e2;
						const double in = scratch[i];
						const double out = b0 * in + z1;
						z1 = b1 * in - a1 * out + z2;
						z2 = b2 * in - a2 * out;
						scratch[i] = out;
					}
				} else {
					for (std::uint32_t i = 0; i < a_frames; ++i) {
						const double in = scratch[i];
						const double out = c.b0 * in + z1;
						z1 = c.b1 * in - c.a1 * out + z2;
						z2 = c.b2 * in - c.a2 * out;
						scratch[i] = out;
					}
				}
				delays[stage][0] = z1;
				delays[stage][1] = z2;
			}
			if (used > 0) {
				const auto& last = delays[order[used - 1]];
				if (!(std::fabs(last[0]) + std::fabs(last[1]) < 1e15)) {
					delays = {};
					for (std::uint32_t i = 0; i < a_frames; ++i) {
						a_samples[static_cast<std::size_t>(i) * a_stride] = 0.0f;
					}
					return;
				}
			}
			for (std::uint32_t i = 0; i < a_frames; ++i) {
				a_samples[static_cast<std::size_t>(i) * a_stride] = static_cast<float>(scratch[i] * (ramp ? startGain[1] + outStep * (i + 1) : active.outputGain));
			}
		}

		Filter                                                               active{};
		Filter                                                               target{};
		double                                                               rate{ 48000.0 };
		std::array<Motion, kBands>                                           motion{};
		std::array<bool, kStages>                                            fading{};
		std::int64_t                                                         fadeLeft{ 0 };
		bool                                                                 gainMoving{ false };
		double                                                               gainProgress{ 0.0 };
		double                                                               gainFrom[2]{};
		double                                                               gainTo[2]{};
		double                                                               startGain[2]{ 1.0, 1.0 };
		bool                                                                 gliding{ false };
		std::array<Coefficients, kStages>                                    startStages{};
		std::size_t                                                          used{ 0 };
		std::array<std::uint16_t, kStages>                                   order{};
		std::array<double, kChunk>                                           scratch{};
		std::array<std::array<std::array<double, 2>, kStages>, kMaxChannels> state{};
	};

	inline constexpr std::size_t kSpectrumSize = 8192;
	inline constexpr float       kGraphLowHz = 10.0f;
	inline constexpr float       kGraphHighHz = 30000.0f;
	inline constexpr std::size_t kSpectrumPoints = 256;

	inline void Transform(std::vector<std::complex<float>>& a_data)
	{
		const std::size_t size = a_data.size();
		for (std::size_t i = 1, j = 0; i < size; ++i) {
			std::size_t bit = size >> 1;
			for (; j & bit; bit >>= 1) {
				j ^= bit;
			}
			j ^= bit;
			if (i < j) {
				std::swap(a_data[i], a_data[j]);
			}
		}
		for (std::size_t length = 2; length <= size; length <<= 1) {
			const double angle = -2.0 * kPi / static_cast<double>(length);
			const std::complex<float> step(static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle)));
			for (std::size_t i = 0; i < size; i += length) {
				std::complex<float> turn(1.0f, 0.0f);
				for (std::size_t j = 0; j < length / 2; ++j) {
					const auto even = a_data[i + j];
					const auto odd = a_data[i + j + length / 2] * turn;
					a_data[i + j] = even + odd;
					a_data[i + j + length / 2] = even - odd;
					turn *= step;
				}
			}
		}
	}

	inline void Analyze(const float* a_samples, double a_sampleRate, std::array<float, kSpectrumPoints>& a_decibels)
	{
		static std::vector<std::complex<float>> data(kSpectrumSize);
		for (std::size_t i = 0; i < kSpectrumSize; ++i) {
			const double window = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / (kSpectrumSize - 1));
			data[i] = static_cast<float>(window) * a_samples[i];
		}
		Transform(data);
		const double hz = a_sampleRate / static_cast<double>(kSpectrumSize);
		for (std::size_t point = 0; point < kSpectrumPoints; ++point) {
			const double ratio = static_cast<double>(kGraphHighHz) / kGraphLowHz;
			const double low = kGraphLowHz * std::pow(ratio, static_cast<double>(point) / kSpectrumPoints);
			const double high = kGraphLowHz * std::pow(ratio, static_cast<double>(point + 1) / kSpectrumPoints);
			if (low >= a_sampleRate * 0.5) {
				a_decibels[point] = -120.0f;
				continue;
			}
			const double position = low / hz;
			const auto   first = (std::min)(static_cast<std::size_t>(position), kSpectrumSize / 2 - 2);
			const auto   last = (std::min)(static_cast<std::size_t>(high / hz), kSpectrumSize / 2 - 1);
			float        peak = 0.0f;
			if (last <= first + 1) {
				const auto mix = static_cast<float>(position - static_cast<double>(first));
				peak = std::norm(data[first]) * (1.0f - mix) + std::norm(data[first + 1]) * mix;
			} else {
				for (std::size_t bin = first; bin <= last; ++bin) {
					peak = (std::max)(peak, std::norm(data[bin]));
				}
			}
			a_decibels[point] = 10.0f * std::log10((std::max)(peak * 16.0f / (kSpectrumSize * kSpectrumSize), 1e-12f));
		}
	}

	inline constexpr std::size_t kBalanceBands = 19;

	inline double BalanceFrequency(std::size_t a_band)
	{
		return 31.25 * std::pow(2.0, static_cast<double>(a_band) * 0.5);
	}

	inline double BiquadDb(const Coefficients& a_c, double a_w)
	{
		const std::complex<double> z = std::polar(1.0, -a_w);
		const auto numerator = a_c.b0 + a_c.b1 * z + a_c.b2 * z * z;
		const auto denominator = 1.0 + a_c.a1 * z + a_c.a2 * z * z;
		return 20.0 * std::log10((std::max)(std::abs(numerator / denominator), 1e-12));
	}

	inline Coefficients BalanceBell(std::size_t a_band, double a_gainDb, double a_rate)
	{
		const double w = 2.0 * kPi * BalanceFrequency(a_band) / a_rate;
		const double alpha = std::sin(w) / (2.0 * 2.0);
		const double a = std::pow(10.0, a_gainDb / 40.0);
		const double a0 = 1.0 + alpha / a;
		return { (1.0 + alpha * a) / a0, -2.0 * std::cos(w) / a0, (1.0 - alpha * a) / a0, -2.0 * std::cos(w) / a0, (1.0 - alpha / a) / a0 };
	}

	class Balancer
	{
	public:
		void Prepare(double a_rate)
		{
			used = 0;
			for (std::size_t band = 0; band < kBalanceBands; ++band) {
				const double frequency = BalanceFrequency(band);
				if (frequency > a_rate * 0.45) {
					break;
				}
				const double w = 2.0 * kPi * frequency / a_rate;
				const double sine = std::sin(w);
				angle[band] = w;
				cosine[band] = std::cos(w);
				const double alpha = sine / (2.0 * kDetectorQ);
				const double a0 = 1.0 + alpha;
				detector[band] = { alpha / a0, 0.0, -alpha / a0, -2.0 * cosine[band] / a0, (1.0 - alpha) / a0 };
				bellAlpha[band] = sine / (2.0 * kBellQ);
				const double seconds = std::clamp(40.0 / (frequency * 0.35), 0.3, 3.0);
				powerStep[band] = 1.0 - std::exp(-1.0 / (seconds * a_rate));
				used = band + 1;
			}
			constexpr int kSteps = 2000;
			const double  lowest = std::log(10.0);
			const double  step = (std::log(a_rate * 0.499) - lowest) / kSteps;
			for (std::size_t band = 0; band < used; ++band) {
				double sum = 0.0;
				for (int i = 0; i < kSteps; ++i) {
					const double frequency = std::exp(lowest + (i + 0.5) * step);
					sum += std::pow(10.0, BiquadDb(detector[band], 2.0 * kPi * frequency / a_rate) / 10.0) * step;
				}
				calibration[band] = 10.0 * std::log10(sum);
			}
			quickStep = 1.0 - std::exp(-1.0 / (0.05 * a_rate));
			for (std::size_t band = 0; band < used; ++band) {
				const auto bell = Bell(band, 1.0);
				for (std::size_t at = 0; at < used; ++at) {
					spread[at][band] = BiquadDb(bell, angle[at]);
				}
			}
			attackStep = 1.0 - std::exp(-static_cast<double>(kChunk) / (kAttackSeconds * a_rate));
			releaseStep = 1.0 - std::exp(-static_cast<double>(kChunk) / (kReleaseSeconds * a_rate));
			Reset();
		}

		void Reset()
		{
			detectorState = {};
			bellState = {};
			power = {};
			quick = 0.0;
			gains = {};
			designed = {};
			active = {};
			totals = {};
			idle = true;
		}

		[[nodiscard]] std::size_t Bands() const { return used; }
		[[nodiscard]] double      Total(std::size_t a_band) const { return totals[a_band]; }
		[[nodiscard]] double      Gain(std::size_t a_band) const { return active[a_band] ? gains[a_band] : 0.0; }
		[[nodiscard]] bool        Working() const { return anyActive; }
		[[nodiscard]] double      Density(std::size_t a_band) const { return power[a_band] / std::pow(10.0, calibration[a_band] / 10.0); }

		void ClearFilters()
		{
			detectorState = {};
			bellState = {};
		}

		void Process(float* a_samples, std::uint32_t a_frames, std::uint32_t a_channels, const float* a_weights, float a_amount)
		{
			if (used == 0 || (a_amount <= 0.0f && idle)) {
				return;
			}
			const std::uint32_t channels = (std::min)(a_channels, static_cast<std::uint32_t>(kMaxChannels));
			for (std::uint32_t done = 0; done < a_frames;) {
				const std::uint32_t chunk = (std::min)(a_frames - done, kChunk);
				float*              block = a_samples + static_cast<std::size_t>(done) * a_channels;
				Detect(block, chunk, a_channels, channels, a_weights);
				Update(a_amount);
				if (anyActive) {
					for (std::uint32_t channel = 0; channel < channels; ++channel) {
						Apply(block, chunk, a_channels, channel);
					}
				}
				done += chunk;
			}
			double check = 0.0;
			for (std::size_t band = 0; band < used; ++band) {
				check += std::fabs(power[band]) + std::fabs(bellState[0][band][0]);
			}
			if (!(check < 1e15)) {
				Reset();
				std::memset(a_samples, 0, static_cast<std::size_t>(a_frames) * a_channels * sizeof(float));
			}
		}

	private:
		static constexpr std::uint32_t kChunk = 32;
		static constexpr double        kDetectorQ = 2.87;
		static constexpr double        kBellQ = 2.0;
		static constexpr double        kAttackSeconds = 0.25;
		static constexpr double        kReleaseSeconds = 0.6;
		static constexpr double        kFloorDb = -80.0;
		static constexpr double        kSilenceDb = -70.0;
		static constexpr double        kKneeDb = 1.5;
		static constexpr double        kMaxCutDb = 6.0;
		static constexpr double        kMaxBoostDb = 3.0;

		[[nodiscard]] Coefficients Bell(std::size_t a_band, double a_gainDb) const
		{
			const double a = std::pow(10.0, a_gainDb / 40.0);
			const double a0 = 1.0 + bellAlpha[a_band] / a;
			return { (1.0 + bellAlpha[a_band] * a) / a0, -2.0 * cosine[a_band] / a0, (1.0 - bellAlpha[a_band] * a) / a0, -2.0 * cosine[a_band] / a0, (1.0 - bellAlpha[a_band] / a) / a0 };
		}

		void Detect(const float* a_block, std::uint32_t a_frames, std::uint32_t a_stride, std::uint32_t a_channels, const float* a_weights)
		{
			for (std::uint32_t frame = 0; frame < a_frames; ++frame) {
				double mono = 0.0;
				for (std::uint32_t channel = 0; channel < a_channels; ++channel) {
					mono += static_cast<double>(a_block[static_cast<std::size_t>(frame) * a_stride + channel]) * a_weights[channel];
				}
				quick += (mono * mono - quick) * quickStep;
				const bool sounding = quick > kSilencePower;
				for (std::size_t band = 0; band < used; ++band) {
					auto&       z = detectorState[band];
					const auto& c = detector[band];
					const double out = c.b0 * mono + z[0];
					z[0] = c.b1 * mono - c.a1 * out + z[1];
					z[1] = c.b2 * mono - c.a2 * out;
					if (sounding) {
						power[band] += (out * out - power[band]) * powerStep[band];
					}
				}
			}
		}

		void Update(float a_amount)
		{
			std::array<double, kBalanceBands> level{};
			std::array<double, kBalanceBands> deviation{};
			std::array<bool, kBalanceBands>   valid{};
			double                            reference = 0.0;
			int                               count = 0;
			for (std::size_t band = 0; band < used; ++band) {
				valid[band] = 10.0 * std::log10(power[band] + 1e-30) > kFloorDb;
				level[band] = 10.0 * std::log10(power[band] + 1e-30) - calibration[band];
				if (valid[band]) {
					reference += level[band];
					++count;
				}
			}
			const bool ready = count >= 3 && a_amount > 0.0f && quick > kSilencePower;
			if (ready) {
				reference /= count;
			}
			for (std::size_t band = 0; band < used; ++band) {
				deviation[band] = ready && valid[band] ? level[band] - reference : 0.0;
			}

			std::array<double, kBalanceBands> target{};
			for (std::size_t band = 0; band < used; ++band) {
				if (!ready || !valid[band]) {
					continue;
				}
				const double left = deviation[band > 0 ? band - 1 : band];
				const double right = deviation[band + 1 < used ? band + 1 : band];
				const double d = 0.25 * left + 0.5 * deviation[band] + 0.25 * right;
				const double boost = 10.0 * std::log10(power[band] + 1e-30) > kFloorDb + 12.0 ? kMaxBoostDb : 0.0;
				target[band] = std::clamp(-static_cast<double>(a_amount) * d * d * d / (d * d + kKneeDb * kKneeDb), -kMaxCutDb, boost);
			}

			std::array<double, kBalanceBands> wanted = target;
			for (int pass = 0; pass < 4; ++pass) {
				for (std::size_t at = 0; at < used; ++at) {
					double sum = 0.0;
					for (std::size_t band = 0; band < used; ++band) {
						sum += spread[at][band] * wanted[band];
					}
					wanted[at] = std::clamp(wanted[at] + (target[at] - sum) / spread[at][at], -kMaxCutDb * 1.5, kMaxBoostDb * 1.5);
				}
			}

			idle = a_amount <= 0.0f;
			anyActive = false;
			for (std::size_t band = 0; band < used; ++band) {
				gains[band] += (wanted[band] - gains[band]) * (std::fabs(wanted[band]) > std::fabs(gains[band]) ? attackStep : releaseStep);
				if (std::fabs(gains[band]) < 0.01) {
					if (active[band]) {
						active[band] = false;
						for (auto& channel : bellState) {
							channel[band] = {};
						}
					}
					continue;
				}
				idle = false;
				anyActive = true;
				if (!active[band] || std::fabs(gains[band] - designed[band]) > 0.005) {
					bells[band] = Bell(band, gains[band]);
					designed[band] = gains[band];
					active[band] = true;
				}
			}
			for (std::size_t at = 0; at < used; ++at) {
				double sum = 0.0;
				for (std::size_t band = 0; band < used; ++band) {
					sum += active[band] ? spread[at][band] * gains[band] : 0.0;
				}
				totals[at] = sum;
			}
			if (idle) {
				power = {};
				detectorState = {};
			}
		}

		void Apply(float* a_block, std::uint32_t a_frames, std::uint32_t a_stride, std::uint32_t a_channel)
		{
			std::array<double, kChunk> scratch{};
			for (std::uint32_t i = 0; i < a_frames; ++i) {
				scratch[i] = a_block[static_cast<std::size_t>(i) * a_stride + a_channel];
			}
			auto& delays = bellState[a_channel];
			for (std::size_t band = 0; band < used; ++band) {
				if (!active[band]) {
					continue;
				}
				const auto c = bells[band];
				double     z1 = delays[band][0];
				double     z2 = delays[band][1];
				for (std::uint32_t i = 0; i < a_frames; ++i) {
					const double in = scratch[i];
					const double out = c.b0 * in + z1;
					z1 = c.b1 * in - c.a1 * out + z2;
					z2 = c.b2 * in - c.a2 * out;
					scratch[i] = out;
				}
				delays[band][0] = z1;
				delays[band][1] = z2;
			}
			for (std::uint32_t i = 0; i < a_frames; ++i) {
				const auto value = static_cast<float>(scratch[i]);
				a_block[static_cast<std::size_t>(i) * a_stride + a_channel] = value;
			}
		}

		std::size_t                                                                  used{ 0 };
		std::array<double, kBalanceBands>                                            angle{};
		std::array<double, kBalanceBands>                                            cosine{};
		std::array<double, kBalanceBands>                                            bellAlpha{};
		std::array<double, kBalanceBands>                                            powerStep{};
		std::array<Coefficients, kBalanceBands>                                      detector{};
		std::array<Coefficients, kBalanceBands>                                      bells{};
		std::array<std::array<double, kBalanceBands>, kBalanceBands>                 spread{};
		std::array<std::array<double, 2>, kBalanceBands>                             detectorState{};
		std::array<std::array<std::array<double, 2>, kBalanceBands>, kMaxChannels> bellState{};
		std::array<double, kBalanceBands>                                            power{};
		std::array<double, kBalanceBands>                                            calibration{};
		double                                                                       quick{ 0.0 };
		double                                                                       quickStep{ 0.0 };
		static constexpr double                                                      kSilencePower = 1e-7;
		std::array<double, kBalanceBands>                                            gains{};
		std::array<double, kBalanceBands>                                            designed{};
		std::array<double, kBalanceBands>                                            totals{};
		std::array<bool, kBalanceBands>                                              active{};
		double                                                                       attackStep{ 0.0 };
		double                                                                       releaseStep{ 0.0 };
		bool                                                                         idle{ true };
		bool                                                                         anyActive{ false };
	};

	inline constexpr std::size_t kAutoGainPoints = 64;

	using EqPower = std::array<float, kAutoGainPoints>;

	inline double AutoGainFrequency(std::size_t a_point)
	{
		return 20.0 * std::pow(1000.0, (static_cast<double>(a_point) + 0.5) / kAutoGainPoints);
	}

	inline void MeasureEqPower(const Filter& a_filter, double a_rate, EqPower& a_power)
	{
		for (std::size_t point = 0; point < kAutoGainPoints; ++point) {
			const double frequency = (std::min)(AutoGainFrequency(point), a_rate * 0.499);
			a_power[point] = static_cast<float>(std::pow(10.0, ResponseDb(a_filter, frequency, a_rate) / 10.0));
		}
	}

	class AutoGain
	{
	public:
		void Prepare(double a_rate)
		{
			rate = a_rate;
			Coefficients shelf{};
			Coefficients highPass{};
			{
				const double a = std::pow(10.0, 3.999843853973347 / 40.0);
				const double w = 2.0 * kPi * 1681.974450955533 / a_rate;
				const double c = std::cos(w);
				const double alpha = std::sin(w) / (2.0 * 0.7071752369554196);
				const double root = 2.0 * std::sqrt(a) * alpha;
				const double a0 = (a + 1.0) - (a - 1.0) * c + root;
				shelf = { a * ((a + 1.0) + (a - 1.0) * c + root) / a0, -2.0 * a * ((a - 1.0) + (a + 1.0) * c) / a0, a * ((a + 1.0) + (a - 1.0) * c - root) / a0, 2.0 * ((a - 1.0) - (a + 1.0) * c) / a0, ((a + 1.0) - (a - 1.0) * c - root) / a0 };
			}
			{
				const double w = 2.0 * kPi * 38.13547087602444 / a_rate;
				const double c = std::cos(w);
				const double alpha = std::sin(w) / (2.0 * 0.5003270373238773);
				const double a0 = 1.0 + alpha;
				highPass = { (1.0 + c) / 2.0 / a0, -(1.0 + c) / a0, (1.0 + c) / 2.0 / a0, -2.0 * c / a0, (1.0 - alpha) / a0 };
			}
			total = 0.0;
			eqTotal = 0.0;
			for (std::size_t point = 0; point < kAutoGainPoints; ++point) {
				const double frequency = AutoGainFrequency(point);
				const double w = 2.0 * kPi * (std::min)(frequency, a_rate * 0.499) / a_rate;
				weight[point] = frequency < a_rate * 0.499 ? std::pow(10.0, (BiquadDb(shelf, w) + BiquadDb(highPass, w)) / 10.0) : 0.0;
				total += weight[point];
				eqWeight[point] = frequency < a_rate * 0.499 ? 1.0 / (1.0 + std::pow(kEqLowHz / frequency, 4.0)) / (1.0 + std::pow(frequency / kEqHighHz, 4.0)) : 0.0;
				eqTotal += eqWeight[point];
				position[point] = 2.0 * std::log2(frequency / BalanceFrequency(0));
				for (std::size_t band = 0; band < kBalanceBands; ++band) {
					spread[point][band] = BalanceFrequency(band) > a_rate * 0.45 ? 0.0 : BiquadDb(BalanceBell(band, 1.0, a_rate), w);
				}
			}
			Reset();
		}

		void Reset()
		{
			gainDb = 0.0;
			applied = 1.0;
		}

		[[nodiscard]] double GainDb() const { return gainDb; }
		[[nodiscard]] bool   Working() const { return applied != 1.0 || gainDb != 0.0; }

		[[nodiscard]] double Target(const EqPower& a_eq, const Balancer& a_balance) const
		{
			double curve = 0.0;
			for (std::size_t point = 0; point < kAutoGainPoints; ++point) {
				curve += eqWeight[point] * std::sqrt(static_cast<double>(a_eq[point]));
			}
			double            program = 0.0;
			double            corrected = 0.0;
			const std::size_t bands = a_balance.Bands();
			if (bands >= 2 && a_balance.Working()) {
				std::array<double, kBalanceBands> level{};
				for (std::size_t band = 0; band < bands; ++band) {
					const double density = a_balance.Density(band);
					level[band] = density > 0.0 ? 10.0 * std::log10(density) : -300.0;
				}
				for (std::size_t point = 0; point < kAutoGainPoints; ++point) {
					const double place = std::clamp(position[point], 0.0, static_cast<double>(bands - 1));
					const auto   low = (std::min)(static_cast<std::size_t>(place), bands - 2);
					const double mix = place - static_cast<double>(low);
					const double share = weight[point] * std::pow(10.0, (level[low] * (1.0 - mix) + level[low + 1] * mix) / 10.0);
					double       db = 0.0;
					for (std::size_t band = 0; band < bands; ++band) {
						db += spread[point][band] * a_balance.Gain(band);
					}
					program += share;
					corrected += share * std::pow(10.0, db / 10.0);
				}
			}
			const double eq = eqTotal > 0.0 && curve > 0.0 ? -20.0 * std::log10(curve / eqTotal) : 0.0;
			const double balance = program > 0.0 && corrected > 0.0 ? -10.0 * std::log10(corrected / program) : 0.0;
			return std::clamp(eq + balance, -kLimitDb, kLimitDb);
		}

		void Apply(float* a_samples, std::uint32_t a_frames, std::uint32_t a_channels, const EqPower& a_eq, const Balancer& a_balance, bool a_on)
		{
			if (!a_on && !Working()) {
				return;
			}
			const double target = a_on ? Target(a_eq, a_balance) : 0.0;
			gainDb += (target - gainDb) * (1.0 - std::exp(-static_cast<double>(a_frames) / (kSeconds * rate)));
			if (!std::isfinite(gainDb) || (!a_on && std::fabs(gainDb) < 0.01)) {
				gainDb = 0.0;
			}
			const double next = std::pow(10.0, gainDb / 20.0);
			for (std::uint32_t frame = 0; frame < a_frames; ++frame) {
				const double scale = applied + (next - applied) * static_cast<double>(frame + 1) / a_frames;
				for (std::uint32_t channel = 0; channel < a_channels; ++channel) {
					float& sample = a_samples[static_cast<std::size_t>(frame) * a_channels + channel];
					sample = static_cast<float>(sample * scale);
				}
			}
			applied = next;
		}

	private:
		static constexpr double kSeconds = 0.02;
		static constexpr double kLimitDb = 24.0;

		std::array<double, kAutoGainPoints>                                weight{};
		std::array<double, kAutoGainPoints>                                position{};
		std::array<std::array<double, kBalanceBands>, kAutoGainPoints>     spread{};
		double                                                             total{ 0.0 };
		std::array<double, kAutoGainPoints>                                eqWeight{};
		double                                                             eqTotal{ 0.0 };
		static constexpr double                                            kEqLowHz = 42.39;
		static constexpr double                                            kEqHighHz = 15061.0;
		double                                                             gainDb{ 0.0 };
		double                                                             applied{ 1.0 };
		double                                                             rate{ 48000.0 };
	};

	class Bypass
	{
	public:
		void Prepare(double a_rate, std::size_t a_values, bool a_on)
		{
			step = 1.0 / (kFadeSeconds * a_rate);
			dry.assign(a_values, 0.0f);
			wet = a_on ? 1.0 : 0.0;
			fading = false;
		}

		[[nodiscard]] bool Active(bool a_on) const { return a_on || wet > 0.0; }
		[[nodiscard]] bool Resuming(bool a_on) const { return a_on && wet == 0.0; }

		void Keep(const float* a_samples, std::uint32_t a_frames, std::uint32_t a_channels, bool a_on)
		{
			const std::size_t count = static_cast<std::size_t>(a_frames) * a_channels;
			fading = (!a_on || wet < 1.0) && count <= dry.size();
			if (fading) {
				std::memcpy(dry.data(), a_samples, count * sizeof(float));
			}
		}

		void Blend(float* a_samples, std::uint32_t a_frames, std::uint32_t a_channels, bool a_on)
		{
			if (!fading) {
				wet = a_on ? 1.0 : 0.0;
				return;
			}
			for (std::uint32_t frame = 0; frame < a_frames; ++frame) {
				wet = a_on ? (std::min)(1.0, wet + step) : (std::max)(0.0, wet - step);
				for (std::uint32_t channel = 0; channel < a_channels; ++channel) {
					const std::size_t index = static_cast<std::size_t>(frame) * a_channels + channel;
					a_samples[index] = static_cast<float>(dry[index] + (static_cast<double>(a_samples[index]) - dry[index]) * wet);
				}
			}
		}

	private:
		static constexpr double kFadeSeconds = 0.02;

		std::vector<float> dry;
		double             wet{ 1.0 };
		double             step{ 0.0 };
		bool               fading{ false };
	};
}
