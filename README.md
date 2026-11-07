# Audio Equalizer

SKSE plugin that processes Skyrim's final audio mix in real time: an 8-band parametric EQ, spectral balance toward pink noise and automatic gain compensation, with a window in SKSE Menu Framework.

## Features

- 8-band parametric EQ: bell, low and high shelf, low and high pass (12 to 96 dB/oct), double-precision filters, zero latency
- Live spectrum of the processed output
- Balance: zero-latency spectral balance toward a pink noise curve over 19 half-octave bands (cuts up to 6 dB, boosts up to 3 dB)
- Auto gain: compensates the level change of the EQ (from its curve against pink noise) and of Balance (from the measured band levels), K-weighted
- Input and output gain with peak meters, one soft limiter at the end of the chain
- Click-free bypass and parameter changes

## Requirements

- Skyrim Special Edition or Anniversary Edition (built for SE and AE, tested on 1.7.104)
- [SKSE](https://skse.silverlock.org/)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
- [SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/118580)

## Usage

Press F4 in game, or open Audio Equalizer in the SKSE Menu Framework menu, where the key can be changed. Save Preset writes all settings to `Data/SKSE/Plugins/AudioEqualizer.ini`.

## How it works

An XAPO effect on the game's XAudio2 mastering voice processes every sound the game plays, including sounds and music added by mods. Order: Balance, EQ, auto gain, soft limiter.

## Building

Visual Studio 2022 with CMake and vcpkg (`VCPKG_ROOT` set). Run `BuildRelease.bat`; CommonLibSSE-NG and spdlog are fetched into `extern/`. The package is written to `dist/`.

## License

GPL-3.0 with the exceptions in [EXCEPTIONS.md](EXCEPTIONS.md). See [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt) for third-party licenses.
