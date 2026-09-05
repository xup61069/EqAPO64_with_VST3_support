/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2012  Jonas Thedering

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "stdafx.h"
#ifdef DEBUG
#include <stdlib.h>
#include <crtdbg.h>
#endif
#include <cstdio>
#define _USE_MATH_DEFINES
#include <cmath>
#include <algorithm>
#include <string>
#include <vector>
#include <sndfile.h>
#include <tclap/CmdLine.h>
#include <fftw3.h>

#include "../version.h"
#include "../FilterEngine.h"
#include "../libHybridConv-0.1.1/libHybridConv_eapo.h"
#include "../helpers/LogHelper.h"
#include "../helpers/StringHelper.h"
#include "../helpers/PrecisionTimer.h"
#include "../helpers/MemoryHelper.h"
#include "../filters/loudnessCorrection/LoudnessCorrectionFilter.h"

using namespace std;

class LoudnessCorrectionFilterTestAccess
{
public:
	static void publishVolumeUpdate(
		LoudnessCorrectionFilter& filter,
		double volume)
	{
		vector<double> scratchGains;
		filter.publishVolumeUpdate(volume, scratchGains);
	}

	static void beginRuntimeBypass(LoudnessCorrectionFilter& filter)
	{
		filter._recoveryPending.store(false, std::memory_order_release);
		filter._runtimeBypass.store(true, std::memory_order_release);
	}

	static void publishRuntimeRecovery(
		LoudnessCorrectionFilter& filter,
		double volume)
	{
		publishVolumeUpdate(filter, volume);
		filter._recoveryPending.store(true, std::memory_order_release);
	}

	static void calculateTransfer(
		const LoudnessCorrectionFilter& filter,
		double volume,
		vector<double>& gains,
		double& outputGainLinear)
	{
		filter.calculateBandGains(volume, gains, outputGainLinear);
	}

	static double rawCorrectionResponseDb(
		const LoudnessCorrectionFilter& filter,
		const vector<double>& gains,
		double frequency)
	{
		double response = 0.0;
		for (size_t band = 0; band < filter._activeBandCount; ++band)
			response += filter.biquadResponseDb(band, gains[band], frequency);
		return response;
	}

	static double aggregateRawCorrectionResponseDb(
		const LoudnessCorrectionFilter& filter,
		const vector<double>& gains,
		double frequency)
	{
		LoudnessCorrectionFilter::BiquadCoeffs coefficients[
			LoudnessCorrectionFilter::NUM_BANDS];
		for (size_t band = 0; band < filter._activeBandCount; ++band)
			filter.computeBiquadCoeffs(band, gains[band], coefficients[band]);
		return filter.bandResponseDb(
			coefficients, filter._activeBandCount, frequency);
	}

	static double guardedResponseDb(
		const LoudnessCorrectionFilter& filter,
		const vector<double>& gains,
		double outputGainLinear,
		double frequency)
	{
		return filter.guardedResponseDb(gains, outputGainLinear, frequency);
	}

	static double maximumGuardedResponseDb(
		const LoudnessCorrectionFilter& filter,
		const vector<double>& gains,
		double outputGainLinear)
	{
		return filter.findMaximumResponseDb(gains, outputGainLinear, true);
	}

	static double maximumRawCorrectionResponseDb(
		const LoudnessCorrectionFilter& filter,
		const vector<double>& gains)
	{
		return filter.findMaximumResponseDb(gains, 1.0, false);
	}

	static size_t activeBandCount(const LoudnessCorrectionFilter& filter)
	{
		return filter._activeBandCount;
	}

	static unsigned warmupLength(const LoudnessCorrectionFilter& filter)
	{
		return filter._warmupLength;
	}

	static unsigned crossoverPrewarmLength(const LoudnessCorrectionFilter& filter)
	{
		return filter._crossoverPrewarmLength;
	}

	static bool crossoverPrewarmActive(const LoudnessCorrectionFilter& filter)
	{
		return filter._crossoverPrewarmActive;
	}

	static bool crossoverDomainReady(const LoudnessCorrectionFilter& filter)
	{
		return !filter._crossoverPrewarmActive &&
			!filter._crossoverHandoffActive &&
			filter._crossoverDomainChannelCount == filter._channelCount;
	}

	static bool runtimeBypass(const LoudnessCorrectionFilter& filter)
	{
		return filter._runtimeBypass.load(std::memory_order_acquire);
	}

	static double maximumCrossoverHandoffStep(
		const LoudnessCorrectionFilter& filter)
	{
		return filter._maximumCrossoverHandoffStep;
	}

	static bool isSafeCrossoverHandoff(
		double previousRaw,
		double currentRaw,
		double previousIdentity,
		double currentIdentity,
		double& handoffStep,
		double& naturalStep)
	{
		return LoudnessCorrectionFilter::isSafeCrossoverHandoff(
			previousRaw,
			currentRaw,
			previousIdentity,
			currentIdentity,
			handoffStep,
			naturalStep);
	}

	static bool crossoverChannelInDomain(
		const LoudnessCorrectionFilter& filter,
		size_t channel)
	{
		return channel < filter._crossoverDomainActive.size() &&
			filter._crossoverDomainActive[channel] != 0;
	}

	static complex<double> crossoverIdentityResponse(
		const LoudnessCorrectionFilter& filter,
		double frequency)
	{
		complex<double> lowpass(1.0, 0.0);
		complex<double> highpass(1.0, 0.0);
		for (size_t section = 0;
			section < LoudnessCorrectionFilter::CROSSOVER_SECTION_COUNT;
			++section)
		{
			lowpass *= filter.biquadResponse(
				filter._lowpassCoeffs[section], frequency);
			highpass *= filter.biquadResponse(
				filter._highpassCoeffs[section], frequency);
		}
		return lowpass + highpass;
	}

	static bool settledOnNonIdentityBank(
		const LoudnessCorrectionFilter& filter)
	{
		return crossoverDomainReady(filter) && !filter._warmupActive &&
			!filter._crossfadeActive &&
			!filter._runtimeBypass.load(std::memory_order_acquire) &&
			!filter._bankIdentity[filter._activeBankIndex];
	}

	static bool warmupActive(const LoudnessCorrectionFilter& filter)
	{
		return filter._warmupActive;
	}

	static unsigned warmupPosition(const LoudnessCorrectionFilter& filter)
	{
		return filter._warmupPosition;
	}

	static bool crossfadeActive(const LoudnessCorrectionFilter& filter)
	{
		return filter._crossfadeActive;
	}

	static unsigned crossfadePosition(const LoudnessCorrectionFilter& filter)
	{
		return filter._crossfadePosition;
	}

	static bool bypassFadeActive(const LoudnessCorrectionFilter& filter)
	{
		return filter._bypassFadeActive;
	}

	static unsigned bypassFadeLength(const LoudnessCorrectionFilter& filter)
	{
		return filter._bypassFadeLength;
	}

	static bool holdingBypassOnIdentityBank(
		const LoudnessCorrectionFilter& filter)
	{
		return crossoverDomainReady(filter) && !filter._warmupActive &&
			!filter._crossfadeActive && !filter._bypassFadeActive &&
			filter._runtimeBypass.load(std::memory_order_acquire) &&
			!filter._recoveryPending.load(std::memory_order_acquire);
	}

	static bool settledOnIdentityBank(
		const LoudnessCorrectionFilter& filter)
	{
		return crossoverDomainReady(filter) && !filter._warmupActive &&
			!filter._crossfadeActive &&
			!filter._runtimeBypass.load(std::memory_order_acquire) &&
			filter._bankIdentity[filter._activeBankIndex];
	}

	static unsigned crossfadeLength(const LoudnessCorrectionFilter& filter)
	{
		return filter._crossfadeLength;
	}

	static bool canTrackAutomaticVolume(
		const LoudnessCorrectionFilter& filter)
	{
		return filter.canTrackAutomaticVolume();
	}

	static bool inspectCrossover(
		const LoudnessCorrectionFilter& filter,
		double& maximumPoleRadius,
		double& maximumComplementError)
	{
		maximumPoleRadius = 0.0;
		maximumComplementError = 0.0;
		for (size_t section = 0;
			section < LoudnessCorrectionFilter::CROSSOVER_SECTION_COUNT;
			++section)
		{
			const LoudnessCorrectionFilter::BiquadCoeffs* coefficients[] = {
				&filter._lowpassCoeffs[section],
				&filter._highpassCoeffs[section]
			};
			for (const LoudnessCorrectionFilter::BiquadCoeffs* coeffs : coefficients)
			{
				if (!std::isfinite(coeffs->b0) || !std::isfinite(coeffs->b1) ||
					!std::isfinite(coeffs->b2) || !std::isfinite(coeffs->a1) ||
					!std::isfinite(coeffs->a2))
				{
					return false;
				}

				std::complex<double> discriminant(
					coeffs->a1 * coeffs->a1 - 4.0 * coeffs->a2,
					0.0);
				std::complex<double> squareRoot = std::sqrt(discriminant);
				std::complex<double> pole1 = (-coeffs->a1 + squareRoot) / 2.0;
				std::complex<double> pole2 = (-coeffs->a1 - squareRoot) / 2.0;
				maximumPoleRadius = (std::max)(maximumPoleRadius,
					(std::max)(std::abs(pole1), std::abs(pole2)));
			}
		}

		const double frequencies[] = { 1.0, 5.0, 10.0, 15.0, 19.0,
			20.0, 25.0, 31.5, 100.0 };
		for (double frequency : frequencies)
		{
			std::complex<double> lowpass(1.0, 0.0);
			std::complex<double> highpass(1.0, 0.0);
			for (size_t section = 0;
				section < LoudnessCorrectionFilter::CROSSOVER_SECTION_COUNT;
				++section)
			{
				lowpass *= filter.biquadResponse(
					filter._lowpassCoeffs[section], frequency);
				highpass *= filter.biquadResponse(
					filter._highpassCoeffs[section], frequency);
			}
			maximumComplementError = (std::max)(maximumComplementError,
				std::abs(std::abs(lowpass + highpass) - 1.0));
		}
		return true;
	}
};

namespace
{
	int nextPowerOfTwo(int value)
	{
		int result = 1;
		while (result < value)
			result <<= 1;
		return result;
	}

	void referenceConvolutionFft(
		const vector<double>& input,
		const vector<double>& impulse,
		vector<double>& output)
	{
		const int resultLength =
			static_cast<int>(input.size() + impulse.size() - 1);
		const int fftLength = nextPowerOfTwo(resultLength);

		double* xTime = static_cast<double*>(
			fftw_malloc(sizeof(double) * fftLength));
		double* hTime = static_cast<double*>(
			fftw_malloc(sizeof(double) * fftLength));
		fftw_complex* xFreq = static_cast<fftw_complex*>(
			fftw_malloc(sizeof(fftw_complex) * (fftLength / 2 + 1)));
		fftw_complex* hFreq = static_cast<fftw_complex*>(
			fftw_malloc(sizeof(fftw_complex) * (fftLength / 2 + 1)));

		memset(xTime, 0, sizeof(double) * fftLength);
		memset(hTime, 0, sizeof(double) * fftLength);
		memcpy(xTime, input.data(), sizeof(double) * input.size());
		memcpy(hTime, impulse.data(), sizeof(double) * impulse.size());

		fftw_plan xPlan = fftw_plan_dft_r2c_1d(
			fftLength, xTime, xFreq, FFTW_ESTIMATE);
		fftw_plan hPlan = fftw_plan_dft_r2c_1d(
			fftLength, hTime, hFreq, FFTW_ESTIMATE);
		fftw_execute(xPlan);
		fftw_execute(hPlan);

		for (int index = 0; index < fftLength / 2 + 1; ++index)
		{
			const double real =
				xFreq[index][0] * hFreq[index][0] -
				xFreq[index][1] * hFreq[index][1];
			const double imaginary =
				xFreq[index][0] * hFreq[index][1] +
				xFreq[index][1] * hFreq[index][0];
			xFreq[index][0] = real;
			xFreq[index][1] = imaginary;
		}

		fftw_plan yPlan = fftw_plan_dft_c2r_1d(
			fftLength, xFreq, xTime, FFTW_ESTIMATE);
		fftw_execute(yPlan);

		output.resize(input.size());
		for (size_t index = 0; index < output.size(); ++index)
			output[index] = xTime[index] / fftLength;

		fftw_destroy_plan(yPlan);
		fftw_destroy_plan(hPlan);
		fftw_destroy_plan(xPlan);
		fftw_free(hFreq);
		fftw_free(xFreq);
		fftw_free(hTime);
		fftw_free(xTime);
	}

