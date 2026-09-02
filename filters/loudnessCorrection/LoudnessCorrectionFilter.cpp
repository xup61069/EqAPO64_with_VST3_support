/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2017  Alexander Walch
    Copyright (C) 2026  Equalizer APO contributors

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "stdafx.h"
#include "LoudnessCorrectionFilter.h"
#include "VolumeController.h"
#include "helpers/LogHelper.h"

#include <algorithm>
#include <cmath>
#include <limits>

LoudnessCorrectionFilter::LoudnessCorrectionFilter(const FilterParameters& fParameters)
	: _parameterUpdateThreadHandle(NULL),
	  _stopParameterUpdateThreadEvent(NULL),
	  _parameters(fParameters),
	  _runtimeContext(),
	  _hasInitialAutomaticVolume(false),
	  _initialAutomaticVolume(0.0),
	  _runtimeBypass(false),
	  _recoveryPending(false),
	  _channelCount(0),
	  _activeBandCount(0),
	  _sampleRate(48000.0f),
	  _activeBankIndex(0),
	  _transitionBankIndex(1),
	  _crossoverPrewarmPosition(0),
	  _crossoverPrewarmLength(36000),
	  _warmupPosition(0),
	  _warmupLength(12000),
	  _crossfadePosition(0),
	  _crossfadeLength(4800),
	  _bypassFadePosition(0),
	  _bypassFadeLength(480),
	  _warmupActive(false),
	  _crossfadeActive(false),
	  _bypassFadeActive(false),
	  _runtimeBypassWasActive(false),
	  _transitionFromBypass(false),
	  _crossoverPrewarmActive(false),
	  _crossoverHandoffActive(false),
	  _crossoverDomainActive(),
	  _crossoverHandoffHasPrevious(),
	  _crossoverHandoffPreviousRaw(),
	  _crossoverHandoffPreviousIdentity(),
	  _crossoverDomainChannelCount(0),
	  _maximumCrossoverHandoffStep(0.0),
	  _pendingCoeffs{},
	  _lowpassCoeffs{},
	  _highpassCoeffs{},
	  _bankIdentity{ false, false },
	  _pendingIdentity(false),
	  _outputGainLinear(1.0),
	  _targetOutputGainLinear(1.0),
	  _pendingOutputGainLinear(1.0),
	  _coeffsUpdated(false)
{
	InitializeCriticalSection(&_parameterUpdateSection);

	for (size_t row = 0; row < NUM_BANDS; ++row)
	{
		for (size_t column = 0; column < NUM_BANDS; ++column)
			_inverseResponseMatrix[row][column] = 0.0;
	}
}

LoudnessCorrectionFilter::~LoudnessCorrectionFilter()
{
	if (_stopParameterUpdateThreadEvent)
		SetEvent(_stopParameterUpdateThreadEvent);

	// The worker owns references to this object and its critical section. Do not
	// destroy either until it has definitely exited.
	if (_parameterUpdateThreadHandle)
	{
		WaitForSingleObject(_parameterUpdateThreadHandle, INFINITE);
		CloseHandle(_parameterUpdateThreadHandle);
		_parameterUpdateThreadHandle = NULL;
	}

	if (_stopParameterUpdateThreadEvent)
	{
		CloseHandle(_stopParameterUpdateThreadEvent);
		_stopParameterUpdateThreadEvent = NULL;
	}

	DeleteCriticalSection(&_parameterUpdateSection);
}

bool LoudnessCorrectionFilter::canTrackAutomaticVolume() const
{
	if (!_runtimeContext.flowKnown || _runtimeContext.isCapture)
		return false;
	return _parameters.binding == FilterParameters::BINDING_ALL ||
		!_runtimeContext.endpointId.empty();
}

std::wstring LoudnessCorrectionFilter::getVolumeControllerEndpointId() const
{
	if (_parameters.binding == FilterParameters::BINDING_ALL)
		return L"";
	return _runtimeContext.endpointId;
}

