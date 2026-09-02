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
#include <string>
#include <sndfile.h>
#include <tclap/CmdLine.h>

#include "../version.h"
#include "../FilterEngine.h"
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

		bool passed = !LoudnessCorrectionFilterTestAccess::canTrackAutomaticVolume(
			filter);
		FilterRuntimeContext context;
		context.flowKnown = true;
		context.isCapture = true;
		context.endpointId = L"capture-endpoint";
		filter.setRuntimeContext(context);
		passed = !LoudnessCorrectionFilterTestAccess::canTrackAutomaticVolume(
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

		printf("Loudness runtime context: %s\n", passed ? "passed" : "failed");
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

	int runLoudnessTransitionTests()
	{
		bool passed = runLoudnessFormulaTests();
		passed = runLoudnessParameterCodecTests() && passed;
		passed = runLoudnessRuntimeContextTests() && passed;
		passed = runFilterEngineDeviceInfoReuseTests() && passed;
		passed = runLoudnessCrossoverCoefficientTests() && passed;
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

		TCLAP::SwitchArg noPauseArg("", "nopause", "Do not wait for key press at the end", cmd);
		TCLAP::SwitchArg verboseArg("v", "verbose", "Print trace and error messages to console instead of logfile", cmd);
		TCLAP::SwitchArg loudnessTransitionTestArg(
			"", "loudness-transition-test",
			"Run native loudness coefficient-transition safety regressions", cmd);
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
		if (loudnessTransitionTestArg.getValue())
			return runLoudnessTransitionTests();
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

			SF_INFO info;
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