	double deterministicNoise(unsigned& state)
	{
		state = state * 1664525u + 1013904223u;
		return ((state >> 8) / 16777216.0) * 2.0 - 1.0;
	}

	bool runConvolutionSelfTestCase(
		int sampleRate,
		int frameLength,
		int impulseLength,
		int blocks)
	{
		const int inputLength = frameLength * blocks;
		vector<double> input(inputLength);
		vector<double> impulse(impulseLength);
		vector<double> actual(inputLength);
		vector<double> reference;
		vector<double> block(frameLength);

		unsigned state =
			0x12345678u ^ static_cast<unsigned>(sampleRate) ^
			static_cast<unsigned>(frameLength);
		for (int index = 0; index < inputLength; ++index)
		{
			const double time = static_cast<double>(index) / sampleRate;
			input[index] = 0.17 * sin(2.0 * M_PI * 997.0 * time) +
				0.11 * sin(2.0 * M_PI * 1234.5 * time) +
				0.03 * deterministicNoise(state);
		}

		for (int index = 0; index < impulseLength; ++index)
		{
			const double decay =
				exp(-static_cast<double>(index) / (0.065 * sampleRate));
			impulse[index] = decay *
				(0.012 * sin(0.013 * index) +
					0.006 * deterministicNoise(state));
		}

		if (!impulse.empty())
		{
			impulse[0] += 0.55;
			for (int index = frameLength - 1;
				index < impulseLength;
				index += frameLength)
			{
				impulse[index] += 0.04 *
					((index / frameLength) % 2 == 0 ? 1.0 : -1.0);
			}
			for (int index = frameLength;
				index < impulseLength;
				index += frameLength)
			{
				impulse[index] += 0.035 *
					((index / frameLength) % 2 == 0 ? -1.0 : 1.0);
			}
		}

		referenceConvolutionFft(input, impulse, reference);

		HConvSingle filter;
		hcInitSingle(&filter, impulse.data(), impulseLength, frameLength, 1);
		for (int blockIndex = 0; blockIndex < blocks; ++blockIndex)
		{
			hcPutSingle(
				&filter, input.data() + static_cast<size_t>(blockIndex) * frameLength);
			hcProcessSingle(&filter);
			hcGetSingle(&filter, block.data());
			memcpy(
				actual.data() + static_cast<size_t>(blockIndex) * frameLength,
				block.data(),
				sizeof(double) * frameLength);
		}
		hcCloseSingle(&filter);

		double maxAbsError = 0.0;
		double rmsError = 0.0;
		double maxReference = 0.0;
		int maxIndex = 0;
		for (int index = 0; index < inputLength; ++index)
		{
			const double error = fabs(actual[index] - reference[index]);
			if (error > maxAbsError)
			{
				maxAbsError = error;
				maxIndex = index;
			}
			rmsError += error * error;
			maxReference = max(maxReference, fabs(reference[index]));
		}
		rmsError = sqrt(rmsError / inputLength);
		const double relativeError = maxReference > 0.0 ?
			maxAbsError / maxReference : maxAbsError;
		const bool passed = maxAbsError < 1e-8 || relativeError < 1e-8;

		printf(
			"%s sr=%d flen=%d hlen=%d blocks=%d max=%0.12g rel=%0.12g rms=%0.12g idx=%d\n",
			passed ? "PASS" : "FAIL",
			sampleRate,
			frameLength,
			impulseLength,
			blocks,
			maxAbsError,
			relativeError,
			rmsError,
			maxIndex);

		return passed;
	}

	int runConvolutionSelfTest()
	{
		struct TestCase
		{
			int sampleRate;
			int frameLength;
			int impulseLength;
			int blocks;
		};

		const TestCase tests[] = {
			{ 44100, 256, 8192, 80 },
			{ 48000, 256, 8916, 80 },
			{ 96000, 512, 17832, 64 },
			{ 192000, 1024, 35664, 48 },
			{ 192000, 256, 35664, 80 },
		};

		bool passed = true;
		printf("Running internal HybridConv correctness benchmark...\n");
		for (const TestCase& test : tests)
		{
			passed = runConvolutionSelfTestCase(
				test.sampleRate,
				test.frameLength,
				test.impulseLength,
				test.blocks) && passed;
		}

		printf("HybridConv correctness benchmark: %s\n",
			passed ? "PASS" : "FAIL");
		return passed ? 0 : 2;
	}

	bool checkLoudnessFormulaValue(
		const char* name,
		size_t frequencyIndex,
		double phon,
		double expected)
	{
		double actual = LoudnessProfile::computeSPL(phon, frequencyIndex);
		double error = std::abs(actual - expected);
		printf("Loudness formula %s: %.12f dB\n", name, actual);
		if (!std::isfinite(actual) || error > 1.0e-9)
		{
			fprintf(stderr,
				"%s formula mismatch: expected %.12f dB, got %.12f dB.\n",
				name, expected, actual);
			return false;
		}
		return true;
	}

	bool runLoudnessFormulaTests()
	{
		bool passed = true;
		passed = checkLoudnessFormulaValue(
			"20Hz-20phon", 0, 20.0, 89.544478418546) && passed;
		passed = checkLoudnessFormulaValue(
			"100Hz-80phon", 7, 80.0, 92.460642396735) && passed;
		passed = checkLoudnessFormulaValue(
			"12500Hz-80phon", 28, 80.0, 85.605878244606) && passed;
		return passed;
	}

	bool runLoudnessParameterCodecTests()
	{
		auto checkCase = [](const char* name, bool result)
		{
			if (!result)
				fprintf(stderr, "Loudness parameter codec case '%s' failed.\n", name);
			return result;
		};

		LoudnessCorrectionFilter::FilterParameters source;
		source.state = false;
		source.referenceLevel = 75.5f;
		source.referenceOffset = -2.25f;
		source.attenuation = 0.75f;
		source.useManualVolume = true;
		source.manualVolume = -24.5f;
		source.binding = LoudnessCorrectionFilter::FilterParameters::BINDING_ALL;

		std::vector<char> serialized = source.serialize();
		std::string serializedText(serialized.begin(), serialized.end());
		LoudnessCorrectionFilter::FilterParameters roundTrip(serialized);
		bool roundTripPassed = serializedText.rfind(
			"Schema 1 Model FormulaLoudnessV1 Binding All ", 0) == 0
			&& roundTrip.isInitialized()
			&& roundTrip.binding ==
				LoudnessCorrectionFilter::FilterParameters::BINDING_ALL
			&& !roundTrip.state
			&& std::abs(roundTrip.referenceLevel - 75.5f) < 1.0e-6f
			&& std::abs(roundTrip.referenceOffset + 2.25f) < 1.0e-6f
			&& std::abs(roundTrip.attenuation - 0.75f) < 1.0e-6f
			&& roundTrip.useManualVolume
			&& std::abs(roundTrip.manualVolume + 24.5f) < 1.0e-6f;
		bool passed = checkCase("marked-round-trip", roundTripPassed);
		if (!roundTripPassed)
		{
			fprintf(stderr,
				"Serialized codec payload: '%.*s'; decoded initialized=%d state=%d "
				"referenceLevel=%.9g referenceOffset=%.9g attenuation=%.9g "
				"useManualVolume=%d manualVolume=%.9g binding=%d.\n",
				static_cast<int>(serializedText.size()), serializedText.c_str(),
				roundTrip.isInitialized(), roundTrip.state,
				roundTrip.referenceLevel, roundTrip.referenceOffset,
				roundTrip.attenuation, roundTrip.useManualVolume,
				roundTrip.manualVolume, static_cast<int>(roundTrip.binding));
		}

		LoudnessCorrectionFilter::FilterParameters commaDecimal(std::wstring(
			L"Schema 1 Model FormulaLoudnessV1 State 0 ReferenceLevel 75,5 "
			L"ReferenceOffset -2,25 Attenuation 0,75 Volume -24,5"));
		passed = checkCase("marked-comma-decimal", commaDecimal.isInitialized()
			&& commaDecimal.binding ==
				LoudnessCorrectionFilter::FilterParameters::BINDING_SINGLE
			&& !commaDecimal.state
			&& std::abs(commaDecimal.referenceLevel - 75.5f) < 1.0e-6f
			&& std::abs(commaDecimal.referenceOffset + 2.25f) < 1.0e-6f
			&& std::abs(commaDecimal.attenuation - 0.75f) < 1.0e-6f
			&& commaDecimal.useManualVolume
			&& std::abs(commaDecimal.manualVolume + 24.5f) < 1.0e-6f) && passed;

		LoudnessCorrectionFilter::FilterParameters explicitSingle(std::wstring(
			L"Schema 1 Model FormulaLoudnessV1 Binding Single State 1 "
			L"ReferenceLevel 80 ReferenceOffset 0 Attenuation 1"));
		passed = checkCase("explicit-single-binding", explicitSingle.isInitialized()
			&& explicitSingle.binding ==
				LoudnessCorrectionFilter::FilterParameters::BINDING_SINGLE) && passed;

		LoudnessCorrectionFilter::FilterParameters releasedFormula(std::wstring(
			L"State 1 ReferenceLevel 80 ReferenceOffset 0 Attenuation 1"));
		passed = checkCase("unmarked-formula-fails-closed",
			!releasedFormula.isInitialized()) && passed;

		LoudnessCorrectionFilter::FilterParameters mixomoLegacy(std::wstring(
			L"State 1 ReferenceLevel 0 ReferenceOffset 0 Attenuation 1"));
		LoudnessCorrectionFilter::FilterParameters unknownModel(std::wstring(
			L"Schema 2 Model FutureLoudness State 1 ReferenceLevel 80 "
			L"ReferenceOffset 0 Attenuation 1"));
		LoudnessCorrectionFilter::FilterParameters truncated(std::wstring(
			L"Schema 1 Model FormulaLoudnessV1 State 1 ReferenceLevel 80"));
		LoudnessCorrectionFilter::FilterParameters duplicate(std::wstring(
			L"Schema 1 Model FormulaLoudnessV1 State 1 State 0 "
			L"ReferenceLevel 80 ReferenceOffset 0 Attenuation 1"));
		LoudnessCorrectionFilter::FilterParameters invalidBinding(std::wstring(
			L"Schema 1 Model FormulaLoudnessV1 Binding Nearby State 1 "
			L"ReferenceLevel 80 ReferenceOffset 0 Attenuation 1"));
		LoudnessCorrectionFilter::FilterParameters duplicateBinding(std::wstring(
			L"Schema 1 Model FormulaLoudnessV1 Binding All Binding Single State 1 "
			L"ReferenceLevel 80 ReferenceOffset 0 Attenuation 1"));
		passed = checkCase("unmarked-mixomo-legacy-fails-closed",
			!mixomoLegacy.isInitialized()) && passed;
		passed = checkCase("unknown-model-fails-closed",
			!unknownModel.isInitialized()) && passed;
		passed = checkCase("truncated-input-fails-closed",
			!truncated.isInitialized()) && passed;
		passed = checkCase("duplicate-field-fails-closed",
			!duplicate.isInitialized()) && passed;
		passed = checkCase("invalid-binding-fails-closed",
			!invalidBinding.isInitialized()) && passed;
		passed = checkCase("duplicate-binding-fails-closed",
			!duplicateBinding.isInitialized()) && passed;

		LoudnessCorrectionFilter::FilterParameters fastEngine(std::wstring(
			L"Schema 1 Model FormulaLoudnessV1 Binding Single State 1 "
			L"ReferenceLevel 80 ReferenceOffset 0 Attenuation 1 Engine Fast"));
		passed = checkCase("fast-engine-parses",
			fastEngine.isInitialized() &&
			fastEngine.engine ==
				LoudnessCorrectionFilter::FilterParameters::ENGINE_FAST) && passed;

		LoudnessCorrectionFilter::FilterParameters defaultEngine(std::wstring(
			L"Schema 1 Model FormulaLoudnessV1 Binding Single State 1 "
			L"ReferenceLevel 80 ReferenceOffset 0 Attenuation 1"));
		passed = checkCase("absent-engine-defaults-to-full",
			defaultEngine.isInitialized() &&
			defaultEngine.engine ==
				LoudnessCorrectionFilter::FilterParameters::ENGINE_FULL) && passed;

		LoudnessCorrectionFilter::FilterParameters explicitFull(std::wstring(
			L"Schema 1 Model FormulaLoudnessV1 Binding Single State 1 "
			L"ReferenceLevel 80 ReferenceOffset 0 Attenuation 1 Engine Full"));
		passed = checkCase("explicit-full-engine-parses",
			explicitFull.isInitialized() &&
			explicitFull.engine ==
				LoudnessCorrectionFilter::FilterParameters::ENGINE_FULL) && passed;

		LoudnessCorrectionFilter::FilterParameters invalidEngine(std::wstring(
			L"Schema 1 Model FormulaLoudnessV1 Binding Single State 1 "
			L"ReferenceLevel 80 ReferenceOffset 0 Attenuation 1 Engine Turbo"));
		LoudnessCorrectionFilter::FilterParameters duplicateEngine(std::wstring(
			L"Schema 1 Model FormulaLoudnessV1 Binding Single State 1 "
			L"ReferenceLevel 80 ReferenceOffset 0 Attenuation 1 Engine Fast Engine Full"));
		passed = checkCase("invalid-engine-fails-closed",
			!invalidEngine.isInitialized()) && passed;
		passed = checkCase("duplicate-engine-fails-closed",
			!duplicateEngine.isInitialized()) && passed;

		// The default full engine omits the key so older profiles keep
		// their exact text; the fast engine round-trips explicitly.
		LoudnessCorrectionFilter::FilterParameters fastSource;
		fastSource.state = true;
		fastSource.referenceLevel = 80.0f;
		fastSource.referenceOffset = 0.0f;
		fastSource.attenuation = 1.0f;
		fastSource.useManualVolume = false;
		fastSource.binding = LoudnessCorrectionFilter::FilterParameters::BINDING_SINGLE;
		fastSource.engine = LoudnessCorrectionFilter::FilterParameters::ENGINE_FAST;
		std::vector<char> fastSerialized = fastSource.serialize();
		std::string fastText(fastSerialized.begin(), fastSerialized.end());
		LoudnessCorrectionFilter::FilterParameters fastRoundTrip(fastSerialized);
		passed = checkCase("fast-engine-round-trip",
			fastText.find("Engine Fast") != std::string::npos &&
			fastRoundTrip.isInitialized() &&
			fastRoundTrip.engine ==
				LoudnessCorrectionFilter::FilterParameters::ENGINE_FAST) && passed;
		std::vector<char> fullSerialized = source.serialize();
		std::string fullText(fullSerialized.begin(), fullSerialized.end());
		passed = checkCase("full-engine-omits-key",
			fullText.find("Engine") == std::string::npos) && passed;

		printf("Loudness parameter codec: %s\n", passed ? "passed" : "failed");
		return passed;
	}

