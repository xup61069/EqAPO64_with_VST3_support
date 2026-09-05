/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2017  Alexander Walch
    Copyright (C) 2026  Equalizer APO contributors

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.
*/

#include "stdafx.h"
#include "OriginalLoudnessCorrectionFilter.h"
#include "VolumeController.h"
#include "helpers/LogHelper.h"

#include <algorithm>
#include <cmath>
#include <limits>

static_assert(std::atomic<double>::is_always_lock_free,
	"Original loudness publication must stay lock-free on the audio thread.");
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
	"Original loudness sequence must stay lock-free on the audio thread.");

OriginalLoudnessCorrectionFilter::OriginalLoudnessCorrectionFilter(
	const FilterParameters& filterParameters)
	: _parameterUpdateThreadHandle(NULL),
	  _stopParameterUpdateThreadEvent(NULL),
	  _parameters(filterParameters),
	  _runtimeContext(),
	  _initialVolumeAvailable(false),
	  _initialVolumeDb(0.0),
	  _sampleRate(48000.0f),
	  _channelCount(0),
	  _bankSnapshots{},
	  _activeBankIndex(0),
	  _transitionBankIndex(1),
	  _warmupPosition(0),
	  _warmupLength(1200),
	  _crossfadePosition(0),
	  _crossfadeLength(480),
	  _warmupActive(false),
	  _crossfadeActive(false),
	  _installedSequence(0),
	  _publishedSequence(0),
	  _publishedLowShelf{},
	  _publishedLowShelfA0(1.0),
	  _publishedHighShelf{},
	  _publishedHighShelfA0(1.0),
	  _publishedOutputGainLinear(1.0),
	  _publishedIdentity(true)
{
	if (!std::isfinite(_parameters.referenceLevel))
		_parameters.referenceLevel = 0.0f;
	if (!std::isfinite(_parameters.referenceOffset))
		_parameters.referenceOffset = 0.0f;
	if (!std::isfinite(_parameters.attenuation))
		_parameters.attenuation = 1.0f;
	_parameters.referenceLevel = (std::max)(-999.0f,
		(std::min)(999.0f, _parameters.referenceLevel));
	_parameters.referenceOffset = (std::max)(-999.0f,
		(std::min)(999.0f, _parameters.referenceOffset));
	_parameters.attenuation = (std::max)(0.0f,
		(std::min)(1.0f, _parameters.attenuation));

	const CoefficientSnapshot identity = identitySnapshot();
	_bankSnapshots[0] = identity;
	_bankSnapshots[1] = identity;
}

OriginalLoudnessCorrectionFilter::~OriginalLoudnessCorrectionFilter()
{
	stopWorker();
}

void OriginalLoudnessCorrectionFilter::stopWorker()
{
	if (_stopParameterUpdateThreadEvent != NULL)
		SetEvent(static_cast<HANDLE>(_stopParameterUpdateThreadEvent));
	if (_parameterUpdateThreadHandle != NULL)
	{
		WaitForSingleObject(
			static_cast<HANDLE>(_parameterUpdateThreadHandle), INFINITE);
		CloseHandle(static_cast<HANDLE>(_parameterUpdateThreadHandle));
		_parameterUpdateThreadHandle = NULL;
	}
	if (_stopParameterUpdateThreadEvent != NULL)
	{
		CloseHandle(static_cast<HANDLE>(_stopParameterUpdateThreadEvent));
		_stopParameterUpdateThreadEvent = NULL;
	}
}

