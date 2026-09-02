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
	  _runtimeBypass(false),
	  _recoveryPending(false),
	  _channelCount(0),
	  _activeBandCount(0),
	  _sampleRate(48000.0f),
	  _activeBankIndex(0),
	  _transitionBankIndex(1),
	  _warmupPosition(0),
	  _crossfadePosition(0),
	  _crossfadeLength(4800),
	  _warmupActive(false),
	  _crossfadeActive(false),
	  _transitionFromBypass(false),
	  _pendingCoeffs{},
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
	_channelCount = channelNames.size();
	_activeBandCount = 0;
	for (size_t bank = 0; bank < 2; ++bank)
		_biquadBanks[bank].clear();
	_activeBankIndex = 0;
	_transitionBankIndex = 1;
	_warmupPosition = 0;
	_crossfadePosition = 0;
	_warmupActive = false;
	_crossfadeActive = false;
	_transitionFromBypass = false;
	_coeffsUpdated.store(false, std::memory_order_relaxed);
	_sampleRate = std::isfinite(sampleRate) && sampleRate >= 8000.0f ? sampleRate : 48000.0f;
	_crossfadeLength = (std::max)(1u, static_cast<unsigned>(
		std::lround(_sampleRate * COEFFICIENT_CROSSFADE_SECONDS)));

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
		if (_runtimeContext.isCapture || _runtimeContext.endpointId.empty())
		{
			_runtimeBypass.store(true, std::memory_order_relaxed);
			LogF(L"LoudnessCorrection automatic volume mode is unavailable for this endpoint; filter is bypassed. Use manual volume mode.");
		}
		else
		{
			VolumeController volumeController(_runtimeContext.endpointId);
			if (FAILED(volumeController.getVolume(initialVolume)))
			{
				_runtimeBypass.store(true, std::memory_order_relaxed);
				initialVolume = 0.0;
				LogF(L"LoudnessCorrection could not read the configured endpoint volume; filter is bypassed until the endpoint recovers.");
			}
		}
	}

	std::vector<double> gains;
	calculateBandGains(initialVolume, gains, _outputGainLinear);
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

	// Manual mode is immutable for the lifetime of a filter instance, so it
	// needs no polling thread. A configuration edit creates a new instance.
	if (_parameters.state && !_parameters.useManualVolume &&
		!_runtimeContext.isCapture && !_runtimeContext.endpointId.empty())
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

	double maximumFrequency = (std::min)(
		20000.0,
		0.499 * static_cast<double>(_sampleRate));
	if (maximumFrequency <= 20.0)
		return 1.0;

	const double minimumFrequency = 20.0;
	const double logMinimum = std::log(minimumFrequency);
	const double logMaximum = std::log(maximumFrequency);
	const double logStep = (logMaximum - logMinimum) /
		static_cast<double>(RESPONSE_SCAN_POINTS - 1);
	std::vector<double> responses(RESPONSE_SCAN_POINTS, 0.0);

	auto responseAtLogFrequency = [this, &gains](double logFrequency)
	{
		double response = 0.0;
		double frequency = std::exp(logFrequency);
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

	if (maximumResponse <= 0.0)
		return 1.0;
	return std::pow(10.0, -(maximumResponse + HEADROOM_MARGIN_DB) / 20.0);
}

void LoudnessCorrectionFilter::publishVolumeUpdate(
	double currentVolumeDb,
	std::vector<double>& scratchGains)
{
	double outputGainLinear = 1.0;
	calculateBandGains(currentVolumeDb, scratchGains, outputGainLinear);

	EnterCriticalSection(&_parameterUpdateSection);
	for (size_t band = 0; band < _activeBandCount; ++band)
		computeBiquadCoeffs(band, scratchGains[band], _pendingCoeffs[band]);
	_pendingOutputGainLinear = outputGainLinear;
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

double LoudnessCorrectionFilter::biquadResponseDb(
	size_t bandIndex,
	double gainDb,
	double frequency) const
{
	BiquadCoeffs coefficients;
	computeBiquadCoeffs(bandIndex, gainDb, coefficients);

	double omega = 2.0 * PI * frequency / _sampleRate;
	double cosine = std::cos(omega);
	double sine = std::sin(omega);
	double cosine2 = std::cos(2.0 * omega);
	double sine2 = std::sin(2.0 * omega);

	double numeratorReal =
		coefficients.b0 + coefficients.b1 * cosine + coefficients.b2 * cosine2;
	double numeratorImaginary =
		-coefficients.b1 * sine - coefficients.b2 * sine2;
	double denominatorReal =
		1.0 + coefficients.a1 * cosine + coefficients.a2 * cosine2;
	double denominatorImaginary =
		-coefficients.a1 * sine - coefficients.a2 * sine2;

	double numeratorSquared =
		numeratorReal * numeratorReal + numeratorImaginary * numeratorImaginary;
	double denominatorSquared =
		denominatorReal * denominatorReal + denominatorImaginary * denominatorImaginary;
	if (denominatorSquared < 1.0e-30)
		denominatorSquared = 1.0e-30;
	double magnitudeSquared = (std::max)(1.0e-30, numeratorSquared / denominatorSquared);
	return 10.0 * std::log10(magnitudeSquared);
}

unsigned long __stdcall LoudnessCorrectionFilter::parameterUpdateThread(void* parameter)
{
	LoudnessCorrectionFilter* self = static_cast<LoudnessCorrectionFilter*>(parameter);
	VolumeController volumeController(self->_runtimeContext.endpointId);
	double lastVolume = std::numeric_limits<double>::quiet_NaN();
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
			// crossfade from the unfiltered signal.
			self->_recoveryPending.store(true, std::memory_order_release);
		}
		lastVolume = currentVolume;
	}
	return 0;
}