	bool runLoudnessCrossoverCoefficientTests()
	{
		const unsigned sampleRates[] = {
			8000, 44100, 48000, 96000, 192000, 384000
		};
		bool passed = true;
		for (unsigned sampleRate : sampleRates)
		{
			LoudnessCorrectionFilter::FilterParameters parameters;
			parameters.state = true;
			parameters.referenceLevel = 80.0f;
			parameters.referenceOffset = 0.0f;
			parameters.attenuation = 1.0f;
			parameters.useManualVolume = true;
			parameters.manualVolume = 0.0f;

			LoudnessCorrectionFilter filter(parameters);
			vector<wstring> channels(1, L"C");
			filter.initialize(static_cast<float>(sampleRate), 256, channels);
			double maximumPoleRadius = 0.0;
			double maximumComplementError = 0.0;
			bool finite = LoudnessCorrectionFilterTestAccess::inspectCrossover(
				filter, maximumPoleRadius, maximumComplementError);
			printf(
				"Loudness crossover %u Hz: pole %.12f complement error %.3g\n",
				sampleRate, maximumPoleRadius, maximumComplementError);
			if (!finite || maximumPoleRadius >= 1.0 ||
				maximumComplementError > 1.0e-6)
			{
				fprintf(stderr,
					"LR28 crossover is non-finite, unstable, or non-complementary at %u Hz.\n",
					sampleRate);
				passed = false;
			}
		}
		return passed;
	}

	bool runLoudnessRuntimeContextTests()
	{
		LoudnessCorrectionFilter::FilterParameters parameters;
		parameters.state = true;
		parameters.useManualVolume = false;
		parameters.binding =
			LoudnessCorrectionFilter::FilterParameters::BINDING_ALL;
		LoudnessCorrectionFilter filter(parameters);

		// Global binding preserves the original Mixomo behavior: it reads the
		// Windows default render/multimedia volume without depending on APO
		// endpoint metadata.
		bool passed = LoudnessCorrectionFilterTestAccess::canTrackAutomaticVolume(
			filter);
		FilterRuntimeContext context;
		context.flowKnown = true;
		context.isCapture = true;
		context.endpointId = L"capture-endpoint";
		filter.setRuntimeContext(context);
		passed = LoudnessCorrectionFilterTestAccess::canTrackAutomaticVolume(
			filter) && passed;

		context.isCapture = false;
		context.endpointId.clear();
		filter.setRuntimeContext(context);
		passed = LoudnessCorrectionFilterTestAccess::canTrackAutomaticVolume(
			filter) && passed;

		parameters.binding =
			LoudnessCorrectionFilter::FilterParameters::BINDING_SINGLE;
		LoudnessCorrectionFilter singleFilter(parameters);
		singleFilter.setRuntimeContext(context);
		passed = !LoudnessCorrectionFilterTestAccess::canTrackAutomaticVolume(
			singleFilter) && passed;
		context.endpointId = L"render-endpoint";
		singleFilter.setRuntimeContext(context);
		passed = LoudnessCorrectionFilterTestAccess::canTrackAutomaticVolume(
			singleFilter) && passed;
		context.isCapture = true;
		singleFilter.setRuntimeContext(context);
		passed = !LoudnessCorrectionFilterTestAccess::canTrackAutomaticVolume(
			singleFilter) && passed;

		printf("Loudness runtime context: %s\n", passed ? "passed" : "failed");
		return passed;
	}

	bool runLoudnessOfflineAnalysisTests()
	{
		const unsigned sampleRate = 48000;
		const unsigned frameCount = 65536;
		auto renderFirstWindow = [=](
			bool state,
			float referenceOffset,
			bool manualVolume,
			vector<double>& output)
		{
			LoudnessCorrectionFilter::FilterParameters parameters;
			parameters.state = state;
			parameters.referenceLevel = 80.0f;
			parameters.referenceOffset = referenceOffset;
			parameters.attenuation = 1.0f;
			parameters.binding =
				LoudnessCorrectionFilter::FilterParameters::BINDING_SINGLE;
			parameters.useManualVolume = manualVolume;
			parameters.manualVolume = 0.0f;

			LoudnessCorrectionFilter filter(parameters);
			FilterRuntimeContext context;
			context.offlineAnalysis = true;
			filter.setRuntimeContext(context);
			vector<wstring> channels(1, L"C");
			filter.initialize(static_cast<float>(sampleRate), frameCount, channels);

			vector<double> input(frameCount, 0.0);
			input[0] = 1.0;
			output.assign(frameCount, 0.0);
			double* inputChannels[] = { input.data() };
			double* outputChannels[] = { output.data() };
			filter.process(outputChannels, inputChannels, frameCount);
			return input;
		};

		vector<double> offset0;
		vector<double> offset40;
		vector<double> disabled;
		vector<double> unavailableAutomatic;
		vector<double> input = renderFirstWindow(true, 0.0f, true, offset0);
		(void)renderFirstWindow(true, 40.0f, true, offset40);
		(void)renderFirstWindow(false, 40.0f, true, disabled);
		(void)renderFirstWindow(true, 40.0f, false, unavailableAutomatic);

		double maximumOffsetDifference = 0.0;
		double maximumDisabledError = 0.0;
		double maximumUnavailableError = 0.0;
		bool finite = true;
		for (unsigned frame = 0; frame < frameCount; ++frame)
		{
			finite = finite && std::isfinite(offset0[frame]) &&
				std::isfinite(offset40[frame]) && std::isfinite(disabled[frame]) &&
				std::isfinite(unavailableAutomatic[frame]);
			maximumOffsetDifference = (std::max)(maximumOffsetDifference,
				std::abs(offset0[frame] - offset40[frame]));
			maximumDisabledError = (std::max)(maximumDisabledError,
				std::abs(disabled[frame] - input[frame]));
			maximumUnavailableError = (std::max)(maximumUnavailableError,
				std::abs(unavailableAutomatic[frame] - input[frame]));
		}

		bool passed = finite && maximumOffsetDifference > 1.0e-6 &&
			maximumDisabledError <= 1.0e-12 &&
			maximumUnavailableError <= 1.0e-12;
		printf(
			"Loudness offline first window: offset delta %.9g, State0 error %.9g, unavailable error %.9g, %s\n",
			maximumOffsetDifference,
			maximumDisabledError,
			maximumUnavailableError,
			passed ? "passed" : "failed");
		return passed;
	}

	bool runFilterEngineDeviceInfoReuseTests()
	{
		FilterEngine engine;
		engine.setDeviceInfo(
			true, false, L"old-device", L"old-connection", L"old-guid", L"old-string");
		bool passed = engine.isDeviceInfoKnown() && engine.isCapture() &&
			!engine.isPostMixInstalled() && engine.getDeviceName() == L"old-device" &&
			engine.getConnectionName() == L"old-connection" &&
			engine.getDeviceGuid() == L"old-guid" &&
			engine.getDeviceString() == L"old-string";

		engine.clearDeviceInfo();
		passed = !engine.isDeviceInfoKnown() && !engine.isCapture() &&
			engine.isPostMixInstalled() && engine.getDeviceName().empty() &&
			engine.getConnectionName().empty() && engine.getDeviceGuid().empty() &&
			engine.getDeviceString().empty() && passed;

		printf("FilterEngine device-info reuse: %s\n", passed ? "passed" : "failed");
		return passed;
	}

	bool runLoudnessAnchorFitCase(unsigned sampleRate)
	{
		LoudnessCorrectionFilter::FilterParameters parameters;
		parameters.state = true;
		parameters.referenceLevel = 80.0f;
		parameters.referenceOffset = 0.0f;
		parameters.attenuation = 1.0f;
		parameters.useManualVolume = true;
		parameters.manualVolume = -40.0f;

		LoudnessCorrectionFilter filter(parameters);
		vector<wstring> channels(1, L"C");
		filter.initialize(static_cast<float>(sampleRate), 256, channels);
		vector<double> gains;
		double outputGainLinear = 1.0;
		LoudnessCorrectionFilterTestAccess::calculateTransfer(
			filter, parameters.manualVolume, gains, outputGainLinear);

		double maximumAnchorError = 0.0;
		for (size_t band = 0;
			band < LoudnessCorrectionFilterTestAccess::activeBandCount(filter);
			++band)
		{
			double frequency =
				LoudnessProfile::LOUDNESS_PROFILE_TABLE[band].frequency;
			double target = LoudnessProfile::computeContourDelta(
				40.0, 80.0, band);
			double actual =
				LoudnessCorrectionFilterTestAccess::rawCorrectionResponseDb(
					filter, gains, frequency);
			maximumAnchorError = (std::max)(maximumAnchorError,
				std::abs(actual - target));
		}

		printf("Loudness raw anchor fit %u Hz: maximum error %.9f dB\n",
			sampleRate, maximumAnchorError);
		if (!std::isfinite(outputGainLinear) || maximumAnchorError > 0.02)
		{
			fprintf(stderr, "Raw 29-point correction anchor fit regressed.\n");
			return false;
		}
		return true;
	}