std::vector<std::wstring> LoudnessCorrectionFilter::initialize(
	float sampleRate,
	unsigned maxFrameCount,
	std::vector<std::wstring> channelNames)
{
	(void)maxFrameCount;
	if (_stopParameterUpdateThreadEvent)
		SetEvent(_stopParameterUpdateThreadEvent);
	if (_parameterUpdateThreadHandle)
	{
		WaitForSingleObject(_parameterUpdateThreadHandle, INFINITE);
		CloseHandle(_parameterUpdateThreadHandle);
		_parameterUpdateThreadHandle = NULL;
	}
	if (_stopParameterUpdateThreadEvent)
	{
		CloseHandle(_stopParameterUpdateThreadEvent);
		_stopParameterUpdateThreadEvent = NULL;
	}

	_runtimeBypass.store(false, std::memory_order_relaxed);
	_recoveryPending.store(false, std::memory_order_relaxed);
	_hasInitialAutomaticVolume = false;
	_initialAutomaticVolume = 0.0;
	_channelCount = channelNames.size();
	_activeBandCount = 0;
	for (size_t bank = 0; bank < 2; ++bank)
	{
		_biquadBanks[bank].clear();
		_lowpassBanks[bank].clear();
		_highpassBanks[bank].clear();
	}
	_activeBankIndex = 0;
	_transitionBankIndex = 1;
	_crossoverPrewarmPosition = 0;
	_crossoverPrewarmLength = 36000;
	_warmupPosition = 0;
	_warmupLength = 12000;
	_crossfadePosition = 0;
	_bypassFadePosition = 0;
	_warmupActive = false;
	_crossfadeActive = false;
	_bypassFadeActive = false;
	_runtimeBypassWasActive = false;
	_transitionFromBypass = false;
	_crossoverPrewarmActive = false;
	_crossoverHandoffActive = false;
	_crossoverDomainActive.assign(_channelCount, 0);
	_crossoverHandoffHasPrevious.assign(_channelCount, 0);
	_crossoverHandoffPreviousRaw.assign(_channelCount, 0.0);
	_crossoverHandoffPreviousIdentity.assign(_channelCount, 0.0);
	_crossoverDomainChannelCount = 0;
	_maximumCrossoverHandoffStep = 0.0;
	_bankIdentity[0] = false;
	_bankIdentity[1] = false;
	_pendingIdentity = false;
	_coeffsUpdated.store(false, std::memory_order_relaxed);
	_sampleRate = std::isfinite(sampleRate) && sampleRate >= 8000.0f ? sampleRate : 48000.0f;
	_crossoverPrewarmLength = (std::max)(1u, static_cast<unsigned>(
		std::lround(_sampleRate * CROSSOVER_HISTORY_PREWARM_SECONDS)));
	_warmupLength = (std::max)(1u, static_cast<unsigned>(
		std::lround(_sampleRate * FILTER_WARMUP_SECONDS)));
	_crossfadeLength = (std::max)(1u, static_cast<unsigned>(
		std::lround(_sampleRate * COEFFICIENT_CROSSFADE_SECONDS)));
	_bypassFadeLength = (std::max)(1u, static_cast<unsigned>(
		std::lround(_sampleRate * BYPASS_FADE_SECONDS)));
	for (size_t section = 0; section < CROSSOVER_SECTION_COUNT; ++section)
	{
		computeCrossoverCoeffs(false, section, _lowpassCoeffs[section]);
		computeCrossoverCoeffs(true, section, _highpassCoeffs[section]);
	}

	// Frequencies at or above 90% of Nyquist are not representable reliably.
	double maximumCenterFrequency = 0.45 * static_cast<double>(_sampleRate);
	while (_activeBandCount < NUM_BANDS &&
		LoudnessProfile::LOUDNESS_PROFILE_TABLE[_activeBandCount].frequency <= maximumCenterFrequency)
	{
		++_activeBandCount;
	}

	for (size_t bank = 0; bank < 2; ++bank)
	{
		_biquadBanks[bank].resize(_channelCount);
		_lowpassBanks[bank].resize(_channelCount);
		_highpassBanks[bank].resize(_channelCount);
		for (size_t channel = 0; channel < _channelCount; ++channel)
		{
			_biquadBanks[bank][channel].reserve(_activeBandCount);
			for (size_t band = 0; band < _activeBandCount; ++band)
			{
				_biquadBanks[bank][channel].push_back(BiQuad(
					BiQuad::PEAKING,
					0.0,
					LoudnessProfile::LOUDNESS_PROFILE_TABLE[band].frequency,
					_sampleRate,
					FILTER_Q,
					false));
			}

			_lowpassBanks[bank][channel].reserve(CROSSOVER_SECTION_COUNT);
			_highpassBanks[bank][channel].reserve(CROSSOVER_SECTION_COUNT);
			for (size_t section = 0; section < CROSSOVER_SECTION_COUNT; ++section)
			{
				_lowpassBanks[bank][channel].push_back(BiQuad());
				_highpassBanks[bank][channel].push_back(BiQuad());
				double lowpassCoefficients[4] = {
					_lowpassCoeffs[section].b1,
					_lowpassCoeffs[section].b2,
					_lowpassCoeffs[section].a1,
					_lowpassCoeffs[section].a2
				};
				double highpassCoefficients[4] = {
					_highpassCoeffs[section].b1,
					_highpassCoeffs[section].b2,
					_highpassCoeffs[section].a1,
					_highpassCoeffs[section].a2
				};
				_lowpassBanks[bank][channel][section].setCoefficients(
					lowpassCoefficients, _lowpassCoeffs[section].b0);
				_highpassBanks[bank][channel][section].setCoefficients(
					highpassCoefficients, _highpassCoeffs[section].b0);
			}
		}
	}

	computeResponseInverse();

	double initialVolume = 0.0;
	if (_parameters.state && _parameters.useManualVolume)
	{
		initialVolume = _parameters.manualVolume;
	}
	else if (_parameters.state)
	{
		if (!canTrackAutomaticVolume())
		{
			_runtimeBypass.store(true, std::memory_order_relaxed);
			LogF(L"LoudnessCorrection automatic volume mode is unavailable for this binding; filter is bypassed. Use manual volume mode.");
		}
		else
		{
			VolumeController volumeController(getVolumeControllerEndpointId());
			if (FAILED(volumeController.getVolume(initialVolume)))
			{
				_runtimeBypass.store(true, std::memory_order_relaxed);
				initialVolume = 0.0;
				LogF(L"LoudnessCorrection could not read the configured endpoint volume; filter is bypassed until the endpoint recovers.");
			}
			else
			{
				_hasInitialAutomaticVolume = true;
				_initialAutomaticVolume = initialVolume;
			}
		}
	}

	std::vector<double> gains;
	calculateBandGains(initialVolume, gains, _outputGainLinear);
	bool initialIdentity = _outputGainLinear == 1.0;
	for (size_t band = 0; band < _activeBandCount; ++band)
		initialIdentity = initialIdentity && std::abs(gains[band]) <= 1.0e-12;
	_bankIdentity[0] = initialIdentity;
	_bankIdentity[1] = initialIdentity;
	_pendingIdentity = initialIdentity;
	_targetOutputGainLinear = _outputGainLinear;
	_pendingOutputGainLinear = _outputGainLinear;
	for (size_t band = 0; band < _activeBandCount; ++band)
	{
		computeBiquadCoeffs(band, gains[band], _pendingCoeffs[band]);
		double coefficients[4] = {
			_pendingCoeffs[band].b1,
			_pendingCoeffs[band].b2,
			_pendingCoeffs[band].a1,
			_pendingCoeffs[band].a2
		};
		for (size_t bank = 0; bank < 2; ++bank)
		{
			for (size_t channel = 0; channel < _channelCount; ++channel)
			{
				_biquadBanks[bank][channel][band].setCoefficients(
					coefficients, _pendingCoeffs[band].b0);
			}
		}
	}

	// Every enabled instance first accumulates live fixed-crossover history,
	// even when its initial correction is neutral or endpoint tracking starts
	// in fail-closed bypass. A non-neutral target remains pending until that
	// history is ready; this prevents an early volume update or recovery from
	// copying a partially settled 25 Hz state into an audible transition.
	if (_parameters.state && _parameters.attenuation > 0.0f &&
		_activeBandCount > 0)
	{
		_bankIdentity[0] = true;
		_activeBankIndex = 0;
		_transitionBankIndex = 1;
		_outputGainLinear = 1.0;
		_crossoverPrewarmPosition = 0;
		_warmupPosition = 0;
		_crossfadePosition = 0;
		_crossoverPrewarmActive = true;
		_crossoverHandoffActive = false;
		_warmupActive = false;
		_crossfadeActive = false;
		_transitionFromBypass = false;
		_coeffsUpdated.store(!initialIdentity, std::memory_order_relaxed);
	}

	// Manual mode is immutable for the lifetime of a filter instance, so it
	// needs no polling thread. A configuration edit creates a new instance.
	if (_parameters.state && !_parameters.useManualVolume &&
		canTrackAutomaticVolume())
	{
		_stopParameterUpdateThreadEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		if (_stopParameterUpdateThreadEvent)
		{
			_parameterUpdateThreadHandle =
				CreateThread(NULL, 0, &parameterUpdateThread, this, 0, NULL);
			if (!_parameterUpdateThreadHandle)
			{
				CloseHandle(_stopParameterUpdateThreadEvent);
				_stopParameterUpdateThreadEvent = NULL;
				_runtimeBypass.store(true, std::memory_order_relaxed);
				LogF(L"LoudnessCorrection could not start endpoint-volume tracking; filter is bypassed.");
			}
		}
		else
		{
			_runtimeBypass.store(true, std::memory_order_relaxed);
			LogF(L"LoudnessCorrection could not create the endpoint-volume tracking event; filter is bypassed.");
		}
	}

	return channelNames;
}