OriginalLoudnessCorrectionFilter::Transfer
OriginalLoudnessCorrectionFilter::calculateTransfer(
	const FilterParameters& parameters,
	double volumeDb)
{
	Transfer result = { 0.0, 0.0, 1.0, true };
	if (!parameters.state || !std::isfinite(volumeDb) ||
		!std::isfinite(parameters.referenceLevel) ||
		!std::isfinite(parameters.referenceOffset) ||
		!std::isfinite(parameters.attenuation))
	{
		return result;
	}

	const double attenuation = (std::max)(0.0,
		(std::min)(1.0, static_cast<double>(parameters.attenuation)));
	if (attenuation == 0.0)
		return result;

	const double referenceLevel = parameters.referenceLevel;
	const double referenceOffset = parameters.referenceOffset;
	const double difference = referenceLevel - referenceOffset - volumeDb;
	double preampDb = 0.0;
	if (difference > 0.0)
	{
		result.lowShelfGainDb =
			difference * 0.55 / (1.0 - 0.55) * attenuation;
		preampDb = -result.lowShelfGainDb;
	}
	else if (difference < 0.0)
	{
		result.lowShelfGainDb = difference * 0.55 *
			std::exp(difference / 90.0) * attenuation;
		preampDb = 0.0;
	}
	else
	{
		// The released fork left preampDb uninitialized in this branch,
		// producing the reported first-update attenuation. Unity is exact.
		result.lowShelfGainDb = 0.0;
		preampDb = 0.0;
	}

	const double highDifference =
		referenceLevel - referenceOffset - (volumeDb + preampDb);
	if (highDifference > 0.0)
	{
		result.highShelfGainDb = highDifference * 0.225 *
			std::exp(-highDifference / 100.0) * attenuation;
	}
	else if (highDifference < 0.0)
	{
		result.highShelfGainDb = highDifference * 0.175 *
			std::exp(highDifference / 80.0) * attenuation;
	}
	else
	{
		result.highShelfGainDb = 0.0;
	}

	result.outputGainLinear =
		std::exp(preampDb / 6.0 * std::log(2.0));
	if (!std::isfinite(result.lowShelfGainDb) ||
		!std::isfinite(result.highShelfGainDb) ||
		!std::isfinite(result.outputGainLinear) ||
		result.outputGainLinear < 0.0)
	{
		return Transfer{ 0.0, 0.0, 1.0, true };
	}

	const double identityEpsilon =
		8.0 * std::numeric_limits<double>::epsilon();
	result.identity =
		std::abs(result.lowShelfGainDb) <= identityEpsilon &&
		std::abs(result.highShelfGainDb) <= identityEpsilon &&
		std::abs(result.outputGainLinear - 1.0) <= identityEpsilon;
	return result;
}

OriginalLoudnessCorrectionFilter::CoefficientSnapshot
OriginalLoudnessCorrectionFilter::identitySnapshot()
{
	CoefficientSnapshot snapshot = {};
	snapshot.lowShelfA0 = 1.0;
	snapshot.highShelfA0 = 1.0;
	snapshot.outputGainLinear = 1.0;
	snapshot.identity = true;
	return snapshot;
}

OriginalLoudnessCorrectionFilter::CoefficientSnapshot
OriginalLoudnessCorrectionFilter::makeSnapshot(
	const Transfer& transfer,
	double sampleRate)
{
	if (transfer.identity || !std::isfinite(sampleRate) || sampleRate <= 0.0)
		return identitySnapshot();

	CoefficientSnapshot snapshot = {};
	snapshot.lowShelfA0 = 1.0;
	snapshot.highShelfA0 = 1.0;
	snapshot.outputGainLinear = transfer.outputGainLinear;
	snapshot.identity = false;

	auto isStableSection = [](const double coefficients[4], double b0)
	{
		bool finite = std::isfinite(b0);
		for (size_t index = 0; index < 4; ++index)
			finite = finite && std::isfinite(coefficients[index]);
		if (!finite)
			return false;

		// The normalized denominator is 1 + a1*z^-1 + a2*z^-2.
		// These are the second-order Jury conditions with a small margin.
		const double a1 = coefficients[2];
		const double a2 = coefficients[3];
		const double margin = 1.0e-12;
		return 1.0 + a1 + a2 > margin &&
			1.0 - a1 + a2 > margin &&
			1.0 - a2 > margin;
	};

	BiQuad lowShelf(
		BiQuad::LOW_SHELF,
		transfer.lowShelfGainDb,
		LOW_SHELF_FREQUENCY_HZ,
		sampleRate,
		LOW_SHELF_Q,
		false);
	lowShelf.getCoefficients(snapshot.lowShelf, snapshot.lowShelfA0);
	if (!isStableSection(snapshot.lowShelf, snapshot.lowShelfA0) ||
		!std::isfinite(snapshot.outputGainLinear) ||
		snapshot.outputGainLinear < 0.0)
	{
		return identitySnapshot();
	}

	// The original transfer is fixed at 10 kHz. At or above Nyquist the
	// RBJ shelf aliases into an unstable section, so retain the original
	// low shelf/preamp and safely omit only the unrepresentable high shelf.
	if (HIGH_SHELF_FREQUENCY_HZ * 2.0 < sampleRate)
	{
		BiQuad highShelf(
			BiQuad::HIGH_SHELF,
			transfer.highShelfGainDb,
			HIGH_SHELF_FREQUENCY_HZ,
			sampleRate,
			HIGH_SHELF_Q,
			false);
		highShelf.getCoefficients(
			snapshot.highShelf, snapshot.highShelfA0);
		if (!isStableSection(
			snapshot.highShelf, snapshot.highShelfA0))
		{
			for (size_t index = 0; index < 4; ++index)
				snapshot.highShelf[index] = 0.0;
			snapshot.highShelfA0 = 1.0;
		}
	}

	return snapshot;
}