	bool runLoudnessGuardedTransferCase(
		const char* name,
		unsigned sampleRate,
		double referenceLevel,
		double volume)
	{
		LoudnessCorrectionFilter::FilterParameters parameters;
		parameters.state = true;
		parameters.referenceLevel = static_cast<float>(referenceLevel);
		parameters.referenceOffset = 0.0f;
		parameters.attenuation = 1.0f;
		parameters.useManualVolume = true;
		parameters.manualVolume = static_cast<float>(volume);

		LoudnessCorrectionFilter filter(parameters);
		vector<wstring> channels(1, L"C");
		filter.initialize(static_cast<float>(sampleRate), 256, channels);
		vector<double> gains;
		double outputGainLinear = 1.0;
		LoudnessCorrectionFilterTestAccess::calculateTransfer(
			filter, volume, gains, outputGainLinear);

		const double subsonicFrequencies[] = { 1.0, 5.0, 10.0, 15.0, 19.0 };
		double maximumSubsonicError = 0.0;
		for (double frequency : subsonicFrequencies)
		{
			double response = LoudnessCorrectionFilterTestAccess::guardedResponseDb(
				filter, gains, outputGainLinear, frequency);
			if (!std::isfinite(response))
				return false;
			maximumSubsonicError = (std::max)(maximumSubsonicError,
				std::abs(response));
		}
		double maximumResponse =
			LoudnessCorrectionFilterTestAccess::maximumGuardedResponseDb(
				filter, gains, outputGainLinear);
		double rawMaximumResponse =
			LoudnessCorrectionFilterTestAccess::maximumRawCorrectionResponseDb(
				filter, gains);
		double expectedOutputGain = rawMaximumResponse <= 0.0 ? 1.0 :
			std::pow(10.0, -(rawMaximumResponse + 1.0) / 20.0);
		const double probeFrequencies[] = { 31.5, 80.0, 1000.0 };
		double probeResponses[3] = {};
		double probeCorrectionDeltas[3] = {};
		for (size_t index = 0; index < 3; ++index)
		{
			probeResponses[index] =
				LoudnessCorrectionFilterTestAccess::guardedResponseDb(
					filter, gains, outputGainLinear, probeFrequencies[index]);
			double lowpassOnly =
				LoudnessCorrectionFilterTestAccess::guardedResponseDb(
					filter, gains, 0.0, probeFrequencies[index]);
			probeCorrectionDeltas[index] =
				std::abs(probeResponses[index] - lowpassOnly);
		}
		printf(
			"Loudness guarded transfer %s: gain %.12f expected %.12f "
			"subsonic error %.9f dB peak %.9f dB probes %.6f/%.6f/%.6f dB\n",
			name, outputGainLinear, expectedOutputGain, maximumSubsonicError,
			maximumResponse,
			probeResponses[0], probeResponses[1], probeResponses[2]);
		if (!std::isfinite(outputGainLinear) || outputGainLinear <= 0.0 ||
			outputGainLinear > 1.0 ||
			std::abs(outputGainLinear - expectedOutputGain) >
				1.0e-9 * (std::max)(1.0, expectedOutputGain) ||
			probeCorrectionDeltas[0] <= 1.0e-3 ||
			probeCorrectionDeltas[1] <= 1.0e-3 ||
			probeCorrectionDeltas[2] <= 1.0e-3 ||
			maximumSubsonicError > 0.01 || maximumResponse > 1.0e-6)
		{
			fprintf(stderr,
				"%s violated subsonic unity or full-transfer headroom.\n", name);
			return false;
		}
		return true;
	}

	bool runLoudnessIdentityCase(
		const char* name,
		bool state,
		float attenuation,
		float volume)
	{
		const unsigned sampleRate = 48000;
		const unsigned batchSize = 256;
		LoudnessCorrectionFilter::FilterParameters parameters;
		parameters.state = state;
		parameters.referenceLevel = 80.0f;
		parameters.referenceOffset = 0.0f;
		parameters.attenuation = attenuation;
		parameters.useManualVolume = true;
		parameters.manualVolume = volume;

		LoudnessCorrectionFilter filter(parameters);
		vector<wstring> channels(1, L"C");
		filter.initialize(static_cast<float>(sampleRate), batchSize, channels);
		double inputStorage[batchSize];
		double outputStorage[batchSize];
		double* inputChannels[] = { inputStorage };
		double* outputChannels[] = { outputStorage };
		for (unsigned index = 0; index < batchSize; ++index)
		{
			inputStorage[index] =
				static_cast<double>(static_cast<int>(index % 37) - 18) / 19.0;
			outputStorage[index] = 0.0;
		}
		filter.process(outputChannels, inputChannels, batchSize);
		for (unsigned index = 0; index < batchSize; ++index)
		{
			if (outputStorage[index] != inputStorage[index])
			{
				fprintf(stderr, "%s was not bit-transparent.\n", name);
				return false;
			}
		}
		printf("Loudness identity %s: bit-transparent\n", name);
		return true;
	}

	bool runLoudnessSubsonicSineCase(double frequency)
	{
		const unsigned sampleRate = 48000;
		const unsigned batchSize = 256;
		const unsigned totalFrames = sampleRate * 5;
		const unsigned measureFrom = sampleRate * 4;
		LoudnessCorrectionFilter::FilterParameters parameters;
		parameters.state = true;
		parameters.referenceLevel = 100.0f;
		parameters.referenceOffset = 0.0f;
		parameters.attenuation = 1.0f;
		parameters.useManualVolume = true;
		parameters.manualVolume = -100.0f;

		LoudnessCorrectionFilter filter(parameters);
		vector<wstring> channels(1, L"C");
		filter.initialize(static_cast<float>(sampleRate), batchSize, channels);
		if (!LoudnessCorrectionFilterTestAccess::crossoverPrewarmActive(filter) ||
			LoudnessCorrectionFilterTestAccess::crossoverPrewarmLength(filter) !=
				sampleRate)
		{
			fprintf(stderr, "Initial LR28 prewarm timing regressed.\n");
			return false;
		}
		double inputStorage[batchSize];
		double outputStorage[batchSize];
		double* inputChannels[] = { inputStorage };
		double* outputChannels[] = { outputStorage };
		double inputPower = 0.0;
		double outputPower = 0.0;
		double maximumOutput = 0.0;
		unsigned frame = 0;
		while (frame < totalFrames)
		{
			unsigned frameCount = (std::min)(batchSize, totalFrames - frame);
			for (unsigned index = 0; index < frameCount; ++index)
			{
				double time = static_cast<double>(frame + index) /
					static_cast<double>(sampleRate);
				inputStorage[index] = std::sin(2.0 * M_PI * frequency * time);
				outputStorage[index] = 0.0;
			}
			filter.process(outputChannels, inputChannels, frameCount);
			for (unsigned index = 0; index < frameCount; ++index)
			{
				double outputSample = outputStorage[index];
				if (!std::isfinite(outputSample))
					return false;
				maximumOutput = (std::max)(maximumOutput, std::abs(outputSample));
				if (frame + index >= measureFrom)
				{
					inputPower += inputStorage[index] * inputStorage[index];
					outputPower += outputSample * outputSample;
				}
			}
			frame += frameCount;
		}

		double responseDb = 10.0 * std::log10(outputPower / inputPower);
		printf(
			"Loudness subsonic sine %.1f Hz: response %.9f dB maximum %.9f\n",
			frequency, responseDb, maximumOutput);
		if (std::abs(responseDb) > 0.01 || maximumOutput > 1.000001)
		{
			fprintf(stderr,
				"%.1f Hz end-to-end response violated unity or clipped.\n",
				frequency);
			return false;
		}
		return true;
	}

	bool runLoudnessTransitionCase(
		const char* name,
		unsigned sampleRate,
		double frequency,
		double initialVolume,
		double targetVolume,
		double phase)
	{
		const unsigned batchSize = 256;
		const unsigned switchFrame = sampleRate * 3;
		const unsigned totalFrames = sampleRate * 5;
		LoudnessCorrectionFilter::FilterParameters parameters;
		parameters.state = true;
		parameters.referenceLevel = 100.0f;
		parameters.referenceOffset = 0.0f;
		parameters.attenuation = 1.0f;
		parameters.useManualVolume = true;
		parameters.manualVolume = static_cast<float>(initialVolume);

		LoudnessCorrectionFilter filter(parameters);
		vector<wstring> channels(1, L"C");
		filter.initialize(static_cast<float>(sampleRate), batchSize, channels);

		double inputStorage[batchSize];
		double outputStorage[batchSize];
		double* inputChannels[] = { inputStorage };
		double* outputChannels[] = { outputStorage };
		double maximumOutput = 0.0;
		unsigned frame = 0;
		while (frame < totalFrames)
		{
			if (frame == switchFrame)
			{
				LoudnessCorrectionFilterTestAccess::publishVolumeUpdate(
					filter, targetVolume);
			}

			unsigned frameCount = (std::min)(batchSize, totalFrames - frame);
			if (frame < switchFrame && frame + frameCount > switchFrame)
				frameCount = switchFrame - frame;

			for (unsigned index = 0; index < frameCount; ++index)
			{
				double time = static_cast<double>(frame + index) /
					static_cast<double>(sampleRate);
				inputStorage[index] =
					std::sin(2.0 * M_PI * frequency * time + phase);
				outputStorage[index] = 0.0;
			}
			filter.process(outputChannels, inputChannels, frameCount);
			for (unsigned index = 0; index < frameCount; ++index)
			{
				double magnitude = std::abs(outputStorage[index]);
				if (!std::isfinite(magnitude))
				{
					fprintf(stderr, "%s produced a non-finite sample.\n", name);
					return false;
				}
				maximumOutput = (std::max)(maximumOutput, magnitude);
			}
			frame += frameCount;
		}

		printf("Loudness transition %s: maximum output %.9f\n", name, maximumOutput);
		if (maximumOutput > 1.000001)
		{
			fprintf(stderr, "%s exceeded full scale.\n", name);
			return false;
		}
		return true;
	}