void LoudnessCorrectionFilter::computeResponseInverse()
{
	for (size_t row = 0; row < NUM_BANDS; ++row)
	{
		for (size_t column = 0; column < NUM_BANDS; ++column)
			_inverseResponseMatrix[row][column] = 0.0;
	}

	if (_activeBandCount == 0)
		return;

	double augmented[NUM_BANDS][2 * NUM_BANDS] = {};
	for (size_t row = 0; row < _activeBandCount; ++row)
	{
		double frequency = LoudnessProfile::LOUDNESS_PROFILE_TABLE[row].frequency;
		for (size_t column = 0; column < _activeBandCount; ++column)
			augmented[row][column] = biquadResponseDb(column, 1.0, frequency);
		augmented[row][row + _activeBandCount] = 1.0;
	}

	for (size_t column = 0; column < _activeBandCount; ++column)
	{
		size_t pivot = column;
		for (size_t row = column + 1; row < _activeBandCount; ++row)
		{
			if (std::abs(augmented[row][column]) > std::abs(augmented[pivot][column]))
				pivot = row;
		}

		if (std::abs(augmented[pivot][column]) < 1.0e-10)
		{
			// This should not occur with the Q=3 one-third-octave basis. Keep
			// a safe identity fallback instead of creating NaN coefficients.
			for (size_t index = 0; index < _activeBandCount; ++index)
				_inverseResponseMatrix[index][index] = 1.0;
			return;
		}

		if (pivot != column)
		{
			for (size_t value = 0; value < 2 * _activeBandCount; ++value)
				std::swap(augmented[column][value], augmented[pivot][value]);
		}

		double divisor = augmented[column][column];
		for (size_t value = 0; value < 2 * _activeBandCount; ++value)
			augmented[column][value] /= divisor;

		for (size_t row = 0; row < _activeBandCount; ++row)
		{
			if (row == column)
				continue;
			double factor = augmented[row][column];
			for (size_t value = 0; value < 2 * _activeBandCount; ++value)
				augmented[row][value] -= factor * augmented[column][value];
		}
	}

	for (size_t row = 0; row < _activeBandCount; ++row)
	{
		for (size_t column = 0; column < _activeBandCount; ++column)
		{
			_inverseResponseMatrix[row][column] =
				augmented[row][column + _activeBandCount];
		}
	}
}

bool LoudnessCorrectionFilter::isSafeCrossoverHandoff(
	double previousRaw,
	double currentRaw,
	double previousIdentity,
	double currentIdentity,
	double& handoffStep,
	double& naturalStep)
{
	double previousDifference = previousIdentity - previousRaw;
	double currentDifference = currentIdentity - currentRaw;
	bool crossed = previousDifference == 0.0 || currentDifference == 0.0 ||
		(previousDifference < 0.0 && currentDifference >= 0.0) ||
		(previousDifference > 0.0 && currentDifference <= 0.0);
	handoffStep = std::abs(currentIdentity - previousRaw);
	naturalStep = (std::max)(
		std::abs(currentRaw - previousRaw),
		std::abs(currentIdentity - previousIdentity));
	double scale = (std::max)(1.0, (std::max)(
		(std::max)(std::abs(previousRaw), std::abs(currentRaw)),
		(std::max)(std::abs(previousIdentity), std::abs(currentIdentity))));
	double tolerance =
		64.0 * std::numeric_limits<double>::epsilon() * scale;
	return crossed && std::isfinite(handoffStep) &&
		std::isfinite(naturalStep) && handoffStep <= naturalStep + tolerance;
}

