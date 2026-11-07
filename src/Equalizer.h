#pragma once

#include "Eq.h"
#include "PCH.h"

namespace Equalizer
{
	void        Attach();
	void        Apply();
	const char* Status();
	double      SampleRate();
	void        SetCapture(bool a_on);
	void        ReadLevels(float& a_input, float& a_output);
	void        ReadSpectrum(std::array<float, Eq::kSpectrumPoints>& a_decibels);
	std::size_t ReadBalance(std::array<float, Eq::kBalanceBands>& a_decibels);
	float       AutoGainDb();
}