	bool runLoudnessAdaptiveHandoffSweep()
	{
		const unsigned sampleRates[] = {
			8000, 44100, 48000, 96000, 192000, 384000
		};
		const double phases[] = { 0.0, M_PI / 4.0, M_PI / 2.0, 3.0 * M_PI / 4.0 };
		bool passed = true;
		for (unsigned sampleRate : sampleRates)
		{
			LoudnessCorrectionFilter::FilterParameters parameters;
			parameters.state = true;
			parameters.referenceLevel = 100.0f;
			parameters.attenuation = 1.0f;
			parameters.useManualVolume = true;
			parameters.manualVolume = 0.0f;
			LoudnessCorrectionFilter filter(parameters);
			vector<wstring> channels(1, L"C");
			filter.initialize(static_cast<float>(sampleRate), 64, channels);

			size_t caseCount = 0;
			double maximumSecondDifferenceRatio = 0.0;
			double maximumWaitSeconds = 0.0;
			auto checkFrequency = [&](double frequency)
			{
				complex<double> identityResponse =
					LoudnessCorrectionFilterTestAccess::crossoverIdentityResponse(
						filter, frequency);
				double omega = 2.0 * M_PI * frequency /
					static_cast<double>(sampleRate);
				unsigned maximumWait = static_cast<unsigned>(std::ceil(
					static_cast<double>(sampleRate) / (2.0 * frequency))) + 8;
				for (double phase : phases)
				{
					bool switched = false;
					double previousRaw = 0.0;
					double previousIdentity = 0.0;
					for (unsigned sample = 0; sample <= maximumWait; ++sample)
					{
						double angle = omega * static_cast<double>(sample) + phase;
						double raw = std::sin(angle);
						double identity = std::imag(
							identityResponse * std::exp(complex<double>(0.0, angle)));
						if (sample > 0)
						{
							double handoffStep = 0.0;
							double naturalStep = 0.0;
							if (LoudnessCorrectionFilterTestAccess::isSafeCrossoverHandoff(
								previousRaw,
								raw,
								previousIdentity,
								identity,
								handoffStep,
								naturalStep))
							{
								double nextAngle = angle + omega;
								double nextIdentity = std::imag(identityResponse *
									std::exp(complex<double>(0.0, nextAngle)));
								double secondDifference = std::abs(
									nextIdentity - 2.0 * identity + previousRaw);
								double firstDifferenceBound =
									2.0 * std::sin(M_PI * frequency /
										static_cast<double>(sampleRate));
								double secondDifferenceBound =
									2.0 * firstDifferenceBound + 1.0e-9;
								maximumSecondDifferenceRatio = (std::max)(
									maximumSecondDifferenceRatio,
									secondDifference /
										(std::max)(firstDifferenceBound, 1.0e-15));
								maximumWaitSeconds = (std::max)(
									maximumWaitSeconds,
									static_cast<double>(sample) /
										static_cast<double>(sampleRate));
								if (handoffStep > naturalStep + 1.0e-12 ||
									secondDifference > secondDifferenceBound)
								{
									fprintf(stderr,
										"Adaptive handoff bounds failed at %u Hz/%.6f Hz.\n",
										sampleRate, frequency);
									passed = false;
								}
								switched = true;
								break;
							}
						}
						previousRaw = raw;
						previousIdentity = identity;
					}
					if (!switched)
					{
						fprintf(stderr,
							"Adaptive handoff did not complete at %u Hz/%.6f Hz.\n",
							sampleRate, frequency);
						passed = false;
					}
					++caseCount;
				}
			};

			unsigned upperFrequency = (std::min)(
				4000u,
				static_cast<unsigned>(std::floor(0.499 * sampleRate)));
			for (unsigned frequency = 1; frequency <= upperFrequency; ++frequency)
				checkFrequency(static_cast<double>(frequency));
			checkFrequency(4.38);
			checkFrequency(12.7711);
			printf(
				"Loudness adaptive handoff sweep %u Hz: %zu cases, "
				"max wait %.6f s, max second-difference/B1 %.6f\n",
				sampleRate, caseCount, maximumWaitSeconds,
				maximumSecondDifferenceRatio);
		}
		return passed;
	}

	bool runLoudnessExactHandoffCase(
		unsigned sampleRate,
		double frequency,
		double phase)
	{
		const unsigned batchSize = 64;
		LoudnessCorrectionFilter::FilterParameters parameters;
		parameters.state = true;
		parameters.referenceLevel = 100.0f;
		parameters.attenuation = 1.0f;
		parameters.useManualVolume = true;
		parameters.manualVolume = 0.0f;
		LoudnessCorrectionFilter filter(parameters);
		vector<wstring> channels(1, L"C");
		filter.initialize(static_cast<float>(sampleRate), batchSize, channels);

		double inputStorage[batchSize];
		double outputStorage[batchSize];
		double* inputChannels[] = { inputStorage };
		double* outputChannels[] = { outputStorage };
		double previousOutput = 0.0;
		double previousPreviousOutput = 0.0;
		bool havePrevious = false;
		bool havePreviousPrevious = false;
		double maximumSecondDifference = 0.0;
		unsigned frame = 0;
		unsigned maximumFrame =
			LoudnessCorrectionFilterTestAccess::crossoverPrewarmLength(filter) +
			static_cast<unsigned>(std::ceil(
				static_cast<double>(sampleRate) / (2.0 * frequency))) +
			4 * batchSize;
		while (!LoudnessCorrectionFilterTestAccess::crossoverDomainReady(filter) &&
			frame < maximumFrame)
		{
			unsigned frameCount = (std::min)(batchSize, maximumFrame - frame);
			for (unsigned index = 0; index < frameCount; ++index)
			{
				double time = static_cast<double>(frame + index) /
					static_cast<double>(sampleRate);
				inputStorage[index] =
					std::sin(2.0 * M_PI * frequency * time + phase);
				outputStorage[index] = 0.0;
			}
			filter.process(outputChannels, inputChannels, frameCount);
			for (unsigned index = 0; index < frameCount; ++index)
			{
				double sample = outputStorage[index];
				if (!std::isfinite(sample))
					return false;
				if (havePreviousPrevious)
				{
					maximumSecondDifference = (std::max)(
						maximumSecondDifference,
						std::abs(sample - 2.0 * previousOutput +
							previousPreviousOutput));
				}
				previousPreviousOutput = previousOutput;
				previousOutput = sample;
				havePreviousPrevious = havePrevious;
				havePrevious = true;
			}
			frame += frameCount;
		}

		double firstDifferenceBound = 2.0 * std::sin(
			M_PI * frequency / static_cast<double>(sampleRate));
		double secondDifferenceBound = 2.0 * firstDifferenceBound + 1.0e-8;
		double maximumHandoffStep =
			LoudnessCorrectionFilterTestAccess::maximumCrossoverHandoffStep(filter);
		bool passed =
			LoudnessCorrectionFilterTestAccess::crossoverDomainReady(filter) &&
			maximumHandoffStep <= firstDifferenceBound + 1.0e-6 &&
			maximumSecondDifference <= secondDifferenceBound;
		if (!passed)
		{
			fprintf(stderr,
				"Exact adaptive handoff failed at %u Hz/%.6f Hz/phase %.3f: "
				"frame %u, step %.9g, second %.9g.\n",
				sampleRate, frequency, phase, frame,
				maximumHandoffStep, maximumSecondDifference);
		}
		else
		{
			printf(
				"Loudness exact handoff %u Hz/%.6f Hz/phase %.3f: "
				"step %.9g, second %.9g\n",
				sampleRate, frequency, phase,
				maximumHandoffStep, maximumSecondDifference);
		}
		return passed;
	}

	enum LoudnessFailureStage
	{
		FAILURE_AT_SETTLED,
		FAILURE_AT_WARMUP,
		FAILURE_AT_CROSSFADE
	};

	bool runLoudnessCommonDomainCase(
		const char* name,
		unsigned sampleRate,
		double frequency,
		unsigned earlyUpdateDelayMs,
		LoudnessFailureStage failureStage,
		bool exerciseIdentityUpdates,
		bool repeatFailureDuringRecovery)
	{
		const unsigned batchSize = 64;
		const double minimumEnvelope = std::pow(10.0, -0.01 / 20.0);
		LoudnessCorrectionFilter::FilterParameters parameters;
		parameters.state = true;
		parameters.referenceLevel = 100.0f;
		parameters.referenceOffset = 0.0f;
		parameters.attenuation = 1.0f;
		parameters.useManualVolume = true;
		parameters.manualVolume = 0.0f;
		LoudnessCorrectionFilter filter(parameters);
		vector<wstring> channels{ L"I", L"Q" };
		filter.initialize(static_cast<float>(sampleRate), batchSize, channels);

		double inputI[batchSize];
		double inputQ[batchSize];
		double outputI[batchSize];
		double outputQ[batchSize];
		double* inputChannels[] = { inputI, inputQ };
		double* outputChannels[] = { outputI, outputQ };
		unsigned absoluteFrame = 0;
		double measuredMinimumEnvelope =
			std::numeric_limits<double>::infinity();
		double measuredMaximumEnvelope = 0.0;
		double maximumBypassFirstDifference = 0.0;
		double previousOutputs[2] = {};
		bool havePreviousOutputs = false;
		bool trackBypassDifferences = false;
		bool bypassFadeWasActive = false;
		unsigned bypassFadeStartCount = 0;

		auto processFrames = [&](unsigned requestedFrames)
		{
			while (requestedFrames > 0)
			{
				unsigned frameCount = (std::min)(batchSize, requestedFrames);
				bool measureCommonDomain =
					LoudnessCorrectionFilterTestAccess::crossoverDomainReady(filter);
				for (unsigned index = 0; index < frameCount; ++index)
				{
					double time = static_cast<double>(absoluteFrame + index) /
						static_cast<double>(sampleRate);
					double angle = 2.0 * M_PI * frequency * time;
					inputI[index] = std::cos(angle);
					inputQ[index] = std::sin(angle);
					outputI[index] = 0.0;
					outputQ[index] = 0.0;
				}
				filter.process(outputChannels, inputChannels, frameCount);
				for (unsigned index = 0; index < frameCount; ++index)
				{
					if (!std::isfinite(outputI[index]) ||
						!std::isfinite(outputQ[index]))
					{
						return false;
					}
					if (measureCommonDomain)
					{
						double envelope = std::hypot(
							outputI[index], outputQ[index]);
						measuredMinimumEnvelope = (std::min)(
							measuredMinimumEnvelope, envelope);
						measuredMaximumEnvelope = (std::max)(
							measuredMaximumEnvelope, envelope);
					}
					if (trackBypassDifferences && havePreviousOutputs)
					{
						maximumBypassFirstDifference = (std::max)(
							maximumBypassFirstDifference,
							(std::max)(
								std::abs(outputI[index] - previousOutputs[0]),
								std::abs(outputQ[index] - previousOutputs[1])));
					}
					previousOutputs[0] = outputI[index];
					previousOutputs[1] = outputQ[index];
					havePreviousOutputs = true;
				}
				absoluteFrame += frameCount;
				requestedFrames -= frameCount;
				bool bypassFadeIsActive =
					LoudnessCorrectionFilterTestAccess::bypassFadeActive(filter);
				if (bypassFadeIsActive && !bypassFadeWasActive)
					++bypassFadeStartCount;
				bypassFadeWasActive = bypassFadeIsActive;
				if (trackBypassDifferences &&
					!LoudnessCorrectionFilterTestAccess::bypassFadeActive(filter) &&
					LoudnessCorrectionFilterTestAccess::runtimeBypass(filter))
				{
					trackBypassDifferences = false;
				}
			}
			return true;
		};

		auto waitUntil = [&](auto predicate, unsigned maximumFrames)
		{
			unsigned startFrame = absoluteFrame;
			while (!predicate() && absoluteFrame - startFrame < maximumFrames)
			{
				unsigned remaining =
					maximumFrames - (absoluteFrame - startFrame);
				if (!processFrames((std::min)(batchSize, remaining)))
					return false;
			}
			return predicate();
		};

		unsigned updateDelayFrames =
			sampleRate * earlyUpdateDelayMs / 1000;
		if (!processFrames(updateDelayFrames))
			return false;
		LoudnessCorrectionFilterTestAccess::publishVolumeUpdate(filter, -100.0);

		unsigned maximumHandoffFrame =
			LoudnessCorrectionFilterTestAccess::crossoverPrewarmLength(filter) +
			static_cast<unsigned>(std::ceil(
				static_cast<double>(sampleRate) / (2.0 * frequency))) +
			4 * batchSize;
		if (absoluteFrame >= maximumHandoffFrame ||
			!waitUntil([&]()
			{
				return LoudnessCorrectionFilterTestAccess::crossoverDomainReady(
					filter);
			}, maximumHandoffFrame - absoluteFrame))
		{
			fprintf(stderr, "%s did not enter the common A domain.\n", name);
			return false;
		}

		auto waitForNonIdentity = [&]()
		{
			return waitUntil([&]()
			{
				return LoudnessCorrectionFilterTestAccess::settledOnNonIdentityBank(
					filter);
			}, sampleRate);
		};
		if (exerciseIdentityUpdates || failureStage == FAILURE_AT_SETTLED)
		{
			if (!waitForNonIdentity())
				return false;
		}
		if (exerciseIdentityUpdates)
		{
			LoudnessCorrectionFilterTestAccess::publishVolumeUpdate(filter, 0.0);
			if (!waitUntil([&]()
			{
				return LoudnessCorrectionFilterTestAccess::settledOnIdentityBank(filter);
			}, sampleRate))
			{
				return false;
			}
			LoudnessCorrectionFilterTestAccess::publishVolumeUpdate(filter, -100.0);
			if (!waitForNonIdentity())
				return false;
		}
		else if (failureStage == FAILURE_AT_WARMUP)
		{
			if (!waitUntil([&]()
			{
				return LoudnessCorrectionFilterTestAccess::warmupActive(filter);
			}, sampleRate / 2))
			{
				return false;
			}
			while (LoudnessCorrectionFilterTestAccess::warmupActive(filter) &&
				LoudnessCorrectionFilterTestAccess::warmupPosition(filter) <
					LoudnessCorrectionFilterTestAccess::warmupLength(filter) / 2)
			{
				if (!processFrames(batchSize))
					return false;
			}
		}
		else if (failureStage == FAILURE_AT_CROSSFADE)
		{
			if (!waitUntil([&]()
			{
				return LoudnessCorrectionFilterTestAccess::crossfadeActive(filter);
			}, sampleRate))
			{
				return false;
			}
			while (LoudnessCorrectionFilterTestAccess::crossfadeActive(filter) &&
				LoudnessCorrectionFilterTestAccess::crossfadePosition(filter) <
					LoudnessCorrectionFilterTestAccess::crossfadeLength(filter) / 2)
			{
				if (!processFrames(batchSize))
					return false;
			}
		}

		LoudnessCorrectionFilterTestAccess::beginRuntimeBypass(filter);
		trackBypassDifferences = true;
		unsigned recoveryDelayFrames =
			sampleRate * earlyUpdateDelayMs / 1000;
		if (recoveryDelayFrames > 0 && !processFrames(recoveryDelayFrames))
			return false;
		LoudnessCorrectionFilterTestAccess::publishRuntimeRecovery(filter, -100.0);

		if (repeatFailureDuringRecovery)
		{
			if (!waitUntil([&]()
			{
				return !LoudnessCorrectionFilterTestAccess::runtimeBypass(filter) &&
					LoudnessCorrectionFilterTestAccess::warmupActive(filter);
			}, sampleRate))
			{
				return false;
			}
			LoudnessCorrectionFilterTestAccess::beginRuntimeBypass(filter);
			LoudnessCorrectionFilterTestAccess::publishRuntimeRecovery(filter, -100.0);
			trackBypassDifferences = true;
		}
		if (!waitForNonIdentity())
			return false;

		double firstDifferenceBound =
			2.0 * std::sin(M_PI * frequency /
				static_cast<double>(sampleRate)) +
			2.0 / static_cast<double>((std::max)(
				2u,
				LoudnessCorrectionFilterTestAccess::bypassFadeLength(filter)) - 1) +
			2.0 / static_cast<double>((std::max)(
				2u,
				LoudnessCorrectionFilterTestAccess::crossfadeLength(filter)) - 1) +
			1.0e-6;
		bool passed = std::isfinite(measuredMinimumEnvelope) &&
			measuredMinimumEnvelope >= minimumEnvelope &&
			measuredMaximumEnvelope <= 1.000001 &&
			maximumBypassFirstDifference <= firstDifferenceBound &&
			LoudnessCorrectionFilterTestAccess::bypassFadeLength(filter) ==
				static_cast<unsigned>(std::lround(sampleRate * 0.01)) &&
			bypassFadeStartCount >= (repeatFailureDuringRecovery ? 2u : 1u);
		printf(
			"Loudness common-A %s: envelope %.9f..%.9f, bypass step %.9f "
			"(bound %.9f), handoff %.6f s\n",
			name, measuredMinimumEnvelope, measuredMaximumEnvelope,
			maximumBypassFirstDifference, firstDifferenceBound,
			static_cast<double>(absoluteFrame) / static_cast<double>(sampleRate));
		if (!passed)
		{
			fprintf(stderr,
				"%s violated the common-A envelope, headroom, or bypass-fade contract.\n",
				name);
		}
		return passed;
	}