void LoudnessCorrectionFilter::calculateBandGains(
	double currentVolumeDb,
	std::vector<double>& outGains,
	double& outputGainLinear) const
{
	outGains.assign(NUM_BANDS, 0.0);
	outputGainLinear = 1.0;
	if (_activeBandCount == 0 || _parameters.attenuation <= 0.0f)
		return;

	double currentVolume = std::isfinite(currentVolumeDb) ? currentVolumeDb : 0.0;
	currentVolume = (std::max)(-100.0, (std::min)(0.0, currentVolume));
	double referenceLevel = (std::max)(1.0, (std::min)(100.0,
		static_cast<double>(_parameters.referenceLevel)));
	double loudnessLevel = referenceLevel + currentVolume - _parameters.referenceOffset;
	loudnessLevel = (std::max)(0.0, (std::min)(100.0, loudnessLevel));

	double target[NUM_BANDS] = {};
	for (size_t point = 0; point < _activeBandCount; ++point)
	{
		target[point] = static_cast<double>(_parameters.attenuation) *
			LoudnessProfile::computeContourDelta(loudnessLevel, referenceLevel, point);
	}

	for (size_t band = 0; band < _activeBandCount; ++band)
	{
		for (size_t point = 0; point < _activeBandCount; ++point)
			outGains[band] += _inverseResponseMatrix[band][point] * target[point];
		outGains[band] = (std::max)(-MAX_FILTER_GAIN_DB,
			(std::min)(MAX_FILTER_GAIN_DB, outGains[band]));
	}

	// A biquad's dB response is not perfectly linear in gain. Reusing the
	// well-conditioned unit-response inverse as a residual corrector converges
	// to the CSV anchors in a few inexpensive background-thread iterations.
	for (unsigned iteration = 1; iteration < FIT_ITERATIONS; ++iteration)
	{
		double residual[NUM_BANDS] = {};
		for (size_t point = 0; point < _activeBandCount; ++point)
		{
			double actual = 0.0;
			double frequency = LoudnessProfile::LOUDNESS_PROFILE_TABLE[point].frequency;
			for (size_t band = 0; band < _activeBandCount; ++band)
				actual += biquadResponseDb(band, outGains[band], frequency);
			residual[point] = target[point] - actual;
		}

		for (size_t band = 0; band < _activeBandCount; ++band)
		{
			double correction = 0.0;
			for (size_t point = 0; point < _activeBandCount; ++point)
				correction += _inverseResponseMatrix[band][point] * residual[point];
			if (std::isfinite(correction))
			{
				outGains[band] = (std::max)(-MAX_FILTER_GAIN_DB,
					(std::min)(MAX_FILTER_GAIN_DB, outGains[band] + correction));
			}
		}
	}

	outputGainLinear = calculateHeadroomGain(outGains);
}

double LoudnessCorrectionFilter::calculateHeadroomGain(
	const std::vector<double>& gains) const
{
	if (_activeBandCount == 0)
		return 1.0;
	bool hasNonZeroGain = false;
	for (size_t band = 0; band < _activeBandCount; ++band)
	{
		if (std::abs(gains[band]) > 1.0e-12)
		{
			hasNonZeroGain = true;
			break;
		}
	}
	if (!hasNonZeroGain)
		return 1.0;

	double maximumResponse = findMaximumResponseDb(gains, 1.0, false);
	double outputGainLinear = maximumResponse <= 0.0 ? 1.0 :
		std::pow(10.0, -(maximumResponse + HEADROOM_MARGIN_DB) / 20.0);

	// The audible transfer is the complex sum L + H*C, not merely the
	// correction cascade C. LR28 gives L and H matching phase with
	// |L| + |H| = 1, while the correction branch retains the 1 dB margin.
	// Scan the actual final transfer as a defense against implementation or
	// floating-point drift. The fallback search runs only if that invariant is
	// unexpectedly violated, and still happens off the audio callback.
	if (findMaximumResponseDb(gains, outputGainLinear, true) <=
		FINAL_RESPONSE_NUMERICAL_TOLERANCE_DB)
		return outputGainLinear;

	double safeGain = 0.0;
	double unsafeGain = outputGainLinear;
	for (unsigned iteration = 0; iteration < 48; ++iteration)
	{
		double candidate = 0.5 * (safeGain + unsafeGain);
		if (findMaximumResponseDb(gains, candidate, true) <=
			FINAL_RESPONSE_NUMERICAL_TOLERANCE_DB)
			safeGain = candidate;
		else
			unsafeGain = candidate;
	}
	return safeGain;
}