#pragma AVRT_CODE_BEGIN
void LoudnessCorrectionFilter::process(double** output, double** input, unsigned frameCount)
{
	if (!_parameters.state || _activeBandCount == 0)
	{
		for (size_t channel = 0; channel < _channelCount; ++channel)
		{
			for (unsigned frame = 0; frame < frameCount; ++frame)
				output[channel][frame] = input[channel][frame];
		}
		return;
	}

	bool runtimeBypass = _runtimeBypass.load(std::memory_order_acquire);
	if (runtimeBypass)
	{
		// A read failure can arrive in the middle of a transition. Cancel that
		// transition immediately so recovery always starts from raw passthrough.
		_warmupActive = false;
		_crossfadeActive = false;
		_transitionFromBypass = false;
	}

	bool recoveryReady = runtimeBypass &&
		_recoveryPending.load(std::memory_order_acquire);

	if ((!runtimeBypass || recoveryReady) &&
		!_warmupActive && !_crossfadeActive &&
		_coeffsUpdated.load(std::memory_order_acquire) &&
		TryEnterCriticalSection(&_parameterUpdateSection))
	{
		_transitionBankIndex = 1 - _activeBankIndex;
		for (size_t channel = 0; channel < _channelCount; ++channel)
		{
			for (size_t band = 0; band < _activeBandCount; ++band)
				_biquadBanks[_transitionBankIndex][channel][band].resetState();
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
				_biquadBanks[_transitionBankIndex][channel][band].setCoefficients(
					coefficients,
					_pendingCoeffs[band].b0);
		}
		_targetOutputGainLinear = _pendingOutputGainLinear;
		_warmupPosition = 0;
		_crossfadePosition = 0;
		_warmupActive = true;
		_crossfadeActive = false;
		_transitionFromBypass = recoveryReady;
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

	if (runtimeBypass)
	{
		for (size_t channel = 0; channel < _channelCount; ++channel)
		{
			for (unsigned frame = 0; frame < frameCount; ++frame)
				output[channel][frame] = input[channel][frame];
		}
		return;
	}

	for (size_t channel = 0; channel < _channelCount; ++channel)
	{
		double* inputChannel = input[channel];
		double* outputChannel = output[channel];
		for (unsigned frame = 0; frame < frameCount; ++frame)
		{
			if (_warmupActive)
			{
				// Feed the new bank real, headroom-adjusted input while keeping it
				// silent. This lets its zeroed state settle before the audible fade.
				double transitionSample =
					inputChannel[frame] * _targetOutputGainLinear;
				for (size_t band = 0; band < _activeBandCount; ++band)
				{
					transitionSample =
						_biquadBanks[_transitionBankIndex][channel][band]
							.process(transitionSample);
				}

				double activeSample = inputChannel[frame];
				if (!_transitionFromBypass)
				{
					activeSample *= _outputGainLinear;
					for (size_t band = 0; band < _activeBandCount; ++band)
					{
						activeSample = _biquadBanks[_activeBankIndex][channel][band]
							.process(activeSample);
					}
				}
				outputChannel[frame] = activeSample;
			}
			else if (_crossfadeActive)
			{
				double transitionSample =
					inputChannel[frame] * _targetOutputGainLinear;
				for (size_t band = 0; band < _activeBandCount; ++band)
				{
					transitionSample =
						_biquadBanks[_transitionBankIndex][channel][band]
							.process(transitionSample);
				}

				if (_crossfadePosition + frame < _crossfadeLength)
				{
					double activeSample = inputChannel[frame];
					if (!_transitionFromBypass)
					{
						activeSample *= _outputGainLinear;
						for (size_t band = 0; band < _activeBandCount; ++band)
						{
							activeSample =
								_biquadBanks[_activeBankIndex][channel][band]
									.process(activeSample);
						}
					}
					double mix =
						static_cast<double>(_crossfadePosition + frame + 1) /
						static_cast<double>(_crossfadeLength);
					outputChannel[frame] =
						activeSample * (1.0 - mix) + transitionSample * mix;
				}
				else
				{
					outputChannel[frame] = transitionSample;
				}
			}
			else
			{
				double activeSample = inputChannel[frame] * _outputGainLinear;
				for (size_t band = 0; band < _activeBandCount; ++band)
				{
					activeSample = _biquadBanks[_activeBankIndex][channel][band]
						.process(activeSample);
				}
				outputChannel[frame] = activeSample;
			}
		}

		for (size_t bank = 0; bank < 2; ++bank)
		{
			for (size_t band = 0; band < _activeBandCount; ++band)
				_biquadBanks[bank][channel][band].removeDenormals();
		}
	}

	if (_warmupActive)
	{
		unsigned remaining = _crossfadeLength - _warmupPosition;
		unsigned advanced = (std::min)(frameCount, remaining);
		_warmupPosition += advanced;
		if (_warmupPosition >= _crossfadeLength)
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
}
#pragma AVRT_CODE_END