	bool runLoudnessPartialHandoffFailureCase(unsigned sampleRate)
	{
		const unsigned batchSize = 64;
		const double frequency = 1.0;
		LoudnessCorrectionFilter::FilterParameters parameters;
		parameters.state = true;
		parameters.referenceLevel = 100.0f;
		parameters.referenceOffset = 0.0f;
		parameters.attenuation = 1.0f;
		parameters.useManualVolume = true;
		parameters.manualVolume = -100.0f;
		LoudnessCorrectionFilter filter(parameters);
		vector<wstring> channels{ L"I", L"Q" };
		filter.initialize(static_cast<float>(sampleRate), batchSize, channels);

		double inputI[batchSize];
		double inputQ[batchSize];
		double outputI[batchSize];
		double outputQ[batchSize];
		double* inputChannels[] = { inputI, inputQ };
		double* outputChannels[] = { outputI, outputQ };
		unsigned absoluteFrame = 0;
		auto processBlock = [&]()
		{
			for (unsigned index = 0; index < batchSize; ++index)
			{
				double time = static_cast<double>(absoluteFrame + index) /
					static_cast<double>(sampleRate);
				double angle = 2.0 * M_PI * frequency * time;
				inputI[index] = std::cos(angle);
				inputQ[index] = std::sin(angle);
				outputI[index] = 0.0;
				outputQ[index] = 0.0;
			}
			filter.process(outputChannels, inputChannels, batchSize);
			absoluteFrame += batchSize;
			for (unsigned index = 0; index < batchSize; ++index)
			{
				if (!std::isfinite(outputI[index]) ||
					!std::isfinite(outputQ[index]))
				{
					return false;
				}
			}
			return true;
		};

		bool observedPartialHandoff = false;
		unsigned partialDeadline =
			LoudnessCorrectionFilterTestAccess::crossoverPrewarmLength(filter) +
			sampleRate;
		while (absoluteFrame < partialDeadline)
		{
			if (!processBlock())
				return false;
			bool channel0 =
				LoudnessCorrectionFilterTestAccess::crossoverChannelInDomain(filter, 0);
			bool channel1 =
				LoudnessCorrectionFilterTestAccess::crossoverChannelInDomain(filter, 1);
			if (channel0 != channel1)
			{
				observedPartialHandoff = true;
				break;
			}
		}
		if (!observedPartialHandoff)
		{
			fprintf(stderr,
				"Partial-channel handoff was not observed at %u Hz.\n", sampleRate);
			return false;
		}

		LoudnessCorrectionFilterTestAccess::beginRuntimeBypass(filter);
		unsigned readyDeadline = absoluteFrame + sampleRate;
		while (!LoudnessCorrectionFilterTestAccess::crossoverDomainReady(filter) &&
			absoluteFrame < readyDeadline)
		{
			if (!processBlock() ||
				LoudnessCorrectionFilterTestAccess::warmupActive(filter) ||
				LoudnessCorrectionFilterTestAccess::crossfadeActive(filter) ||
				LoudnessCorrectionFilterTestAccess::bypassFadeActive(filter))
			{
				fprintf(stderr,
					"Partial-channel failure started an unsafe correction transition.\n");
				return false;
			}
		}
		if (!LoudnessCorrectionFilterTestAccess::crossoverDomainReady(filter))
			return false;

		double minimumEnvelope = std::numeric_limits<double>::infinity();
		double maximumEnvelope = 0.0;
		for (unsigned remaining = sampleRate / 4; remaining > 0;)
		{
			if (!processBlock())
				return false;
			for (unsigned index = 0; index < batchSize; ++index)
			{
				double envelope = std::hypot(outputI[index], outputQ[index]);
				minimumEnvelope = (std::min)(minimumEnvelope, envelope);
				maximumEnvelope = (std::max)(maximumEnvelope, envelope);
			}
			remaining = remaining > batchSize ? remaining - batchSize : 0;
		}

		bool passed =
			LoudnessCorrectionFilterTestAccess::holdingBypassOnIdentityBank(filter) &&
			minimumEnvelope >= std::pow(10.0, -0.01 / 20.0) &&
			maximumEnvelope <= 1.000001;
		printf(
			"Loudness partial handoff failure %u Hz: envelope %.9f..%.9f, %s\n",
			sampleRate, minimumEnvelope, maximumEnvelope,
			passed ? "passed" : "failed");
		return passed;
	}

	bool runLoudnessBlockProcessingCase(
		LoudnessCorrectionFilter::FilterParameters::EngineMode engine,
		bool inPlace)
	{
		const unsigned sampleRate = 48000;
		const unsigned channelCount = 2;
		const unsigned maximumFrameCount = 256;
		const unsigned storageFrameCount = maximumFrameCount + 1;
		const unsigned framePattern[] = {
			0, 1, 16, 64, maximumFrameCount - 1,
			maximumFrameCount, maximumFrameCount + 1
		};
		LoudnessCorrectionFilter::FilterParameters parameters;
		parameters.state = true;
		parameters.referenceLevel = 80.0f;
		parameters.referenceOffset = 0.0f;
		parameters.attenuation = 1.0f;
		parameters.useManualVolume = true;
		parameters.manualVolume = -38.0f;
		parameters.engine = engine;

		LoudnessCorrectionFilter blockFilter(parameters);
		LoudnessCorrectionFilter scalarFilter(parameters);
		FilterRuntimeContext context;
		context.offlineAnalysis = true;
		blockFilter.setRuntimeContext(context);
		scalarFilter.setRuntimeContext(context);
		vector<wstring> channels(channelCount, L"C");
		blockFilter.initialize(
			static_cast<float>(sampleRate), maximumFrameCount, channels);
		// A zero advertised capacity deliberately selects the existing scalar
		// fallback while leaving all filter coefficients and states unchanged.
		scalarFilter.initialize(static_cast<float>(sampleRate), 0, channels);

		vector<vector<double>> input(
			channelCount, vector<double>(storageFrameCount));
		vector<vector<double>> blockOutput(
			channelCount, vector<double>(storageFrameCount));
		vector<vector<double>> scalarOutput(
			channelCount, vector<double>(storageFrameCount));
		vector<double*> inputChannels(channelCount);
		vector<double*> blockInputChannels(channelCount);
		vector<double*> scalarInputChannels(channelCount);
		vector<double*> blockOutputChannels(channelCount);
		vector<double*> scalarOutputChannels(channelCount);
		double maximumError = 0.0;
		bool finite = true;
		unsigned samplePosition = 0;
		for (unsigned block = 0; block < 256; ++block)
		{
			const unsigned frameCount = framePattern[
				block % (sizeof(framePattern) / sizeof(framePattern[0]))];
			if (block == 64)
			{
				LoudnessCorrectionFilterTestAccess::publishVolumeUpdate(
					blockFilter, -52.0);
				LoudnessCorrectionFilterTestAccess::publishVolumeUpdate(
					scalarFilter, -52.0);
			}
			for (unsigned channel = 0; channel < channelCount; ++channel)
			{
				for (unsigned frame = 0; frame < frameCount; ++frame)
				{
					const double time = static_cast<double>(
						samplePosition + frame) / sampleRate;
					const double value =
						0.21 * sin(2.0 * M_PI * (37.0 + 311.0 * channel) * time) +
						0.13 * cos(2.0 * M_PI * (997.0 + 701.0 * channel) * time);
					input[channel][frame] = value;
					blockOutput[channel][frame] = value;
					scalarOutput[channel][frame] = value;
				}
				inputChannels[channel] = input[channel].data();
				blockOutputChannels[channel] = blockOutput[channel].data();
				scalarOutputChannels[channel] = scalarOutput[channel].data();
				blockInputChannels[channel] = inPlace ?
					blockOutput[channel].data() : input[channel].data();
				scalarInputChannels[channel] = inPlace ?
					scalarOutput[channel].data() : input[channel].data();
			}

			blockFilter.process(
				blockOutputChannels.data(), blockInputChannels.data(), frameCount);
			scalarFilter.process(
				scalarOutputChannels.data(), scalarInputChannels.data(), frameCount);
			for (unsigned channel = 0; channel < channelCount; ++channel)
			{
				for (unsigned frame = 0; frame < frameCount; ++frame)
				{
					finite = finite && isfinite(blockOutput[channel][frame]) &&
						isfinite(scalarOutput[channel][frame]);
					maximumError = (std::max)(maximumError, abs(
						blockOutput[channel][frame] - scalarOutput[channel][frame]));
				}
			}
			samplePosition += frameCount;
		}

		const bool passed = finite && maximumError <= 1.0e-12;
		printf(
			"Loudness %s mixed block/scalar %s: maximum error %.3g, %s\n",
			engine == LoudnessCorrectionFilter::FilterParameters::ENGINE_FAST ?
				"Fast" : "Full",
			inPlace ? "in-place" : "out-of-place",
			maximumError,
			passed ? "passed" : "failed");
		return passed;
	}