std::vector<std::wstring> OriginalLoudnessCorrectionFilter::initialize(
	float sampleRate,
	unsigned maxFrameCount,
	std::vector<std::wstring> channelNames)
{
	stopWorker();
	(void)maxFrameCount;
	_sampleRate = std::isfinite(sampleRate) && sampleRate >= 8000.0f ?
		sampleRate : 48000.0f;
	_channelCount = channelNames.size();
	_activeBankIndex = 0;
	_transitionBankIndex = 1;
	_warmupPosition = 0;
	_warmupLength = (std::max)(1u, static_cast<unsigned>(
		std::lround(_sampleRate * WARMUP_SECONDS)));
	_crossfadePosition = 0;
	_crossfadeLength = (std::max)(1u, static_cast<unsigned>(
		std::lround(_sampleRate * CROSSFADE_SECONDS)));
	_warmupActive = false;
	_crossfadeActive = false;
	_installedSequence = 0;
	_publishedSequence.store(0, std::memory_order_relaxed);

	const CoefficientSnapshot identity = identitySnapshot();
	for (size_t index = 0; index < 4; ++index)
	{
		_publishedLowShelf[index].store(
			identity.lowShelf[index], std::memory_order_relaxed);
		_publishedHighShelf[index].store(
			identity.highShelf[index], std::memory_order_relaxed);
	}
	_publishedLowShelfA0.store(identity.lowShelfA0, std::memory_order_relaxed);
	_publishedHighShelfA0.store(identity.highShelfA0, std::memory_order_relaxed);
	_publishedOutputGainLinear.store(
		identity.outputGainLinear, std::memory_order_relaxed);
	_publishedIdentity.store(identity.identity, std::memory_order_relaxed);

	for (size_t bank = 0; bank < 2; ++bank)
	{
		_lowShelfBanks[bank].assign(_channelCount, BiQuad());
		_highShelfBanks[bank].assign(_channelCount, BiQuad());
	}

	_initialVolumeAvailable = false;
	_initialVolumeDb = 0.0;
	CoefficientSnapshot initialSnapshot = identity;
	if (_parameters.state && _parameters.attenuation > 0.0f)
	{
		// An empty endpoint ID intentionally preserves the fork's binding to
		// the Windows default eRender/eMultimedia endpoint.
		VolumeController volumeController;
		EndpointVolumeState volumeState = {};
		const HRESULT result = volumeController.getVolumeState(volumeState);
		_initialVolumeAvailable = SUCCEEDED(result);
		if (_initialVolumeAvailable)
		{
			_initialVolumeDb = volumeState.levelDb;
			initialSnapshot = makeSnapshot(
				calculateTransfer(_parameters, _initialVolumeDb),
				_sampleRate);
		}
		else
		{
			LogF(L"LoudnessCorrectionOriginal could not read the default render endpoint; the filter is bypassed until it recovers.");
		}

		if (_runtimeContext.volumeObservations != nullptr)
		{
			FilterRuntimeVolumeObservation observation;
			observation.requestedEndpointId = L"";
			observation.resolvedEndpointId = volumeController.getEndpointId();
			observation.volumeDb = _initialVolumeDb;
			observation.volumeScalar = volumeState.scalar;
			observation.muted = volumeState.muted;
			observation.available = _initialVolumeAvailable;
			_runtimeContext.volumeObservations->push_back(observation);
		}
	}

	for (size_t bank = 0; bank < 2; ++bank)
		configureBank(bank, initialSnapshot, true);

	if (_parameters.state && _parameters.attenuation > 0.0f &&
		!_runtimeContext.offlineAnalysis)
	{
		_stopParameterUpdateThreadEvent =
			CreateEvent(NULL, TRUE, FALSE, NULL);
		if (_stopParameterUpdateThreadEvent != NULL)
		{
			_parameterUpdateThreadHandle = CreateThread(
				NULL, 0, &parameterUpdateThread, this, 0, NULL);
			if (_parameterUpdateThreadHandle == NULL)
			{
				CloseHandle(static_cast<HANDLE>(
					_stopParameterUpdateThreadEvent));
				_stopParameterUpdateThreadEvent = NULL;
				LogF(L"LoudnessCorrectionOriginal could not start default-endpoint tracking; the initial transfer is held.");
			}
		}
		else
		{
			LogF(L"LoudnessCorrectionOriginal could not create its tracking event; the initial transfer is held.");
		}
	}

	return channelNames;
}

