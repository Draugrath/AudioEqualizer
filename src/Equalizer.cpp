#include "Equalizer.h"
#include "Eq.h"
#include "Settings.h"

#include <RE/B/BSAudioManager.h>
#include <RE/B/BSXAudio2Audio.h>
#include <RE/I/IXAudio2Voice.h>
#include <RE/X/XAPO.h>
#include <RE/X/XAudio2.h>

#include <xmmintrin.h>

#include <atomic>
#include <cstring>
#include <mutex>

namespace
{
	constexpr GUID kIUnknown{ 0x00000000, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
	constexpr GUID kIXAPO{ 0xA90BC001, 0xE897, 0xE897, { 0x55, 0xE4, 0x9E, 0x47, 0x00, 0x00, 0x00, 0x00 } };

	constexpr std::int32_t kOk = 0;
	constexpr std::int32_t kNoInterface = static_cast<std::int32_t>(0x80004002);
	constexpr std::int32_t kFormatUnsupported = static_cast<std::int32_t>(0x88970001);
	constexpr std::uint32_t kFlags = 0x1 | 0x2 | 0x4 | 0x8 | 0x10;
	constexpr std::uint16_t kFormatFloat = 3;
	constexpr std::uint16_t kFormatExtensible = 0xFFFE;

	bool IsFloat(const RE::WAVEFORMATEX* a_format)
	{
		const auto& format = reinterpret_cast<const WAVEFORMATEX&>(*a_format);
		return format.wBitsPerSample == 32 && (format.wFormatTag == kFormatFloat || format.wFormatTag == kFormatExtensible);
	}

	std::array<float, Eq::kMaxChannels> MixWeights(std::uint32_t a_channels)
	{
		constexpr float kFront = 0.5f;
		constexpr float kCenter = 0.7071f;
		constexpr float kRear = 0.3536f;
		switch (a_channels) {
		case 1:
			return { 1.0f };
		case 2:
			return { kFront, kFront };
		case 3:
			return { kFront, kFront, kCenter };
		case 4:
			return { kFront, kFront, kRear, kRear };
		case 5:
			return { kFront, kFront, kCenter, kRear, kRear };
		case 6:
			return { kFront, kFront, kCenter, 0.0f, kRear, kRear };
		case 8:
			return { kFront, kFront, kCenter, 0.0f, kRear, kRear, kRear, kRear };
		default:
			return { kFront, kFront };
		}
	}

	class Effect final : public RE::IXAPO
	{
	public:
		std::int32_t QueryInterface(const REX::W32::GUID& a_iid, void** a_out) override
		{
			if (!a_out) {
				return kNoInterface;
			}
			if (std::memcmp(&a_iid, &kIUnknown, sizeof(GUID)) == 0 || std::memcmp(&a_iid, &kIXAPO, sizeof(GUID)) == 0) {
				*a_out = this;
				AddRef();
				return kOk;
			}
			*a_out = nullptr;
			return kNoInterface;
		}

		std::uint32_t AddRef() override { return ++references; }
		std::uint32_t Release() override { return --references; }

		std::int32_t GetRegistrationProperties(RE::XAPO_REGISTRATION_PROPERTIES** a_properties) override
		{
			auto* properties = static_cast<RE::XAPO_REGISTRATION_PROPERTIES*>(CoTaskMemAlloc(sizeof(RE::XAPO_REGISTRATION_PROPERTIES)));
			if (!properties) {
				return static_cast<std::int32_t>(0x8007000E);
			}
			std::memset(properties, 0, sizeof(*properties));
			wcscpy_s(properties->friendlyName, L"Audio Equalizer");
			properties->majorVersion = 1;
			properties->flags = kFlags;
			properties->minInputBufferCount = properties->maxInputBufferCount = 1;
			properties->minOutputBufferCount = properties->maxOutputBufferCount = 1;
			*a_properties = properties;
			return kOk;
		}

		std::int32_t IsInputFormatSupported(const RE::WAVEFORMATEX*, const RE::WAVEFORMATEX* a_requested, RE::WAVEFORMATEX** a_supported) override
		{
			if (a_supported) {
				*a_supported = nullptr;
			}
			return IsFloat(a_requested) ? kOk : kFormatUnsupported;
		}

		std::int32_t IsOutputFormatSupported(const RE::WAVEFORMATEX*, const RE::WAVEFORMATEX* a_requested, RE::WAVEFORMATEX** a_supported) override
		{
			if (a_supported) {
				*a_supported = nullptr;
			}
			return IsFloat(a_requested) ? kOk : kFormatUnsupported;
		}

		std::int32_t Initialize(const void*, std::uint32_t) override { return kOk; }

		void Reset() override
		{
			processor.Reset();
			balancer.Reset();
			autoGain.Reset();
		}

		std::int32_t LockForProcess(std::uint32_t, const RE::XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS* a_input, std::uint32_t, const RE::XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS*) override
		{
			const auto& format = reinterpret_cast<const WAVEFORMATEX&>(*a_input->format);
			channels = format.nChannels;
			mixWeights = MixWeights(channels);
			sampleRate.store(format.nSamplesPerSec);
			processor.Reset();
			balancer.Prepare(format.nSamplesPerSec);
			autoGain.Prepare(format.nSamplesPerSec);
			Publish(pendingBands, pendingInput, pendingOutput, pendingEnabled, pendingBalance, pendingAuto);
			bypass.Prepare(format.nSamplesPerSec, static_cast<std::size_t>(a_input->maxFrameCount) * channels, pendingEnabled);
			return kOk;
		}

		void UnlockForProcess() override {}

		void Process(std::uint32_t, const RE::XAPO_PROCESS_BUFFER_PARAMETERS* a_input, std::uint32_t, RE::XAPO_PROCESS_BUFFER_PARAMETERS* a_output, BOOL a_enabled) override
		{
			LARGE_INTEGER started{};
			QueryPerformanceCounter(&started);
			a_output->bufferFlags = a_input->bufferFlags;
			a_output->validFrameCount = a_input->validFrameCount;
			if (a_input->buffer != a_output->buffer) {
				std::memcpy(a_output->buffer, a_input->buffer, static_cast<std::size_t>(a_input->validFrameCount) * channels * sizeof(float));
			}
			Latch();
			active.store(true, std::memory_order_relaxed);
			const bool silent = a_input->bufferFlags == RE::XAPO_BUFFER_FLAGS::XAPO_BUFFER_SILENT;
			auto*      samples = static_cast<float*>(a_output->buffer);
			if (silent && !wasSilent) {
				processor.Reset();
				balancer.ClearFilters();
			}
			wasSilent = silent;
			const bool metering = capture.load(std::memory_order_relaxed);
			if (metering) {
				Note(peakInput, silent ? 0.0f : Eq::PeakOf(samples, a_input->validFrameCount, channels) * static_cast<float>(slots[front].filter.inputGain));
			}
			const bool wanted = enabledFlag.load(std::memory_order_relaxed);
			if (a_enabled && !silent && bypass.Active(wanted)) {
				const auto oldState = _mm_getcsr();
				_mm_setcsr(oldState | 0x8040);
				const std::uint32_t count = a_input->validFrameCount;
				if (bypass.Resuming(wanted)) {
					processor.Reset();
					balancer.ClearFilters();
				}
				bypass.Keep(samples, count, channels, wanted);
				balancer.Process(samples, count, channels, mixWeights.data(), balanceAmount.load(std::memory_order_relaxed));
				processor.Process(slots[front].filter, samples, count, channels);
				autoGain.Apply(samples, count, channels, slots[front].eqPower, balancer, autoOn.load(std::memory_order_relaxed));
				if (processor.Working() || balancer.Working() || autoGain.Working()) {
					Eq::Limit(samples, count, channels);
				}
				bypass.Blend(samples, count, channels, wanted);
				for (std::size_t band = 0; band < Eq::kBalanceBands; ++band) {
					balanceShown[band].store(band < balancer.Bands() ? static_cast<float>(balancer.Gain(band)) : 0.0f, std::memory_order_relaxed);
				}
				autoGainShown.store(static_cast<float>(autoGain.GainDb()), std::memory_order_relaxed);
				_mm_setcsr(oldState);
			}
			if (metering) {
				Note(peakOutput, silent ? 0.0f : Eq::PeakOf(samples, a_input->validFrameCount, channels));
				Capture(silent ? nullptr : samples, a_input->validFrameCount);
			}
			LARGE_INTEGER ended{};
			QueryPerformanceCounter(&ended);
			const auto spent = static_cast<std::uint64_t>(ended.QuadPart - started.QuadPart);
			spentTicks.fetch_add(spent, std::memory_order_relaxed);
			blocks.fetch_add(1, std::memory_order_relaxed);
			frames.fetch_add(a_input->validFrameCount, std::memory_order_relaxed);
			if (spent > mostTicks.load(std::memory_order_relaxed)) {
				mostTicks.store(spent, std::memory_order_relaxed);
			}
		}

		std::uint32_t CalcInputFrames(std::uint32_t a_frames) override { return a_frames; }
		std::uint32_t CalcOutputFrames(std::uint32_t a_frames) override { return a_frames; }

		void Publish(const Eq::Bands& a_bands, float a_inputDb, float a_outputDb, bool a_enabled, float a_balance, bool a_auto)
		{
			std::scoped_lock lock(writer);
			pendingBands = a_bands;
			pendingInput = a_inputDb;
			pendingOutput = a_outputDb;
			pendingEnabled = a_enabled;
			pendingBalance = a_balance;
			pendingAuto = a_auto;
			enabledFlag.store(a_enabled, std::memory_order_relaxed);
			autoOn.store(a_auto, std::memory_order_relaxed);
			balanceAmount.store(std::clamp(a_balance, 0.0f, 100.0f) / 100.0f, std::memory_order_relaxed);
			const auto rate = sampleRate.load();
			if (!rate) {
				return;
			}
			slots[back].filter = Eq::Design(a_bands, rate, a_inputDb, a_outputDb);
			Eq::MeasureEqPower(slots[back].filter, rate, slots[back].eqPower);
			back = shared.exchange(static_cast<std::uint8_t>(back | kFresh), std::memory_order_acq_rel) & kIndex;
		}

		[[nodiscard]] bool   Active() const { return active.load(std::memory_order_relaxed); }
		[[nodiscard]] double Rate() const { return sampleRate.load(); }

		[[nodiscard]] std::string LoadText()
		{
			static LARGE_INTEGER frequency = [] {
				LARGE_INTEGER value{};
				QueryPerformanceFrequency(&value);
				return value;
			}();
			const auto spent = spentTicks.exchange(0, std::memory_order_relaxed);
			const auto count = blocks.exchange(0, std::memory_order_relaxed);
			const auto total = frames.exchange(0, std::memory_order_relaxed);
			const auto most = mostTicks.exchange(0, std::memory_order_relaxed);
			const auto rate = sampleRate.load();
			if (!count || !total || !rate) {
				return shown;
			}
			const double msSpent = static_cast<double>(spent) * 1000.0 / static_cast<double>(frequency.QuadPart);
			const double msAudio = static_cast<double>(total) * 1000.0 / rate;
			if (msAudio >= 500.0) {
				shown = std::format("Audio thread: {:.3f} ms per block ({:.2f}%), peak {:.3f} ms.", msSpent / static_cast<double>(count), msSpent / msAudio * 100.0, static_cast<double>(most) * 1000.0 / static_cast<double>(frequency.QuadPart));
				return shown;
			}
			spentTicks.fetch_add(spent, std::memory_order_relaxed);
			blocks.fetch_add(count, std::memory_order_relaxed);
			frames.fetch_add(total, std::memory_order_relaxed);
			if (most > mostTicks.load(std::memory_order_relaxed)) {
				mostTicks.store(most, std::memory_order_relaxed);
			}
			return shown;
		}

		[[nodiscard]] float AutoGainShown() const { return autoGainShown.load(std::memory_order_relaxed); }

		std::size_t ReadBalance(std::array<float, Eq::kBalanceBands>& a_decibels) const
		{
			for (std::size_t band = 0; band < Eq::kBalanceBands; ++band) {
				a_decibels[band] = balanceShown[band].load(std::memory_order_relaxed);
			}
			return balancer.Bands();
		}

		void SetCapture(bool a_on)
		{
			if (a_on && !capture.load(std::memory_order_relaxed)) {
				for (auto& sample : ring) {
					sample.store(0.0f, std::memory_order_relaxed);
				}
				peakInput.store(0.0f, std::memory_order_relaxed);
				peakOutput.store(0.0f, std::memory_order_relaxed);
			}
			capture.store(a_on, std::memory_order_relaxed);
		}

		void ReadLevels(float& a_input, float& a_output)
		{
			a_input = peakInput.exchange(0.0f, std::memory_order_relaxed);
			a_output = peakOutput.exchange(0.0f, std::memory_order_relaxed);
		}

		void CopyRecent(float* a_out) const
		{
			const auto end = written.load(std::memory_order_acquire);
			for (std::size_t i = 0; i < Eq::kSpectrumSize; ++i) {
				a_out[i] = ring[(end - Eq::kSpectrumSize + i) & (ring.size() - 1)].load(std::memory_order_relaxed);
			}
		}

	private:
		struct Slot
		{
			Eq::Filter  filter{};
			Eq::EqPower eqPower{};
		};

		void Capture(const float* a_samples, std::uint32_t a_frames)
		{
			auto position = written.load(std::memory_order_relaxed);
			const std::uint32_t used = (std::min)(channels, static_cast<std::uint32_t>(Eq::kMaxChannels));
			for (std::uint32_t frame = 0; frame < a_frames; ++frame) {
				float mix = 0.0f;
				if (a_samples) {
					for (std::uint32_t channel = 0; channel < used; ++channel) {
						mix += a_samples[frame * channels + channel] * mixWeights[channel];
					}
				}
				ring[position++ & (ring.size() - 1)].store(mix, std::memory_order_relaxed);
			}
			written.store(position, std::memory_order_release);
		}

		static void Note(std::atomic<float>& a_peak, float a_value)
		{
			if (a_value > a_peak.load(std::memory_order_relaxed)) {
				a_peak.store(a_value, std::memory_order_relaxed);
			}
		}

		void Latch()
		{
			if (shared.load(std::memory_order_acquire) & kFresh) {
				front = shared.exchange(front, std::memory_order_acq_rel) & kIndex;
			}
		}

		std::atomic<std::uint32_t>   references{ 1 };
		std::atomic<std::uint32_t>   sampleRate{ 0 };
		std::atomic<bool>            active{ false };
		std::atomic<bool>            capture{ false };
		std::atomic<float>           peakInput{ 0.0f };
		std::atomic<float>           peakOutput{ 0.0f };
		std::atomic<std::size_t>     written{ 0 };
		std::array<std::atomic<float>, 16384> ring{};
		std::atomic<std::uint64_t>   spentTicks{ 0 };
		std::atomic<std::uint64_t>   mostTicks{ 0 };
		std::atomic<std::uint64_t>   blocks{ 0 };
		std::atomic<std::uint64_t>   frames{ 0 };
		bool                         wasSilent{ false };
		static constexpr std::uint8_t kIndex = 3;
		static constexpr std::uint8_t kFresh = 4;
		std::array<Slot, 3>          slots{};
		std::atomic<std::uint8_t>    shared{ 1 };
		std::uint8_t                 back{ 0 };
		std::uint8_t                 front{ 2 };
		std::mutex                   writer;
		Eq::Bands                    pendingBands{};
		float                        pendingInput{ 0.0f };
		float                        pendingOutput{ 0.0f };
		bool                         pendingEnabled{ false };
		float                        pendingBalance{ 0.0f };
		bool                         pendingAuto{ false };
		std::atomic<bool>            autoOn{ false };
		Eq::AutoGain                 autoGain;
		Eq::Bypass                   bypass;
		std::atomic<bool>            enabledFlag{ false };
		std::atomic<float>           autoGainShown{ 0.0f };
		std::atomic<float>           balanceAmount{ 0.0f };
		std::array<std::atomic<float>, Eq::kBalanceBands> balanceShown{};
		Eq::Balancer                 balancer;
		std::uint32_t                channels{ 2 };
		std::array<float, Eq::kMaxChannels> mixWeights{ 0.5f, 0.5f };
		Eq::Processor                processor;
		std::string                  shown;
	};

	Effect&     effect = *new Effect;
	bool        attached = false;
	std::string status = "Not attached.";
}

void Equalizer::Attach()
{
	auto* platform = RE::BSAudioManager::QPlatformInstance();
	if (!platform) {
		status = "The game's audio is not available.";
		logger::error("{}", status);
		return;
	}
	const auto system = platform->GetSystemName();
	auto*      audio = static_cast<RE::BSXAudio2Audio*>(platform);
	auto*      master = audio->masteringVoice;
	if (!master) {
		status = "The game has no master audio voice.";
		logger::error("{}", status);
		return;
	}
	RE::XAUDIO2_VOICE_DETAILS details{};
	reinterpret_cast<RE::IXAudio2Voice*>(master)->GetVoiceDetails(&details);
	logger::info("Audio system {}: master voice {} channels at {} Hz.", system.c_str(), details.InputChannels, details.InputSampleRate);

	Apply();
	RE::XAUDIO2_EFFECT_DESCRIPTOR descriptor{ &effect, 1, details.InputChannels };
	RE::XAUDIO2_EFFECT_CHAIN      chain{ 1, &descriptor };
	const auto                    result = reinterpret_cast<RE::IXAudio2Voice*>(master)->SetEffectChain(&chain);
	if (result < 0) {
		status = std::format("The game refused the effect (0x{:08X}).", static_cast<std::uint32_t>(result));
		logger::error("{}", status);
		return;
	}
	attached = true;
	status = std::format("Attached: {} channels at {} Hz.", details.InputChannels, details.InputSampleRate);
	logger::info("Equalizer attached to the master voice.");
}

void Equalizer::SetCapture(bool a_on)
{
	effect.SetCapture(a_on);
}

void Equalizer::ReadLevels(float& a_input, float& a_output)
{
	effect.ReadLevels(a_input, a_output);
}

void Equalizer::ReadSpectrum(std::array<float, Eq::kSpectrumPoints>& a_decibels)
{
	static std::vector<float> samples(Eq::kSpectrumSize);
	effect.CopyRecent(samples.data());
	Eq::Analyze(samples.data(), SampleRate(), a_decibels);
}

void Equalizer::Apply()
{
	const auto& settings = Settings::GetSingleton();
	effect.Publish(settings.bands, settings.inputGain, settings.outputGain, settings.enabled, settings.balance, settings.autoGain);
}

std::size_t Equalizer::ReadBalance(std::array<float, Eq::kBalanceBands>& a_decibels)
{
	return effect.ReadBalance(a_decibels);
}

float Equalizer::AutoGainDb()
{
	return effect.AutoGainShown();
}

double Equalizer::SampleRate()
{
	const auto rate = effect.Rate();
	return rate > 0.0 ? rate : 48000.0;
}

const char* Equalizer::Status()
{
	static std::string text;
	text = attached && !effect.Active() ? status + " Waiting for audio." : status;
	if (attached) {
		text += " " + effect.LoadText();
	}
	return text.c_str();
}