	bool runLoudnessResponseAggregationTests()
	{
		const unsigned sampleRates[] = { 8000, 48000, 384000 };
		const double volumes[] = { -100.0, -38.0, 0.0 };
		double maximumError = 0.0;
		bool finite = true;
		for (unsigned sampleRate : sampleRates)
		{
			for (double volume : volumes)
			{
				LoudnessCorrectionFilter::FilterParameters parameters;
				parameters.state = true;
				parameters.referenceLevel = 80.0f;
				parameters.referenceOffset = 0.0f;
				parameters.attenuation = 1.0f;
				parameters.useManualVolume = true;
				parameters.manualVolume = static_cast<float>(volume);
				LoudnessCorrectionFilter filter(parameters);
				FilterRuntimeContext context;
				context.offlineAnalysis = true;
				filter.setRuntimeContext(context);
				filter.initialize(
					static_cast<float>(sampleRate), 256, vector<wstring>(1, L"C"));
				vector<double> gains;
				double outputGainLinear = 1.0;
				LoudnessCorrectionFilterTestAccess::calculateTransfer(
					filter, volume, gains, outputGainLinear);

				const double minimumFrequency = 1.0;
				const double maximumFrequency = (std::min)(
					20000.0, 0.499 * static_cast<double>(sampleRate));
				const double logMinimum = log(minimumFrequency);
				const double logMaximum = log(maximumFrequency);
				for (unsigned point = 0; point < 1025; ++point)
				{
					const double frequency = exp(
						logMinimum + static_cast<double>(point) *
						(logMaximum - logMinimum) / 1024.0);
					const double legacy =
						LoudnessCorrectionFilterTestAccess::rawCorrectionResponseDb(
							filter, gains, frequency);
					const double aggregate =
						LoudnessCorrectionFilterTestAccess::aggregateRawCorrectionResponseDb(
							filter, gains, frequency);
					finite = finite && isfinite(legacy) && isfinite(aggregate);
					maximumError = (std::max)(
						maximumError, abs(legacy - aggregate));
				}
			}
		}

		const bool passed = finite && maximumError <= 1.0e-9;
		printf(
			"Loudness aggregate response: maximum legacy error %.3g dB, %s\n",
			maximumError,
			passed ? "passed" : "failed");
		return passed;
	}

	int runLoudnessTransitionTests()
	{
		bool passed = runLoudnessFormulaTests();
		passed = runLoudnessParameterCodecTests() && passed;
		passed = runLoudnessRuntimeContextTests() && passed;
		passed = runLoudnessOfflineAnalysisTests() && passed;
		passed = runFilterEngineDeviceInfoReuseTests() && passed;
		passed = runLoudnessCrossoverCoefficientTests() && passed;
		passed = runLoudnessResponseAggregationTests() && passed;
		for (LoudnessCorrectionFilter::FilterParameters::EngineMode engine : {
			LoudnessCorrectionFilter::FilterParameters::ENGINE_FULL,
			LoudnessCorrectionFilter::FilterParameters::ENGINE_FAST })
		{
			passed = runLoudnessBlockProcessingCase(engine, false) && passed;
			passed = runLoudnessBlockProcessingCase(engine, true) && passed;
		}
		passed = runLoudnessAnchorFitCase(48000) && passed;
		passed = runLoudnessAnchorFitCase(192000) && passed;
		passed = runLoudnessGuardedTransferCase(
			"8k-extreme", 8000, 100.0, -100.0) && passed;
		passed = runLoudnessGuardedTransferCase(
			"48k-extreme", 48000, 100.0, -100.0) && passed;
		passed = runLoudnessGuardedTransferCase(
			"192k-extreme", 192000, 100.0, -100.0) && passed;
		passed = runLoudnessGuardedTransferCase(
			"384k-extreme", 384000, 100.0, -100.0) && passed;
		passed = runLoudnessIdentityCase(
			"State0", false, 1.0f, -100.0f) && passed;
		passed = runLoudnessIdentityCase(
			"zero-attenuation", true, 0.0f, -100.0f) && passed;
		const double subsonicFrequencies[] = { 1.0, 5.0, 10.0, 15.0, 19.0 };
		for (double frequency : subsonicFrequencies)
			passed = runLoudnessSubsonicSineCase(frequency) && passed;
		passed = runLoudnessTransitionCase(
			"8k-minus100-to-0", 8000, 31.5, -100.0, 0.0, 0.0) && passed;
		passed = runLoudnessTransitionCase(
			"8k-0-to-minus100", 8000, 31.5, 0.0, -100.0, 0.0) && passed;
		passed = runLoudnessTransitionCase(
			"48k-inter-bin", 48000, 80.216, -40.0, 0.0, 0.0) && passed;

		passed = runLoudnessAdaptiveHandoffSweep() && passed;
		const unsigned handoffSampleRates[] = { 8000, 48000 };
		const double handoffPhases[] = { 0.0, M_PI / 2.0 };
		const double handoffCoreFrequencies[] = {
			1.0, 4.38, 5.0, 10.0, 12.5, 12.7711, 15.0, 19.0
		};
		for (unsigned sampleRate : handoffSampleRates)
		{
			for (double frequency : handoffCoreFrequencies)
			{
				for (double phase : handoffPhases)
				{
					passed = runLoudnessExactHandoffCase(
						sampleRate, frequency, phase) && passed;
				}
			}

			const double high8k[] = {
				80.0, 100.0, 125.0, 250.0, 400.0, 800.0, 1000.0, 3992.0
			};
			const double high48k[] = {
				160.0, 200.0, 400.0, 500.0, 1000.0, 2000.0, 4000.0
			};
			const double* highFrequencies =
				sampleRate == 8000 ? high8k : high48k;
			size_t highFrequencyCount = sampleRate == 8000 ?
				sizeof(high8k) / sizeof(high8k[0]) :
				sizeof(high48k) / sizeof(high48k[0]);
			for (size_t index = 0; index < highFrequencyCount; ++index)
			{
				for (double phase : handoffPhases)
				{
					passed = runLoudnessExactHandoffCase(
						sampleRate, highFrequencies[index], phase) && passed;
				}
			}
			passed = runLoudnessPartialHandoffFailureCase(sampleRate) && passed;
		}

		const unsigned commonDomainDelaysMs[] = { 0, 64 };
		for (unsigned sampleRate : handoffSampleRates)
		{
			for (double frequency : handoffCoreFrequencies)
			{
				for (unsigned delayMs : commonDomainDelaysMs)
				{
					char name[128];
					sprintf_s(name, "%uk-%.4fHz-delay-%ums-full",
						sampleRate / 1000, frequency, delayMs);
					bool repeatFailure = sampleRate == 48000 &&
						std::abs(frequency - 12.7711) < 1.0e-9 && delayMs == 0;
					passed = runLoudnessCommonDomainCase(
						name,
						sampleRate,
						frequency,
						delayMs,
						FAILURE_AT_SETTLED,
						true,
						repeatFailure) && passed;
				}
			}
			for (LoudnessFailureStage stage : {
				FAILURE_AT_WARMUP, FAILURE_AT_CROSSFADE })
			{
				char name[128];
				sprintf_s(name, "%uk-12.7711Hz-failure-%s",
					sampleRate / 1000,
					stage == FAILURE_AT_WARMUP ? "warmup" : "crossfade");
				passed = runLoudnessCommonDomainCase(
					name,
					sampleRate,
					12.7711,
					0,
					stage,
					false,
					false) && passed;
			}
		}

		return passed ? 0 : 1;
	}

	struct LoudnessPerformanceResult
	{
		double initializeMilliseconds;
		double updateMilliseconds;
		double processingNanosecondsPerSample;
		bool valid;
	};

	double median(vector<double> values)
	{
		sort(values.begin(), values.end());
		const size_t middle = values.size() / 2;
		if ((values.size() & 1) != 0)
			return values[middle];
		return 0.5 * (values[middle - 1] + values[middle]);
	}

	LoudnessPerformanceResult measureLoudnessPerformance(
		LoudnessCorrectionFilter::FilterParameters::EngineMode engine,
		bool useBlockPath,
		unsigned batchSize = 256)
	{
		const unsigned sampleRate = 48000;
		const unsigned channelCount = 2;
		const unsigned measuredFrameCount = 1024 * 1024;
		const unsigned blockCount = (std::max)(
			1u, measuredFrameCount / batchSize);
		const unsigned repeatCount = 7;
		vector<double> initializeTimes;
		vector<double> updateTimes;
		vector<double> processingTimes;
		volatile double outputChecksum = 0.0;
		bool valid = true;

		for (unsigned repeat = 0; repeat < repeatCount; ++repeat)
		{
			LoudnessCorrectionFilter::FilterParameters parameters;
			parameters.state = true;
			parameters.referenceLevel = 80.0f;
			parameters.referenceOffset = 0.0f;
			parameters.attenuation = 1.0f;
			parameters.useManualVolume = true;
			parameters.manualVolume = -38.0f;
			parameters.engine = engine;

			LoudnessCorrectionFilter filter(parameters);
			FilterRuntimeContext context;
			context.offlineAnalysis = true;
			filter.setRuntimeContext(context);
			vector<wstring> channels(channelCount, L"C");
			PrecisionTimer timer;
			timer.start();
			filter.initialize(
				static_cast<float>(sampleRate),
				useBlockPath ? batchSize : 0,
				channels);
			initializeTimes.push_back(timer.stop() * 1000.0);

			timer.start();
			LoudnessCorrectionFilterTestAccess::publishVolumeUpdate(
				filter, -42.0 + static_cast<double>(repeat));
			updateTimes.push_back(timer.stop() * 1000.0);

			vector<vector<double>> input(channelCount, vector<double>(batchSize));
			vector<vector<double>> output(channelCount, vector<double>(batchSize));
			vector<double*> inputChannels(channelCount);
			vector<double*> outputChannels(channelCount);
			for (unsigned channel = 0; channel < channelCount; ++channel)
			{
				inputChannels[channel] = input[channel].data();
				outputChannels[channel] = output[channel].data();
				for (unsigned frame = 0; frame < batchSize; ++frame)
				{
					input[channel][frame] = 0.25 * sin(
						2.0 * M_PI * (440.0 + 37.0 * channel) * frame /
						static_cast<double>(sampleRate));
				}
			}

			// Consume the pending coefficient handoff before timing the settled
			// one-bank path. Offline analysis already starts in the common A domain.
			unsigned settlingBlocks = 0;
			const unsigned maximumSettlingBlocks =
				(2 * sampleRate + batchSize - 1) / batchSize;
			do
			{
				filter.process(outputChannels.data(), inputChannels.data(), batchSize);
				++settlingBlocks;
			} while (!LoudnessCorrectionFilterTestAccess::settledOnNonIdentityBank(
				filter) && settlingBlocks < maximumSettlingBlocks);
			if (!LoudnessCorrectionFilterTestAccess::settledOnNonIdentityBank(filter))
			{
				fprintf(stderr, "Loudness performance filter did not settle.\n");
				valid = false;
			}

			timer.start();
			for (unsigned block = 0; block < blockCount; ++block)
				filter.process(outputChannels.data(), inputChannels.data(), batchSize);
			double processingSeconds = timer.stop();
			const double sampleCount = static_cast<double>(
				blockCount) * batchSize * channelCount;
			processingTimes.push_back(
				processingSeconds * 1.0e9 / sampleCount);
			outputChecksum += output[repeat % channelCount][repeat % batchSize];
		}

		if (!isfinite(outputChecksum))
		{
			fprintf(stderr, "Loudness performance checksum is non-finite.\n");
			valid = false;
		}
		return LoudnessPerformanceResult{
			median(initializeTimes),
			median(updateTimes),
			median(processingTimes),
			valid
		};
	}