double LoudnessCorrectionFilter::findMaximumResponseDb(
	const std::vector<double>& gains,
	double outputGainLinear,
	bool includeSubsonicCrossover) const
{
	if (_activeBandCount == 0)
		return 0.0;

	double maximumFrequency = (std::min)(
		20000.0,
		0.499 * static_cast<double>(_sampleRate));
	if (maximumFrequency <= 1.0)
		return 0.0;

	const double minimumFrequency = 1.0;
	const double logMinimum = std::log(minimumFrequency);
	const double logMaximum = std::log(maximumFrequency);
	const double logStep = (logMaximum - logMinimum) /
		static_cast<double>(RESPONSE_SCAN_POINTS - 1);
	std::vector<double> responses(RESPONSE_SCAN_POINTS, 0.0);

	auto responseAtLogFrequency = [
		this,
		&gains,
		outputGainLinear,
		includeSubsonicCrossover](double logFrequency)
	{
		double frequency = std::exp(logFrequency);
		if (includeSubsonicCrossover)
			return guardedResponseDb(gains, outputGainLinear, frequency);

		double response = 20.0 * std::log10(
			(std::max)(1.0e-15, outputGainLinear));
		for (size_t band = 0; band < _activeBandCount; ++band)
			response += biquadResponseDb(band, gains[band], frequency);
		return response;
	};

	// A dense logarithmic scan resolves the Q=3 pass bands by hundreds of
	// samples. Each detected local maximum is then refined in log-frequency
	// space, avoiding the inter-bin peaks missed by the former 256-point scan.
	double maximumResponse = 0.0;
	for (unsigned point = 0; point < RESPONSE_SCAN_POINTS; ++point)
	{
		double logFrequency = logMinimum + static_cast<double>(point) * logStep;
		responses[point] = responseAtLogFrequency(logFrequency);
		maximumResponse = (std::max)(maximumResponse, responses[point]);
	}

	const double goldenRatioConjugate = 0.6180339887498948482;
	for (unsigned point = 1; point + 1 < RESPONSE_SCAN_POINTS; ++point)
	{
		if (responses[point] < responses[point - 1] ||
			responses[point] < responses[point + 1] ||
			(responses[point] == responses[point - 1] &&
				responses[point] == responses[point + 1]))
		{
			continue;
		}

		double left = logMinimum + static_cast<double>(point - 1) * logStep;
		double right = logMinimum + static_cast<double>(point + 1) * logStep;
		double innerLeft = right - goldenRatioConjugate * (right - left);
		double innerRight = left + goldenRatioConjugate * (right - left);
		double leftResponse = responseAtLogFrequency(innerLeft);
		double rightResponse = responseAtLogFrequency(innerRight);

		for (unsigned iteration = 0;
			iteration < RESPONSE_REFINEMENT_ITERATIONS;
			++iteration)
		{
			if (leftResponse < rightResponse)
			{
				left = innerLeft;
				innerLeft = innerRight;
				leftResponse = rightResponse;
				innerRight = left + goldenRatioConjugate * (right - left);
				rightResponse = responseAtLogFrequency(innerRight);
			}
			else
			{
				right = innerRight;
				innerRight = innerLeft;
				rightResponse = leftResponse;
				innerLeft = right - goldenRatioConjugate * (right - left);
				leftResponse = responseAtLogFrequency(innerLeft);
			}
		}

		maximumResponse = (std::max)(maximumResponse,
			(std::max)(leftResponse, rightResponse));
	}

	return maximumResponse;
}

void LoudnessCorrectionFilter::publishVolumeUpdate(
	double currentVolumeDb,
	std::vector<double>& scratchGains)
{
	double outputGainLinear = 1.0;
	calculateBandGains(currentVolumeDb, scratchGains, outputGainLinear);
	bool identity = outputGainLinear == 1.0;
	for (size_t band = 0; band < _activeBandCount; ++band)
		identity = identity && std::abs(scratchGains[band]) <= 1.0e-12;

	EnterCriticalSection(&_parameterUpdateSection);
	for (size_t band = 0; band < _activeBandCount; ++band)
		computeBiquadCoeffs(band, scratchGains[band], _pendingCoeffs[band]);
	_pendingOutputGainLinear = outputGainLinear;
	_pendingIdentity = identity;
	_coeffsUpdated.store(true, std::memory_order_release);
	LeaveCriticalSection(&_parameterUpdateSection);
}

void LoudnessCorrectionFilter::computeBiquadCoeffs(
	size_t bandIndex,
	double gainDb,
	BiquadCoeffs& coeffs) const
{
	gainDb = (std::max)(-MAX_FILTER_GAIN_DB, (std::min)(MAX_FILTER_GAIN_DB, gainDb));
	double frequency = LoudnessProfile::LOUDNESS_PROFILE_TABLE[bandIndex].frequency;
	double A = std::pow(10.0, gainDb / 40.0);
	double omega = 2.0 * PI * frequency / _sampleRate;
	double alpha = std::sin(omega) / (2.0 * FILTER_Q);
	double cosine = std::cos(omega);

	double b0 = 1.0 + alpha * A;
	double b1 = -2.0 * cosine;
	double b2 = 1.0 - alpha * A;
	double a0 = 1.0 + alpha / A;
	double a1 = -2.0 * cosine;
	double a2 = 1.0 - alpha / A;

	coeffs.b0 = b0 / a0;
	coeffs.b1 = b1 / a0;
	coeffs.b2 = b2 / a0;
	coeffs.a1 = a1 / a0;
	coeffs.a2 = a2 / a0;
}

void LoudnessCorrectionFilter::computeCrossoverCoeffs(
	bool highPass,
	size_t sectionIndex,
	BiquadCoeffs& coeffs) const
{
	size_t butterworthSection =
		sectionIndex % (CROSSOVER_BUTTERWORTH_ORDER / 2);
	double q = 1.0 / (2.0 * std::sin(
		(2.0 * static_cast<double>(butterworthSection) + 1.0) * PI /
		(2.0 * static_cast<double>(CROSSOVER_BUTTERWORTH_ORDER))));
	double omega = 2.0 * PI * SUBSONIC_CROSSOVER_FREQUENCY_HZ / _sampleRate;
	double alpha = std::sin(omega) / (2.0 * q);
	double cosine = std::cos(omega);
	double a0 = 1.0 + alpha;
	double sineHalf = std::sin(0.5 * omega);
	double cosineHalf = std::cos(0.5 * omega);

	if (highPass)
	{
		// cos(omega / 2)^2 avoids adding nearly equal values.
		coeffs.b0 = cosineHalf * cosineHalf / a0;
		coeffs.b1 = -2.0 * coeffs.b0;
		coeffs.b2 = coeffs.b0;
	}
	else
	{
		// sin(omega / 2)^2 avoids the 1-cos cancellation that otherwise
		// becomes measurable at very high sample rates.
		coeffs.b0 = sineHalf * sineHalf / a0;
		coeffs.b1 = 2.0 * coeffs.b0;
		coeffs.b2 = coeffs.b0;
	}
	coeffs.a1 = -2.0 * cosine / a0;
	coeffs.a2 = (1.0 - alpha) / a0;
}