void OriginalLoudnessCorrectionFilter::publishSnapshot(
	const CoefficientSnapshot& snapshot)
{
	std::uint64_t sequence =
		_publishedSequence.load(std::memory_order_seq_cst);
	if ((sequence & 1) != 0)
		++sequence;
	_publishedSequence.store(sequence + 1, std::memory_order_seq_cst);
	// Payload fields participate in the same global order as both sequence
	// markers. This keeps the seqlock coherent on ARM64 as well as x64 while
	// retaining a single lock-free sequence load on unchanged audio blocks.
	for (size_t index = 0; index < 4; ++index)
	{
		_publishedLowShelf[index].store(
			snapshot.lowShelf[index], std::memory_order_seq_cst);
		_publishedHighShelf[index].store(
			snapshot.highShelf[index], std::memory_order_seq_cst);
	}
	_publishedLowShelfA0.store(snapshot.lowShelfA0, std::memory_order_seq_cst);
	_publishedHighShelfA0.store(snapshot.highShelfA0, std::memory_order_seq_cst);
	_publishedOutputGainLinear.store(
		snapshot.outputGainLinear, std::memory_order_seq_cst);
	_publishedIdentity.store(snapshot.identity, std::memory_order_seq_cst);
	_publishedSequence.store(sequence + 2, std::memory_order_seq_cst);
}

void OriginalLoudnessCorrectionFilter::publishVolumeUpdate(double volumeDb)
{
	publishSnapshot(makeSnapshot(
		calculateTransfer(_parameters, volumeDb), _sampleRate));
}

unsigned long __stdcall
OriginalLoudnessCorrectionFilter::parameterUpdateThread(void* parameter)
{
	OriginalLoudnessCorrectionFilter* self =
		static_cast<OriginalLoudnessCorrectionFilter*>(parameter);
	VolumeController volumeController;
	bool volumeAvailable = self->_initialVolumeAvailable;
	double previousVolume = self->_initialVolumeDb;
	ULONGLONG nextFallbackRead = 0;

	while (WaitForSingleObject(
		static_cast<HANDLE>(self->_stopParameterUpdateThreadEvent),
		UPDATE_POLL_INTERVAL_MS) == WAIT_TIMEOUT)
	{
		const ULONGLONG now = GetTickCount64();
		if (!volumeController.hasVolumeChanged() && now < nextFallbackRead)
			continue;
		nextFallbackRead = now + FALLBACK_POLL_INTERVAL_MS;

		EndpointVolumeState state;
		const HRESULT result = volumeController.getVolumeState(state);
		if (FAILED(result))
		{
			if (volumeAvailable)
			{
				self->publishSnapshot(identitySnapshot());
				volumeAvailable = false;
			}
			continue;
		}

		if (!volumeAvailable || std::abs(state.levelDb - previousVolume) > 1.0e-6)
			self->publishVolumeUpdate(state.levelDb);
		previousVolume = state.levelDb;
		volumeAvailable = true;
	}

	return 0;
}

#pragma AVRT_CODE_BEGIN
bool OriginalLoudnessCorrectionFilter::snapshotsEqual(
	const CoefficientSnapshot& first,
	const CoefficientSnapshot& second)
{
	if (first.lowShelfA0 != second.lowShelfA0 ||
		first.highShelfA0 != second.highShelfA0 ||
		first.outputGainLinear != second.outputGainLinear ||
		first.identity != second.identity)
	{
		return false;
	}
	for (size_t index = 0; index < 4; ++index)
	{
		if (first.lowShelf[index] != second.lowShelf[index] ||
			first.highShelf[index] != second.highShelf[index])
		{
			return false;
		}
	}
	return true;
}

