/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2026  Equalizer APO contributors

    Formula-driven loudness-profile parameters and helpers.
    Redistribution permission for the included profile data is documented in
    NOTICE.md. This implementation is not presented as a standards-conformance
    or certification implementation.
*/

#pragma once

#include <cmath>
#include <cstddef>
#include <limits>

namespace LoudnessProfile
{
	static const size_t FREQUENCY_COUNT = 29;
	static const size_t REFERENCE_FREQUENCY_INDEX = 17; // 1 kHz

	struct ContourParameters
	{
		double frequency;
		double alpha;
		double Lu;
		double Tf;
	};

	// Source: filters/loudnessCorrection/loudness_profile.csv. Keep the four
	// columns byte-for-byte in sync; tests/test_loudness_profile.py enforces it.
	static const ContourParameters LOUDNESS_PROFILE_TABLE[FREQUENCY_COUNT] = {
		{   20.0, 0.635, -31.5, 78.1 },
		{   25.0, 0.602, -27.2, 68.7 },
		{   31.5, 0.569, -23.1, 59.5 },
		{   40.0, 0.537, -19.3, 51.1 },
		{   50.0, 0.509, -16.1, 44.0 },
		{   63.0, 0.482, -13.1, 37.5 },
		{   80.0, 0.456, -10.4, 31.5 },
		{  100.0, 0.433,  -8.2, 26.5 },
		{  125.0, 0.412,  -6.3, 22.1 },
		{  160.0, 0.391,  -4.6, 17.9 },
		{  200.0, 0.373,  -3.2, 14.4 },
		{  250.0, 0.357,  -2.1, 11.4 },
		{  315.0, 0.343,  -1.2,  8.6 },
		{  400.0, 0.330,  -0.5,  6.2 },
		{  500.0, 0.320,   0.0,  4.4 },
		{  630.0, 0.311,   0.4,  3.0 },
		{  800.0, 0.303,   0.5,  2.2 },
		{ 1000.0, 0.300,   0.0,  2.4 },
		{ 1250.0, 0.295,  -2.7,  3.5 },
		{ 1600.0, 0.292,  -4.2,  1.7 },
		{ 2000.0, 0.290,  -1.2, -1.3 },
		{ 2500.0, 0.290,   1.4, -4.2 },
		{ 3150.0, 0.289,   2.3, -6.0 },
		{ 4000.0, 0.289,   1.0, -5.4 },
		{ 5000.0, 0.289,  -2.3, -1.5 },
		{ 6300.0, 0.293,  -7.2,  6.0 },
		{ 8000.0, 0.303, -11.2, 12.6 },
		{10000.0, 0.323, -10.9, 13.9 },
		{12500.0, 0.354,  -3.5, 12.3 }
	};

	// User-supplied loudness formula evaluated from the parameter table above.
	inline double computeSPLFromFormula(double loudnessLevel, double alpha, double Lu, double Tf)
	{
		if (!std::isfinite(loudnessLevel) || !std::isfinite(alpha) ||
			!std::isfinite(Lu) || !std::isfinite(Tf) || alpha <= 0.0)
		{
			return std::numeric_limits<double>::quiet_NaN();
		}

		double argument = std::pow(4.0e-10, 0.3 - alpha) *
			(std::pow(10.0, 0.03 * loudnessLevel) - std::pow(10.0, 0.072)) +
			std::pow(10.0, alpha * (Tf + Lu) / 10.0);
		if (!std::isfinite(argument) || argument <= 0.0)
			return std::numeric_limits<double>::quiet_NaN();

		return (10.0 / alpha) * std::log10(argument) - Lu;
	}

	inline double computeSPL(double loudnessLevel, size_t index)
	{
		if (index >= FREQUENCY_COUNT || !std::isfinite(loudnessLevel))
			return std::numeric_limits<double>::quiet_NaN();

		double clampedLevel = loudnessLevel;
		if (clampedLevel < 0.0)
			clampedLevel = 0.0;
		else if (clampedLevel > 100.0)
			clampedLevel = 100.0;

		const ContourParameters& parameters = LOUDNESS_PROFILE_TABLE[index];
		return computeSPLFromFormula(clampedLevel, parameters.alpha,
			parameters.Lu, parameters.Tf);
	}

	// Relative compensation at frequency f, normalized to the 1 kHz row.
	inline double computeContourDelta(double loudnessLevel, double referenceLevel, size_t index)
	{
		if (index >= FREQUENCY_COUNT)
			return 0.0;

		double currentShape = computeSPL(loudnessLevel, index) -
			computeSPL(loudnessLevel, REFERENCE_FREQUENCY_INDEX);
		double referenceShape = computeSPL(referenceLevel, index) -
			computeSPL(referenceLevel, REFERENCE_FREQUENCY_INDEX);
		return currentShape - referenceShape;
	}
}