std::complex<double> LoudnessCorrectionFilter::biquadResponse(
	const BiquadCoeffs& coeffs,
	double frequency) const
{
	double omega = 2.0 * PI * frequency / _sampleRate;
	std::complex<double> z(std::cos(omega), -std::sin(omega));
	std::complex<double> z2 = z * z;
	std::complex<double> numerator =
		coeffs.b0 + coeffs.b1 * z + coeffs.b2 * z2;
	std::complex<double> denominator =
		1.0 + coeffs.a1 * z + coeffs.a2 * z2;
	if (std::norm(denominator) < 1.0e-30)
		return std::complex<double>(0.0, 0.0);
	return numerator / denominator;
}

double LoudnessCorrectionFilter::biquadResponseDb(
	size_t bandIndex,
	double gainDb,
	double frequency) const
{
	BiquadCoeffs coefficients;
	computeBiquadCoeffs(bandIndex, gainDb, coefficients);
	double magnitudeSquared = (std::max)(
		1.0e-30,
		std::norm(biquadResponse(coefficients, frequency)));
	return 10.0 * std::log10(magnitudeSquared);
}

double LoudnessCorrectionFilter::guardedResponseDb(
	const std::vector<double>& gains,
	double outputGainLinear,
	double frequency) const
{
	std::complex<double> correction(outputGainLinear, 0.0);
	for (size_t band = 0; band < _activeBandCount; ++band)
	{
		BiquadCoeffs coefficients;
		computeBiquadCoeffs(band, gains[band], coefficients);
		correction *= biquadResponse(coefficients, frequency);
	}

	std::complex<double> lowpass(1.0, 0.0);
	std::complex<double> highpass(1.0, 0.0);
	for (size_t section = 0; section < CROSSOVER_SECTION_COUNT; ++section)
	{
		lowpass *= biquadResponse(_lowpassCoeffs[section], frequency);
		highpass *= biquadResponse(_highpassCoeffs[section], frequency);
	}

	double magnitudeSquared = (std::max)(
		1.0e-30,
		std::norm(lowpass + highpass * correction));
	return 10.0 * std::log10(magnitudeSquared);
}

unsigned long __stdcall LoudnessCorrectionFilter::parameterUpdateThread(void* parameter)
{
	LoudnessCorrectionFilter* self = static_cast<LoudnessCorrectionFilter*>(parameter);
	VolumeController volumeController(self->getVolumeControllerEndpointId());
	double lastVolume = self->_hasInitialAutomaticVolume ?
		self->_initialAutomaticVolume :
		std::numeric_limits<double>::quiet_NaN();
	ULONGLONG lastReadTime = 0;
	std::vector<double> gains;

	while (WaitForSingleObject(
		self->_stopParameterUpdateThreadEvent,
		UPDATE_POLL_INTERVAL_MS) == WAIT_TIMEOUT)
	{
		ULONGLONG now = GetTickCount64();
		bool callbackChanged = volumeController.hasVolumeChanged();
		bool fallbackPollDue = lastReadTime == 0 ||
			now - lastReadTime >= FALLBACK_POLL_INTERVAL_MS;
		if (!callbackChanged && !fallbackPollDue)
			continue;

		lastReadTime = now;
		double currentVolume = 0.0;
		if (FAILED(volumeController.getVolume(currentVolume)))
		{
			self->_recoveryPending.store(false, std::memory_order_release);
			self->_runtimeBypass.store(true, std::memory_order_release);
			continue;
		}

		bool recovering = self->_runtimeBypass.load(std::memory_order_acquire);
		if (!recovering && std::isfinite(lastVolume) &&
			std::abs(currentVolume - lastVolume) <= 0.05)
			continue;

		self->publishVolumeUpdate(currentVolume, gains);
		if (recovering)
		{
			// Keep bypass asserted until the audio thread has installed the
			// recovered coefficients. It will warm the new bank silently and
			// crossfade from the common magnitude-unity A = L + H domain.
			self->_recoveryPending.store(true, std::memory_order_release);
		}
		lastVolume = currentVolume;
	}
	return 0;
}