bool OriginalLoudnessCorrectionFilter::readPublishedSnapshot(
	CoefficientSnapshot& snapshot,
	std::uint64_t& sequence) const
{
	const std::uint64_t first =
		_publishedSequence.load(std::memory_order_seq_cst);
	if ((first & 1) != 0 || first == _installedSequence)
		return false;

	for (size_t index = 0; index < 4; ++index)
	{
		snapshot.lowShelf[index] =
			_publishedLowShelf[index].load(std::memory_order_seq_cst);
		snapshot.highShelf[index] =
			_publishedHighShelf[index].load(std::memory_order_seq_cst);
	}
	snapshot.lowShelfA0 =
		_publishedLowShelfA0.load(std::memory_order_seq_cst);
	snapshot.highShelfA0 =
		_publishedHighShelfA0.load(std::memory_order_seq_cst);
	snapshot.outputGainLinear =
		_publishedOutputGainLinear.load(std::memory_order_seq_cst);
	snapshot.identity = _publishedIdentity.load(std::memory_order_seq_cst);

	const std::uint64_t second =
		_publishedSequence.load(std::memory_order_seq_cst);
	if (first != second || (second & 1) != 0)
		return false;
	sequence = second;
	return true;
}

void OriginalLoudnessCorrectionFilter::configureBank(
	size_t bank,
	const CoefficientSnapshot& snapshot,
	bool resetState)
{
	_bankSnapshots[bank] = snapshot;
	for (size_t channel = 0; channel < _channelCount; ++channel)
	{
		double lowShelfCoefficients[4];
		double highShelfCoefficients[4];
		for (size_t index = 0; index < 4; ++index)
		{
			lowShelfCoefficients[index] = snapshot.lowShelf[index];
			highShelfCoefficients[index] = snapshot.highShelf[index];
		}
		_lowShelfBanks[bank][channel].setCoefficients(
			lowShelfCoefficients, snapshot.lowShelfA0);
		_highShelfBanks[bank][channel].setCoefficients(
			highShelfCoefficients, snapshot.highShelfA0);
		if (resetState)
		{
			_lowShelfBanks[bank][channel].resetState();
			_highShelfBanks[bank][channel].resetState();
		}
	}
}

void OriginalLoudnessCorrectionFilter::installPendingSnapshot()
{
	if (_warmupActive || _crossfadeActive)
		return;
	CoefficientSnapshot snapshot;
	std::uint64_t sequence = 0;
	if (!readPublishedSnapshot(snapshot, sequence))
		return;
	_installedSequence = sequence;
	if (snapshotsEqual(snapshot, _bankSnapshots[_activeBankIndex]))
		return;

	_transitionBankIndex = 1u - _activeBankIndex;
	configureBank(_transitionBankIndex, snapshot, true);
	_warmupPosition = 0;
	_crossfadePosition = 0;
	if (_channelCount == 0)
	{
		_activeBankIndex = _transitionBankIndex;
		return;
	}
	if (snapshot.identity)
		_crossfadeActive = true;
	else
		_warmupActive = true;
}

void OriginalLoudnessCorrectionFilter::processBankSegment(
	size_t bank,
	double** output,
	double** input,
	unsigned startFrame,
	unsigned frameCount)
{
	for (size_t channel = 0; channel < _channelCount; ++channel)
	{
		double* inputChannel = input[channel] + startFrame;
		double* outputChannel = output[channel] + startFrame;
		if (_bankSnapshots[bank].identity)
		{
			if (outputChannel != inputChannel)
			{
				for (unsigned frame = 0; frame < frameCount; ++frame)
					outputChannel[frame] = inputChannel[frame];
			}
			continue;
		}

		if (outputChannel != inputChannel)
		{
			for (unsigned frame = 0; frame < frameCount; ++frame)
				outputChannel[frame] = inputChannel[frame];
		}
		_lowShelfBanks[bank][channel].processBlock(outputChannel, frameCount);
		for (unsigned frame = 0; frame < frameCount; ++frame)
			outputChannel[frame] *= _bankSnapshots[bank].outputGainLinear;
		_highShelfBanks[bank][channel].processBlock(outputChannel, frameCount);
		_lowShelfBanks[bank][channel].removeDenormals();
		_highShelfBanks[bank][channel].removeDenormals();
	}
}

