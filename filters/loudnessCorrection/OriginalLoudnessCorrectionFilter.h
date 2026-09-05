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

#pragma once

#include "ParameterArchive.h"
#include <IFilter.h>
#include <filters/BiQuad.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <regex>
#include <vector>

#pragma AVRT_VTABLES_BEGIN
class OriginalLoudnessCorrectionFilter : public IFilter
{
public:
	struct FilterParameters
	{
		bool state;
		float referenceLevel;
		float referenceOffset;
		float attenuation;

		std::vector<char> serialize() const
		{
			ParameterArchive archive;
			archive.add(1, L"Schema");
			archive.add(std::wstring(L"MixomoShelfV1"), L"Model");
			archive.add(state, L"State");
			archive.add(referenceLevel, L"ReferenceLevel");
			archive.add(referenceOffset, L"ReferenceOffset");
			archive.add(attenuation, L"Attenuation");
			return archive.getSerializedParameters();
		}

		template<typename T> bool deSerialize(const T& parameters)
		{
			ParameterArchive archive(parameters);
			const std::regex_constants::syntax_option_type insensitive =
				std::regex_constants::icase;
			if (!archive.find(std::wregex(
				L"(?:^|\\s)Schema\\s+1(?=\\s|$)", insensitive)) ||
				!archive.find(std::wregex(
					L"(?:^|\\s)Model\\s+MixomoShelfV1(?=\\s|$)",
					insensitive)))
			{
				return true;
			}

			const size_t attenuationCount = archive.count(std::wregex(
				L"(?:^|\\s)Attenuation(?=\\s|$)", insensitive));
			if (archive.count(std::wregex(
					L"(?:^|\\s)Schema(?=\\s|$)", insensitive)) != 1 ||
				archive.count(std::wregex(
					L"(?:^|\\s)Model(?=\\s|$)", insensitive)) != 1 ||
				archive.count(std::wregex(
					L"(?:^|\\s)State(?=\\s|$)", insensitive)) != 1 ||
				archive.count(std::wregex(
					L"(?:^|\\s)ReferenceLevel(?=\\s|$)", insensitive)) != 1 ||
				archive.count(std::wregex(
					L"(?:^|\\s)ReferenceOffset(?=\\s|$)", insensitive)) != 1 ||
				attenuationCount != 1)
			{
				return true;
			}

			const wchar_t* number =
				L"([-+]?(?:[0-9]+(?:[\\.,][0-9]*)?|[\\.,][0-9]+))(?=\\s|$)";
			int error = 0;
			error += archive.get(state, std::wregex(
				L"(?:^|\\s)State\\s+(0|1)(?=\\s|$)", insensitive));
			error += archive.get(referenceLevel, std::wregex(
				std::wstring(L"(?:^|\\s)ReferenceLevel\\s+") + number,
				insensitive));
			error += archive.get(referenceOffset, std::wregex(
				std::wstring(L"(?:^|\\s)ReferenceOffset\\s+") + number,
				insensitive));
			error += archive.get(attenuation, std::wregex(
				std::wstring(L"(?:^|\\s)Attenuation\\s+") + number,
				insensitive));
			if (error != 0)
				return true;

			if (!std::isfinite(referenceLevel) ||
				!std::isfinite(referenceOffset) ||
				!std::isfinite(attenuation) ||
				referenceLevel < -999.0f || referenceLevel > 999.0f ||
				referenceOffset < -999.0f || referenceOffset > 999.0f ||
				attenuation < 0.0f || attenuation > 1.0f)
			{
				return true;
			}

			return false;
		}

		FilterParameters()
			: state(true),
			  referenceLevel(0.0f),
			  referenceOffset(0.0f),
			  attenuation(1.0f),
			  _isInitialized(false)
		{
		}

		template<typename T> explicit FilterParameters(const T& input)
			: state(true),
			  referenceLevel(0.0f),
			  referenceOffset(0.0f),
			  attenuation(1.0f),
			  _isInitialized(false)
		{
			_isInitialized = !deSerialize(input);
		}

		bool isInitialized() const
		{
			return _isInitialized;
		}