#pragma AVRT_CODE_BEGIN
void LoudnessCorrectionFilter::process(double** output, double** input, unsigned frameCount)
{
	if (!_parameters.state || _parameters.attenuation <= 0.0f ||
		_activeBandCount == 0)
	{
		for (size_t channel = 0; channel < _channelCount; ++channel)
		{
			for (unsigned frame = 0; frame < frameCount; ++frame)
				output[channel][frame] = input[channel][frame];
		}
		return;
	}

	bool runtimeBypass = _runtimeBypass.load(std::memory_order_acquire);
	bool crossoverDomainReady = !_crossoverPrewarmActive &&
		!_crossoverHandoffActive &&
		_crossoverDomainChannelCount == _channelCount;
	if (runtimeBypass && !_runtimeBypassWasActive && crossoverDomainReady)
	{
		// Preserve the currently audible correction-bank composite and fade only
		// its residual relative to A = L + H. Cancelling the bank transition here
		// would create a single-sample corrected-to-A discontinuity.
		_bypassFadePosition = 0;
		_bypassFadeActive = true;
	}
	else if (runtimeBypass && !crossoverDomainReady)
	{
		// Before the cold-start raw-to-A handoff there is no audible correction
		// transition to preserve. Stay raw and keep the fixed LR28 histories live.
		_bypassFadePosition = 0;
		_bypassFadeActive = false;
		_warmupActive = false;
		_crossfadeActive = false;
		_transitionFromBypass = false;
	}

	bool recoveryReady = runtimeBypass &&
		!_bypassFadeActive &&
		_recoveryPending.load(std::memory_order_acquire);

	if ((!runtimeBypass || recoveryReady) &&
		!_crossoverPrewarmActive && !_crossoverHandoffActive &&
		!_warmupActive && !_crossfadeActive && !_bypassFadeActive &&
		_coeffsUpdated.load(std::memory_order_acquire) &&
		TryEnterCriticalSection(&_parameterUpdateSection))
	{
		bool requiresTransition =
			!(_pendingIdentity && _bankIdentity[_activeBankIndex]);
		if (requiresTransition)
		{
			_transitionBankIndex = 1 - _activeBankIndex;
			for (size_t channel = 0; channel < _channelCount; ++channel)
			{
				// Correction-filter histories belong to their old coefficients and
				// cannot be reused safely. The fixed LR crossover histories, however,
				// are live input histories and avoid a multi-second 25 Hz restart.
				for (size_t band = 0; band < _activeBandCount; ++band)
					_biquadBanks[_transitionBankIndex][channel][band].resetState();
				for (size_t section = 0;
					section < CROSSOVER_SECTION_COUNT;
					++section)
				{
					_lowpassBanks[_transitionBankIndex][channel][section] =
						_lowpassBanks[_activeBankIndex][channel][section];
					_highpassBanks[_transitionBankIndex][channel][section] =
						_highpassBanks[_activeBankIndex][channel][section];
				}
			}
			for (size_t band = 0; band < _activeBandCount; ++band)
			{
				double coefficients[4] = {
					_pendingCoeffs[band].b1,
					_pendingCoeffs[band].b2,
					_pendingCoeffs[band].a1,
					_pendingCoeffs[band].a2
				};
				for (size_t channel = 0; channel < _channelCount; ++channel)
					_biquadBanks[_transitionBankIndex][channel][band]
						.setCoefficients(
							coefficients,
							_pendingCoeffs[band].b0);
			}
			_targetOutputGainLinear = _pendingOutputGainLinear;
			_bankIdentity[_transitionBankIndex] = _pendingIdentity;
			_warmupPosition = 0;
			_crossfadePosition = 0;
			_warmupActive = true;
			_crossfadeActive = false;
			_transitionFromBypass = recoveryReady;
		}
		else
		{
			// A latest pending identity target is already audible in the shared
			// magnitude-unity A = L + H domain. No coefficient transition is needed.
			_outputGainLinear = 1.0;
			_warmupActive = false;
			_crossfadeActive = false;
			_transitionFromBypass = false;
		}
		_coeffsUpdated.store(false, std::memory_order_release);
		if (recoveryReady)
		{
			// Clear bypass before claiming the recovery token. A simultaneous
			// read failure clears that token first; if so, restore bypass instead
			// of overwriting the newer failure with a stale success.
			_runtimeBypass.store(false, std::memory_order_release);
			bool expectedRecovery = true;
			if (_recoveryPending.compare_exchange_strong(
				expectedRecovery,
				false,
				std::memory_order_acq_rel,
				std::memory_order_acquire))
			{
				runtimeBypass = false;
			}
			else
			{
				_runtimeBypass.store(true, std::memory_order_release);
				runtimeBypass = true;
				_warmupActive = false;
				_crossfadeActive = false;
				_transitionFromBypass = false;
			}
		}
		LeaveCriticalSection(&_parameterUpdateSection);
	}

	struct BankSample
	{
		double identity;
		double corrected;
	};

	for (size_t channel = 0; channel < _channelCount; ++channel)
	{
		double* inputChannel = input[channel];
		double* outputChannel = output[channel];
		for (unsigned frame = 0; frame < frameCount; ++frame)
		{
			double inputSample = inputChannel[frame];
			auto processBank = [this, channel, inputSample](
				size_t bankIndex,
				double outputGainLinear) -> BankSample
			{
				double lowpassSample = inputSample;
				for (size_t section = 0;
					section < CROSSOVER_SECTION_COUNT;
					++section)
				{
					lowpassSample = _lowpassBanks[bankIndex][channel][section]
						.process(lowpassSample);
				}

				double highpassIdentitySample = inputSample;
				for (size_t section = 0;
					section < CROSSOVER_SECTION_COUNT;
					++section)
				{
					highpassIdentitySample =
						_highpassBanks[bankIndex][channel][section]
							.process(highpassIdentitySample);
				}
				double highpassCorrectedSample =
					highpassIdentitySample * outputGainLinear;
				for (size_t band = 0; band < _activeBandCount; ++band)
				{
					highpassCorrectedSample =
						_biquadBanks[bankIndex][channel][band]
							.process(highpassCorrectedSample);
				}
				return BankSample{
					lowpassSample + highpassIdentitySample,
					lowpassSample + highpassCorrectedSample
				};
			};

			BankSample activeSample = processBank(
				_activeBankIndex,
				_outputGainLinear);

			if (!_crossoverDomainActive[channel])
			{
				if (_crossoverHandoffActive)
				{
					bool hasPrevious =
						_crossoverHandoffHasPrevious[channel] != 0;
					bool switchToCrossoverDomain = false;
					double handoffStep = 0.0;
					if (hasPrevious)
					{
						double previousRaw =
							_crossoverHandoffPreviousRaw[channel];
						double previousIdentity =
							_crossoverHandoffPreviousIdentity[channel];
						// Switch only when the sampled raw/A intersection produces no
						// larger output step than either signal's natural one-sample step.
						// If this crossing is not safe, keep raw and try the next one;
						// there is deliberately no timeout that can force a click.
						double naturalStep = 0.0;
						switchToCrossoverDomain = isSafeCrossoverHandoff(
							previousRaw,
							inputSample,
							previousIdentity,
							activeSample.identity,
							handoffStep,
							naturalStep);
						if (switchToCrossoverDomain)
						{
							_crossoverDomainActive[channel] = 1;
							++_crossoverDomainChannelCount;
							_maximumCrossoverHandoffStep = (std::max)(
								_maximumCrossoverHandoffStep,
								handoffStep);
						}
					}

					_crossoverHandoffPreviousRaw[channel] = inputSample;
					_crossoverHandoffPreviousIdentity[channel] =
						activeSample.identity;
					_crossoverHandoffHasPrevious[channel] = 1;

					if (switchToCrossoverDomain)
					{
						outputChannel[frame] = activeSample.identity;
						continue;
					}
				}

				outputChannel[frame] = inputSample;
				continue;
			}

			double audibleComposite;
			if (_warmupActive)
			{
				// Feed every state in the target bank real input while keeping it
				// silent. LP and HP state are bank-local, so recovery never reuses a
				// frozen crossover history.
				(void)processBank(
					_transitionBankIndex,
					_targetOutputGainLinear);

				audibleComposite =
					(_transitionFromBypass || _bankIdentity[_activeBankIndex]) ?
					activeSample.identity : activeSample.corrected;
			}
			else if (_crossfadeActive)
			{
				BankSample transitionBankSample = processBank(
					_transitionBankIndex,
					_targetOutputGainLinear);
				double transitionSample =
					_bankIdentity[_transitionBankIndex] ?
					transitionBankSample.identity :
					transitionBankSample.corrected;

				if (_crossfadePosition + frame < _crossfadeLength)
				{
					double activeAudibleSample =
						(_transitionFromBypass ||
							_bankIdentity[_activeBankIndex]) ?
						activeSample.identity : activeSample.corrected;
					double mix =
						static_cast<double>(_crossfadePosition + frame + 1) /
						static_cast<double>(_crossfadeLength);
					audibleComposite =
						activeAudibleSample * (1.0 - mix) +
						transitionSample * mix;
				}
				else
				{
					audibleComposite = transitionSample;
				}
			}
			else
			{
				audibleComposite = _bankIdentity[_activeBankIndex] ?
					activeSample.identity : activeSample.corrected;
			}

			if (runtimeBypass)
			{
				double residualGain = 0.0;
				if (_bypassFadeActive && _bypassFadeLength > 1)
				{
					unsigned fadeIndex = (std::min)(
						_bypassFadePosition + frame,
						_bypassFadeLength - 1);
					residualGain = 1.0 -
						static_cast<double>(fadeIndex) /
						static_cast<double>(_bypassFadeLength - 1);
				}
				outputChannel[frame] = activeSample.identity +
					residualGain * (audibleComposite - activeSample.identity);
			}
			else
			{
				outputChannel[frame] = audibleComposite;
			}
		}

		for (size_t bank = 0; bank < 2; ++bank)
		{
			for (size_t band = 0; band < _activeBandCount; ++band)
				_biquadBanks[bank][channel][band].removeDenormals();
			for (size_t section = 0; section < CROSSOVER_SECTION_COUNT; ++section)
			{
				_lowpassBanks[bank][channel][section].removeDenormals();
				_highpassBanks[bank][channel][section].removeDenormals();
			}
		}
	}

	if (_crossoverHandoffActive &&
		_crossoverDomainChannelCount >= _channelCount)
	{
		_crossoverHandoffActive = false;
	}

	if (_bypassFadeActive)
	{
		unsigned remaining = _bypassFadeLength - _bypassFadePosition;
		unsigned advanced = (std::min)(frameCount, remaining);
		_bypassFadePosition += advanced;
		if (_bypassFadePosition >= _bypassFadeLength)
		{
			// The audible residual has reached zero; only now is it safe to
			// discard a partially warmed or crossfading correction bank.
			_bypassFadeActive = false;
			_warmupActive = false;
			_crossfadeActive = false;
			_transitionFromBypass = false;
		}
	}

	if (_crossoverPrewarmActive)
	{
		unsigned remaining =
			_crossoverPrewarmLength - _crossoverPrewarmPosition;
		unsigned advanced = (std::min)(frameCount, remaining);
		_crossoverPrewarmPosition += advanced;
		if (_crossoverPrewarmPosition >= _crossoverPrewarmLength)
		{
			// Fixed LR28 histories are now ready, but raw and A = L + H can be
			// nearly opposite in the subsonic band. Each channel must first switch
			// at a bounded discrete intersection; until then it remains fail-closed
			// raw. Pending correction is installed only after every channel is in A.
			_crossoverPrewarmActive = false;
			_crossoverHandoffActive = true;
			std::fill(
				_crossoverHandoffHasPrevious.begin(),
				_crossoverHandoffHasPrevious.end(),
				0);
		}
	}
	else if (_warmupActive)
	{
		unsigned remaining = _warmupLength - _warmupPosition;
		unsigned advanced = (std::min)(frameCount, remaining);
		_warmupPosition += advanced;
		if (_warmupPosition >= _warmupLength)
		{
			_warmupActive = false;
			_crossfadePosition = 0;
			_crossfadeActive = true;
		}
	}
	else if (_crossfadeActive)
	{
		unsigned remaining = _crossfadeLength - _crossfadePosition;
		unsigned advanced = (std::min)(frameCount, remaining);
		_crossfadePosition += advanced;
		if (_crossfadePosition >= _crossfadeLength)
		{
			_activeBankIndex = _transitionBankIndex;
			_outputGainLinear = _targetOutputGainLinear;
			_crossfadeActive = false;
			_transitionFromBypass = false;
		}
	}

	_runtimeBypassWasActive = runtimeBypass;
}
#pragma AVRT_CODE_END