	int runLoudnessPerformanceBenchmark()
	{
		const LoudnessPerformanceResult full = measureLoudnessPerformance(
			LoudnessCorrectionFilter::FilterParameters::ENGINE_FULL, true);
		const LoudnessPerformanceResult fullScalar = measureLoudnessPerformance(
			LoudnessCorrectionFilter::FilterParameters::ENGINE_FULL, false);
		const LoudnessPerformanceResult fast = measureLoudnessPerformance(
			LoudnessCorrectionFilter::FilterParameters::ENGINE_FAST, true);
		bool valid = full.valid && fullScalar.valid && fast.valid;
		printf(
			"Loudness Full: init %.3f ms, update %.3f ms, process %.3f ns/sample\n",
			full.initializeMilliseconds,
			full.updateMilliseconds,
			full.processingNanosecondsPerSample);
		printf(
			"Loudness Full scalar fallback: process %.3f ns/sample\n",
			fullScalar.processingNanosecondsPerSample);
		printf(
			"Loudness Fast: init %.3f ms, update %.3f ms, process %.3f ns/sample\n",
			fast.initializeMilliseconds,
			fast.updateMilliseconds,
			fast.processingNanosecondsPerSample);
		printf(
			"Ratios: Full block/scalar %.3f; Fast/Full init %.3f, update %.3f, process %.3f\n",
			full.processingNanosecondsPerSample /
				fullScalar.processingNanosecondsPerSample,
			fast.initializeMilliseconds / full.initializeMilliseconds,
			fast.updateMilliseconds / full.updateMilliseconds,
			fast.processingNanosecondsPerSample /
				full.processingNanosecondsPerSample);
		const unsigned additionalBatchSizes[] = { 16, 64, 1024 };
		for (unsigned batchSize : additionalBatchSizes)
		{
			const LoudnessPerformanceResult block = measureLoudnessPerformance(
				LoudnessCorrectionFilter::FilterParameters::ENGINE_FULL,
				true,
				batchSize);
			const LoudnessPerformanceResult scalar = measureLoudnessPerformance(
				LoudnessCorrectionFilter::FilterParameters::ENGINE_FULL,
				false,
				batchSize);
			valid = block.valid && scalar.valid && valid;
			printf(
				"Loudness Full batch %u: block %.3f, scalar %.3f ns/sample, ratio %.3f\n",
				batchSize,
				block.processingNanosecondsPerSample,
				scalar.processingNanosecondsPerSample,
				block.processingNanosecondsPerSample /
					scalar.processingNanosecondsPerSample);
		}
		return valid ? 0 : 1;
	}
}

int main(int argc, char** argv)
{
	try
	{
		stringstream versionStream;
		versionStream << MAJOR << "." << MINOR;
		if (REVISION != 0)
			versionStream << "." << REVISION;
		TCLAP::CmdLine cmd("Benchmark generates a linear sine sweep or reads from the given input file. "
			"It then filters the waveform using the Equalizer APO filter configuration "
			"and finally writes to the given file or into the user's temp directory.", ' ', versionStream.str());

		TCLAP::SwitchArg convSelfTestArg(
			"", "convselftest",
			"Run internal HybridConv correctness benchmark and exit", cmd);
		TCLAP::SwitchArg noPauseArg("", "nopause", "Do not wait for key press at the end", cmd);
		TCLAP::SwitchArg verboseArg("v", "verbose", "Print trace and error messages to console instead of logfile", cmd);
		TCLAP::SwitchArg loudnessTransitionTestArg(
			"", "loudness-transition-test",
			"Run native loudness coefficient-transition safety regressions", cmd);
		TCLAP::SwitchArg loudnessPerformanceArg(
			"", "loudness-performance",
			"Measure native loudness initialization, update, and callback cost", cmd);
		TCLAP::ValueArg<string> configArg("", "config", "Configuration file to load instead of the installed config.txt", false, "", "path", cmd);
		TCLAP::ValueArg<string> guidArg("", "guid", "Endpoint GUID to use when parsing configuration (Default: <empty>)", false, "", "string", cmd);
		TCLAP::ValueArg<string> connectionnameArg("", "connectionname", "Connection name to use when parsing configuration (Default: File output)", false, "File output", "string", cmd);
		TCLAP::ValueArg<string> devicenameArg("", "devicename", "Device name to use when parsing configuration (Default: Benchmark)", false, "Benchmark", "string", cmd);
		TCLAP::ValueArg<unsigned> batchsizeArg("", "batchsize", "Number of frames processed in one batch (Default: 65536)", false, 65536, "integer", cmd);
		TCLAP::ValueArg<string> outputArg("o", "output", "File to write sound data to", false, "", "string", cmd);
		TCLAP::ValueArg<string> inputArg("i", "input", "File to load sound data from instead of generating sweep", false, "", "string", cmd);
		TCLAP::ValueArg<unsigned> rateArg("r", "rate", "Sample rate of generated sweep (Default: 44100)", false, 44100, "integer", cmd);
		TCLAP::ValueArg<float> toArg("t", "to", "End frequency of generated sweep in Hz (Default: 20000.0)", false, 20000.0f, "float", cmd);
		TCLAP::ValueArg<float> fromArg("f", "from", "Start frequency of generated sweep in Hz (Default: 0.1)", false, 1.0f, "float", cmd);
		TCLAP::ValueArg<float> lengthArg("l", "length", "Length of generated sweep in seconds (Default: 200.0)", false, 200.0f, "float", cmd);
		TCLAP::ValueArg<unsigned> channelArg("c", "channels", "Number of channels of generated sweep (Default: 2)", false, 2, "integer", cmd);

		cmd.parse(argc, argv);

		bool verbose = verboseArg.getValue();
		LogHelper::set(stderr, verbose, true, true);
		if (convSelfTestArg.getValue())
			return runConvolutionSelfTest();
		if (loudnessTransitionTestArg.getValue())
			return runLoudnessTransitionTests();
		if (loudnessPerformanceArg.getValue())
			return runLoudnessPerformanceBenchmark();
#ifdef _DEBUG
		_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
		// _CrtSetBreakAlloc(3318);
#endif

		unsigned sampleRate;
		unsigned channelCount;
		unsigned channelMask;
		unsigned frameCount;
		float length;
		float* buf;

		if (REVISION == 0)
			printf("Benchmark %d.%d\n", MAJOR, MINOR);
		else
			printf("Benchmark %d.%d.%d\n", MAJOR, MINOR, REVISION);

		printf("Run \"%s -h\" to show usage info\n", argv[0]);
		printf("\n");

		string input = inputArg.getValue();
		if (input != "")
		{
			printf("Reading sound data from %s\n", input.c_str());

			PrecisionTimer timer;
			timer.start();

			SF_INFO info = {};
			SNDFILE* inFile = sf_open(input.c_str(), SFM_READ, &info);
			if (inFile == NULL)
			{
				fprintf(stderr, "%s", sf_strerror(inFile));
				return 1;
			}

			sampleRate = info.samplerate;
			channelCount = info.channels;
			channelMask = 0;
			frameCount = (unsigned)info.frames;
			length = float(frameCount) / sampleRate;

			buf = new float[frameCount * channelCount];

			sf_count_t numRead = 0;
			while (numRead < frameCount)
				numRead += sf_readf_float(inFile, buf + numRead * channelCount, frameCount - numRead);

			sf_close(inFile);
			inFile = NULL;

			double readTime = timer.stop();
			printf("Reading input file took %g seconds\n", readTime);
		}
		else
		{
			sampleRate = rateArg.getValue();
			channelMask = 0;
			channelCount = channelArg.getValue();
			float sweepFrom = fromArg.getValue();
			float sweepTo = toArg.getValue();
			float sweepDiff = sweepTo - sweepFrom;
			length = lengthArg.getValue();
			frameCount = (unsigned)(length * sampleRate);

			printf("No input file given, so generating linear sine sweep from %g to %g Hz over %g seconds\n", sweepFrom, sweepTo, length);

			PrecisionTimer timer;
			timer.start();

			buf = new float[frameCount * channelCount];
			for (unsigned i = 0; i < frameCount; i++)
			{
				double t = i * 1.0 / sampleRate;
				float s = (float)sin(((sweepFrom + sweepDiff * (t / length) / 2) * t) * 2 * M_PI);

				for (unsigned j = 0; j < channelCount; j++)
					buf[i * channelCount + j] = s;
			}

			double genTime = timer.stop();
			printf("Generating sweep took %g seconds\n", genTime);
		}

		unsigned batchsize = batchsizeArg.getValue();

		float* buf2 = new float[frameCount * channelCount];
		for (unsigned i = 0; i < frameCount * channelCount; i++)
			buf2[i] = 0.0f;

		PrecisionTimer timer;
		timer.start();
		{
			FilterEngine engine;
			wstring deviceName = StringHelper::toWString(devicenameArg.getValue(), CP_ACP);
			wstring connectionName = StringHelper::toWString(connectionnameArg.getValue(), CP_ACP);
			wstring deviceGuid = StringHelper::toWString(guidArg.getValue(), CP_ACP);
			wstring customConfigPath = StringHelper::toWString(configArg.getValue(), CP_ACP);
			engine.setDeviceInfo(false, true, deviceName, connectionName, deviceGuid, deviceName + L" " + connectionName + L" " + deviceGuid);
			engine.initialize((float)sampleRate, channelCount, channelCount, channelCount, channelMask, batchsize, customConfigPath);

			double initTime = timer.stop();
			if (!verbose)
				printf("\nLoading configuration took %g ms\n", initTime * 1000.0);

			printf("\nProcessing %d frames from %d channel(s)\n", frameCount, channelCount);

			timer.start();

			for (unsigned i = 0; i < frameCount; i += batchsize)
			{
				engine.process(buf2 + i * channelCount, buf + i * channelCount, min(batchsize, frameCount - i));
			}

			double time = timer.stop();

			printf("%d samples processed in %f seconds\n", frameCount * channelCount, time);
			printf("This is equivalent to %.2f%% CPU load (one core) when processing in real time\n", 100.0f * time / length);

			unsigned clipCount = 0;
			float max = 0;
			for (unsigned i = 0; i < frameCount * channelCount; i++)
			{
				float f = fabs(buf2[i]);
				if (f > max)
					max = f;
				if (f > 1.0f)
					clipCount++;
			}

			printf("Max output level: %f (%f dB)", max, log10(max) * 20.0f);
			if (clipCount > 0)
				printf(" (%d samples clipped!)", clipCount);
			printf("\n");

			string output = outputArg.getValue();
			if (output == "")
			{
				char temp[255];
				GetTempPathA(sizeof(temp) / sizeof(wchar_t), temp);

				output = temp;
				output += "testout.wav";
			}

			printf("\nWriting output to %s\n", output.c_str());

			SF_INFO info = {frameCount, (int)sampleRate, (int)channelCount, SF_FORMAT_WAV | SF_FORMAT_PCM_16, 0};
			SNDFILE* outFile = sf_open(output.c_str(), SFM_WRITE, &info);
			if (outFile == NULL)
			{
				fprintf(stderr, "%s", sf_strerror(outFile));
				return 1;
			}

			sf_count_t numWritten = 0;
			while (numWritten < frameCount)
				numWritten += sf_writef_float(outFile, buf2 + numWritten * channelCount, frameCount - numWritten);

			sf_close(outFile);
			outFile = NULL;

			delete[] buf;
			delete[] buf2;
		}

		if (!noPauseArg.getValue())
			system("pause");

		return 0;
	}
	catch (TCLAP::ArgException e)
	{
		printf("Error: %s for arg %s\n", e.error().c_str(), e.argId().c_str());
		return -1;
	}
}