void OriginalLoudnessCorrectionFilter::warmTransitionBank(
	double** input,
	unsigned startFrame,
	unsigned frameCount)
{
	if (_bankSnapshots[_transitionBankIndex].identity)
		return;
	for (size_t channel = 0; channel < _channelCount; ++channel)
	{
		BiQuad& lowShelf = _lowShelfBanks[_transitionBankIndex][channel];
		BiQuad& highShelf = _highShelfBanks[_transitionBankIndex][channel];
		const double gain =
			_bankSnapshots[_transitionBankIndex].outputGainLinear;
		for (unsigned frame = 0; frame < frameCount; ++frame)
		{
			const double low = lowShelf.process(
				input[channel][startFrame + frame]);
			(void)highShelf.process(low * gain);
		}
		lowShelf.removeDenormals();
		highShelf.removeDenormals();
	}
}

void OriginalLoudnessCorrectionFilter::crossfadeSegment(
	double** output,
	double** input,
	unsigned startFrame,
	unsigned frameCount)
{
	const CoefficientSnapshot& active = _bankSnapshots[_activeBankIndex];
	const CoefficientSnapshot& target = _bankSnapshots[_transitionBankIndex];
	for (size_t channel = 0; channel < _channelCount; ++channel)
	{
		BiQuad& activeLow = _lowShelfBanks[_activeBankIndex][channel];
		BiQuad& activeHigh = _highShelfBanks[_activeBankIndex][channel];
		BiQuad& targetLow = _lowShelfBanks[_transitionBankIndex][channel];
		BiQuad& targetHigh = _highShelfBanks[_transitionBankIndex][channel];
		for (unsigned frame = 0; frame < frameCount; ++frame)
		{
			const double sample = input[channel][startFrame + frame];
			const double from = active.identity ? sample :
				activeHigh.process(activeLow.process(sample) *
					active.outputGainLinear);
			const double to = target.identity ? sample :
				targetHigh.process(targetLow.process(sample) *
					target.outputGainLinear);
			const double mix = static_cast<double>(
				_crossfadePosition + frame + 1u) /
				static_cast<double>(_crossfadeLength);
			output[channel][startFrame + frame] = from + (to - from) * mix;
		}
		if (!active.identity)
		{
			activeLow.removeDenormals();
			activeHigh.removeDenormals();
		}
		if (!target.identity)
		{
			targetLow.removeDenormals();
			targetHigh.removeDenormals();
		}
	}
}

void OriginalLoudnessCorrectionFilter::process(
	double** output,
	double** input,
	unsigned frameCount)
{
	if (!_parameters.state)
	{
		for (size_t channel = 0; channel < _channelCount; ++channel)
		{
			if (output[channel] == input[channel])
				continue;
			for (unsigned frame = 0; frame < frameCount; ++frame)
				output[channel][frame] = input[channel][frame];
		}
		return;
	}

	installPendingSnapshot();
	unsigned startFrame = 0;
	while (startFrame < frameCount)
	{
		if (_warmupActive)
		{
			const unsigned segment = (std::min)(
				frameCount - startFrame,
				_warmupLength - _warmupPosition);
			warmTransitionBank(input, startFrame, segment);
			processBankSegment(
				_activeBankIndex, output, input, startFrame, segment);
			startFrame += segment;
			_warmupPosition += segment;
			if (_warmupPosition == _warmupLength)
			{
				_warmupActive = false;
				_crossfadeActive = true;
				_crossfadePosition = 0;
			}
			continue;
		}

		if (_crossfadeActive)
		{
			const unsigned segment = (std::min)(
				frameCount - startFrame,
				_crossfadeLength - _crossfadePosition);
			crossfadeSegment(output, input, startFrame, segment);
			startFrame += segment;
			_crossfadePosition += segment;
			if (_crossfadePosition == _crossfadeLength)
			{
				_crossfadeActive = false;
				_activeBankIndex = _transitionBankIndex;
			}
			continue;
		}

		processBankSegment(
			_activeBankIndex,
			output,
			input,
			startFrame,
			frameCount - startFrame);
		startFrame = frameCount;
	}
}
#pragma AVRT_CODE_END
