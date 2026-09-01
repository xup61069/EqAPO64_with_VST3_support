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

#include <algorithm>
#include <cmath>
#include <limits>

LoudnessCorrectionFilter::LoudnessCorrectionFilter(const FilterParameters& fParameters)
	: _parameterUpdateThreadHandle(NULL),
	  _stopParameterUpdateThreadEvent(NULL),
	  _parameters(fParameters),
	  _channelCount(0),
	  _activeBandCount(0),
	  _sampleRate(48000.0f),
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

	_channelCount = channelNames.size();
	_activeBandCount = 0;
	_biquadBanks.clear();
	_coeffsUpdated.store(false, std::memory_order_relaxed);
	_sampleRate = std::isfinite(sampleRate) && sampleRate >= 8000.0f ? sampleRate : 48000.0f;

	// Frequencies at or above 90% of Nyquist are not representable reliably.
	double maximumCenterFrequency = 0.45 * static_cast<double>(_sampleRate);
	while (_activeBandCount < NUM_BANDS &&
		LoudnessProfile::LOUDNESS_PROFILE_TABLE[_activeBandCount].frequency <= maximumCenterFrequency)
	{
		++_activeBandCount;
	}

	_biquadBanks.resize(_channelCount);
	for (size_t channel = 0; channel < _channelCount; ++channel)
	{
		_biquadBanks[channel].reserve(_activeBandCount);
		for (size_t band = 0; band < _activeBandCount; ++band)
		{
			_biquadBanks[channel].push_back(BiQuad(
				BiQuad::PEAKING,
				0.0,
				LoudnessProfile::LOUDNESS_PROFILE_TABLE[band].frequency,
				_sampleRate,
				FILTER_Q,
				false));
		}
	}

	computeResponseInverse();

	double initialVolume = 0.0;
	if (_parameters.useManualVolume)
	{
		initialVolume = _parameters.manualVolume;
	}
	else
	{
		VolumeController volumeController;
		if (FAILED(volumeController.getVolume(initialVolume)))
			initialVolume = 0.0;
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
		for (size_t channel = 0; channel < _channelCount; ++channel)
			_biquadBanks[channel][band].setCoefficients(coefficients, _pendingCoeffs[band].b0);
	}

	// Manual mode is immutable for the lifetime of a filter instance, so it
	// needs no polling thread. A configuration edit creates a new instance.
	if (_parameters.state && !_parameters.useManualVolume)
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
			}
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

	double maximumFrequency = (std::min)(
		20000.0,
		0.45 * static_cast<double>(_sampleRate));
	if (maximumFrequency <= 20.0)
		return 1.0;

	double maximumResponse = 0.0;
	double ratio = maximumFrequency / 20.0;
	for (unsigned point = 0; point < RESPONSE_SCAN_POINTS; ++point)
	{
		double position = static_cast<double>(point) /
			static_cast<double>(RESPONSE_SCAN_POINTS - 1);
		double frequency = 20.0 * std::pow(ratio, position);
		double response = 0.0;
		for (size_t band = 0; band < _activeBandCount; ++band)
			response += biquadResponseDb(band, gains[band], frequency);
		maximumResponse = (std::max)(maximumResponse, response);
	}

	if (maximumResponse <= 0.0)
		return 1.0;
	return std::pow(10.0, -(maximumResponse + HEADROOM_MARGIN_DB) / 20.0);
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
	VolumeController volumeController;
	double lastVolume = std::numeric_limits<double>::quiet_NaN();
	ULONGLONG lastReadTime = 0;
	std::vector<double> gains;
	double outputGainLinear = 1.0;

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
			continue;

		if (std::isfinite(lastVolume) && std::abs(currentVolume - lastVolume) <= 0.05)
			continue;

		self->calculateBandGains(currentVolume, gains, outputGainLinear);
		EnterCriticalSection(&self->_parameterUpdateSection);
		for (size_t band = 0; band < self->_activeBandCount; ++band)
			self->computeBiquadCoeffs(band, gains[band], self->_pendingCoeffs[band]);
		self->_pendingOutputGainLinear = outputGainLinear;
		self->_coeffsUpdated.store(true, std::memory_order_release);
		LeaveCriticalSection(&self->_parameterUpdateSection);
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

	if (_coeffsUpdated.load(std::memory_order_acquire) &&
		TryEnterCriticalSection(&_parameterUpdateSection))
	{
		for (size_t band = 0; band < _activeBandCount; ++band)
		{
			double coefficients[4] = {
				_pendingCoeffs[band].b1,
				_pendingCoeffs[band].b2,
				_pendingCoeffs[band].a1,
				_pendingCoeffs[band].a2
			};
			for (size_t channel = 0; channel < _channelCount; ++channel)
				_biquadBanks[channel][band].setCoefficients(
					coefficients,
					_pendingCoeffs[band].b0);
		}
		_targetOutputGainLinear = _pendingOutputGainLinear;
		// Reducing gain must be immediate so a newly boosted contour cannot clip.
		// Increasing gain is ramped over the next block to avoid a level step.
		if (_targetOutputGainLinear < _outputGainLinear)
			_outputGainLinear = _targetOutputGainLinear;
		_coeffsUpdated.store(false, std::memory_order_release);
		LeaveCriticalSection(&_parameterUpdateSection);
	}

	double outputGainStep = frameCount == 0 ? 0.0 :
		(_targetOutputGainLinear - _outputGainLinear) / static_cast<double>(frameCount);

	for (size_t channel = 0; channel < _channelCount; ++channel)
	{
		double* inputChannel = input[channel];
		double* outputChannel = output[channel];
		double outputGain = _outputGainLinear;
		for (unsigned frame = 0; frame < frameCount; ++frame)
		{
			outputGain += outputGainStep;
			double sample = inputChannel[frame];
			for (size_t band = 0; band < _activeBandCount; ++band)
				sample = _biquadBanks[channel][band].process(sample);
			outputChannel[frame] = sample * outputGain;
		}

		for (size_t band = 0; band < _activeBandCount; ++band)
			_biquadBanks[channel][band].removeDenormals();
	}
	_outputGainLinear = _targetOutputGainLinear;
}
#pragma AVRT_CODE_END
