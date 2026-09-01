/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2026  Equalizer APO contributors

    Data-driven loudness-profile values and interpolation helpers.
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
	static const size_t LOUDNESS_LEVEL_COUNT = 11;
	static const size_t REFERENCE_FREQUENCY_INDEX = 17; // 1 kHz

	struct ContourPoint
	{
		double frequency;
		double hearingThreshold;
		double spl[LOUDNESS_LEVEL_COUNT];
	};

	// Source: filters/loudnessCorrection/loudness_profile.csv.
	// Columns are 0, 10, ... 100 phon. Keep this table byte-for-byte in sync
	// with the CSV; tests/test_loudness_profile.py enforces that invariant.
	static const ContourPoint LOUDNESS_PROFILE_TABLE[FREQUENCY_COUNT] = {
		{   20.0, 78.1, {75.9, 83.7, 89.5, 94.8, 99.7, 104.6, 109.4, 114.1, 118.9, 123.6, 128.3} },
		{   25.0, 68.7, {64.7, 75.5, 82.4, 88.7, 94.6, 100.4, 106.1, 111.7, 117.2, 122.7, 128.1} },
		{   31.5, 59.5, {54.3, 66.8, 74.8, 81.8, 88.2, 94.4, 100.5, 106.5, 112.4, 118.2, 124.0} },
		{   40.0, 51.1, {44.9, 58.4, 67.5, 75.2, 82.2, 88.8, 95.3, 101.7, 108.0, 114.2, 120.4} },
		{   50.0, 44.0, {37.1, 51.2, 61.0, 69.4, 76.9, 84.0, 90.9, 97.7, 104.4, 111.0, 117.5} },
		{   63.0, 37.5, {30.0, 44.1, 54.7, 63.8, 71.9, 79.5, 86.8, 94.0, 101.1, 108.1, 115.0} },
		{   80.0, 31.5, {23.6, 37.8, 48.8, 58.4, 66.9, 75.0, 82.8, 90.4, 98.0, 105.4, 112.8} },
		{  100.0, 26.5, {19.2, 38.6, 48.6, 56.7, 64.2, 71.4, 78.5, 85.5, 92.5, 99.4, 106.3} },
		{  125.0, 22.1, {14.7, 38.1, 47.2, 54.9, 62.1, 69.0, 75.8, 82.6, 89.4, 96.2, 103.0} },
		{  160.0, 17.9, {10.9, 38.1, 46.8, 54.2, 61.2, 68.0, 74.7, 81.5, 88.2, 95.0, 101.8} },
		{  200.0, 14.4, { 7.7, 36.4, 45.3, 52.7, 59.7, 66.5, 73.3, 80.1, 86.9, 93.8, 100.6} },
		{  250.0, 11.4, { 5.0, 34.4, 43.6, 51.1, 58.3, 65.2, 72.0, 78.9, 85.8, 92.7,  99.7} },
		{  315.0,  8.6, { 2.5, 31.8, 41.6, 49.3, 56.6, 63.6, 70.6, 77.6, 84.6, 91.6,  98.7} },
		{  400.0,  6.2, { 0.4, 28.9, 39.3, 47.2, 54.7, 61.9, 69.1, 76.3, 83.5, 90.7,  97.9} },
		{  500.0,  4.4, {-1.1, 26.2, 37.1, 45.3, 52.9, 60.3, 67.6, 74.9, 82.2, 89.5,  96.8} },
		{  630.0,  3.0, {-1.9, 23.9, 35.1, 43.5, 51.3, 58.8, 66.2, 73.6, 81.0, 88.4,  95.8} },
		{  800.0,  2.2, {-2.4, 21.8, 33.4, 42.0, 49.9, 57.5, 65.0, 72.5, 80.0, 87.4,  94.9} },
		{ 1000.0,  2.4, { 0.0, 10.0, 20.0, 30.0, 40.0, 50.0, 60.0, 70.0, 80.0, 90.0, 100.0} },
		{ 1250.0,  3.5, { 1.7, 12.4, 22.0, 31.6, 41.1, 50.6, 60.1, 69.6, 79.0, 88.5,  97.9} },
		{ 1600.0,  1.7, { 0.5, 11.1, 20.8, 30.4, 39.9, 49.5, 59.0, 68.4, 77.9, 87.4,  96.8} },
		{ 2000.0, -1.3, {-2.4,  7.2, 17.2, 26.9, 36.5, 46.1, 55.6, 65.1, 74.6, 84.1,  93.6} },
		{ 2500.0, -4.2, {-5.4,  3.5, 13.6, 23.3, 32.9, 42.5, 52.1, 61.6, 71.1, 80.6,  90.0} },
		{ 3150.0, -6.0, {-9.0,  3.0, 14.2, 24.9, 35.5, 45.9, 56.4, 66.8, 77.2, 87.5,  97.9} },
		{ 4000.0, -5.4, {-7.9,  3.7, 15.0, 25.7, 36.2, 46.7, 57.1, 67.5, 77.8, 88.1,  98.4} },
		{ 5000.0, -1.5, {-3.0,  7.9, 19.1, 29.8, 40.3, 50.7, 61.0, 71.4, 81.6, 91.9, 102.1} },
		{ 6300.0,  6.0, { 3.8, 13.7, 24.4, 34.9, 45.2, 55.4, 65.6, 75.7, 85.9, 96.0, 106.1} },
		{ 8000.0, 12.6, { 9.5, 18.4, 28.5, 38.7, 48.9, 59.0, 69.1, 79.2, 89.3, 99.4, 109.4} },
		{10000.0, 13.9, { 9.4, 16.7, 26.6, 36.8, 46.9, 57.0, 67.1, 77.2, 87.2, 97.3, 107.3} },
		{12500.0, 12.3, { 8.2, 22.4, 33.0, 42.4, 51.3, 60.0, 68.6, 77.1, 85.6, 94.1, 102.6} }
	};

	// User-supplied loudness formula. The CSV does not contain the
	// alpha_f/L_U/T_f parameter table, so runtime correction deliberately uses
	// the numerical CSV above instead of inventing or reusing 2003 parameters.
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

		if (loudnessLevel <= 0.0)
			return LOUDNESS_PROFILE_TABLE[index].spl[0];
		if (loudnessLevel >= 100.0)
			return LOUDNESS_PROFILE_TABLE[index].spl[LOUDNESS_LEVEL_COUNT - 1];

		double position = loudnessLevel / 10.0;
		size_t lowerIndex = static_cast<size_t>(std::floor(position));
		double fraction = position - static_cast<double>(lowerIndex);
		double lower = LOUDNESS_PROFILE_TABLE[index].spl[lowerIndex];
		double upper = LOUDNESS_PROFILE_TABLE[index].spl[lowerIndex + 1];
		return lower + fraction * (upper - lower);
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