	private:
		bool _isInitialized;
	};

	explicit OriginalLoudnessCorrectionFilter(
		const FilterParameters& filterParameters);
	virtual ~OriginalLoudnessCorrectionFilter();
	virtual bool getInPlace() { return true; }
	virtual void setRuntimeContext(const FilterRuntimeContext& context)
	{
		_runtimeContext = context;
	}
	virtual std::vector<std::wstring> initialize(
		float sampleRate,
		unsigned maxFrameCount,
		std::vector<std::wstring> channelNames);
	virtual void process(double** output, double** input, unsigned frameCount);

private:
	friend class OriginalLoudnessCorrectionFilterTestAccess;

	struct Transfer
	{
		double lowShelfGainDb;
		double highShelfGainDb;
		double outputGainLinear;
		bool identity;
	};

	struct CoefficientSnapshot
	{
		double lowShelf[4];
		double lowShelfA0;
		double highShelf[4];
		double highShelfA0;
		double outputGainLinear;
		bool identity;
	};

	static const unsigned UPDATE_POLL_INTERVAL_MS = 50;
	static const unsigned FALLBACK_POLL_INTERVAL_MS = 1000;
	static constexpr double LOW_SHELF_FREQUENCY_HZ = 75.0;
	static constexpr double LOW_SHELF_Q = 0.52;
	static constexpr double HIGH_SHELF_FREQUENCY_HZ = 10000.0;
	static constexpr double HIGH_SHELF_Q = 0.9;
	static constexpr double WARMUP_SECONDS = 0.025;
	static constexpr double CROSSFADE_SECONDS = 0.01;

	static Transfer calculateTransfer(
		const FilterParameters& parameters,
		double volumeDb);
	static CoefficientSnapshot identitySnapshot();
	static CoefficientSnapshot makeSnapshot(
		const Transfer& transfer,
		double sampleRate);
	static bool snapshotsEqual(
		const CoefficientSnapshot& first,
		const CoefficientSnapshot& second);

	void stopWorker();
	void publishSnapshot(const CoefficientSnapshot& snapshot);
	void publishVolumeUpdate(double volumeDb);
	bool readPublishedSnapshot(
		CoefficientSnapshot& snapshot,
		std::uint64_t& sequence) const;
	void installPendingSnapshot();
	void configureBank(
		size_t bank,
		const CoefficientSnapshot& snapshot,
		bool resetState);
	void processBankSegment(
		size_t bank,
		double** output,
		double** input,
		unsigned startFrame,
		unsigned frameCount);
	void warmTransitionBank(
		double** input,
		unsigned startFrame,
		unsigned frameCount);
	void crossfadeSegment(
		double** output,
		double** input,
		unsigned startFrame,
		unsigned frameCount);
	static unsigned long __stdcall parameterUpdateThread(void* parameter);

	void* _parameterUpdateThreadHandle;
	void* _stopParameterUpdateThreadEvent;
	FilterParameters _parameters;
	FilterRuntimeContext _runtimeContext;
	bool _initialVolumeAvailable;
	double _initialVolumeDb;
	float _sampleRate;
	size_t _channelCount;
	std::vector<BiQuad> _lowShelfBanks[2];
	std::vector<BiQuad> _highShelfBanks[2];
	CoefficientSnapshot _bankSnapshots[2];
	size_t _activeBankIndex;
	size_t _transitionBankIndex;
	unsigned _warmupPosition;
	unsigned _warmupLength;
	unsigned _crossfadePosition;
	unsigned _crossfadeLength;
	bool _warmupActive;
	bool _crossfadeActive;
	std::uint64_t _installedSequence;

	std::atomic<std::uint64_t> _publishedSequence;
	std::atomic<double> _publishedLowShelf[4];
	std::atomic<double> _publishedLowShelfA0;
	std::atomic<double> _publishedHighShelf[4];
	std::atomic<double> _publishedHighShelfA0;
	std::atomic<double> _publishedOutputGainLinear;
	std::atomic<bool> _publishedIdentity;
};
#pragma AVRT_VTABLES_END
