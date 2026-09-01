/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2017  Alexander Walch
    Copyright (C) 2026  Equalizer APO contributors

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#pragma once

#include "LoudnessProfile.h"
#include "ParameterArchive.h"
#include <IFilter.h>
#include <filters/BiQuad.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <regex>
#include <vector>

#pragma AVRT_VTABLES_BEGIN
class LoudnessCorrectionFilter : public IFilter
{
public:
	static const size_t NUM_BANDS = LoudnessProfile::FREQUENCY_COUNT;

	struct FilterParameters
	{
		bool state;
		float referenceLevel;
		float referenceOffset;
		float attenuation;
		float manualVolume;
		bool useManualVolume;

		std::vector<char> serialize()
		{
			ParameterArchive archive;
			archive.add(state, L"State");
			archive.add(referenceLevel, L"ReferenceLevel");
			archive.add(referenceOffset, L"ReferenceOffset");
			archive.add(attenuation, L"Attenuation");
			if (useManualVolume)
				archive.add(manualVolume, L"Volume");
			return archive.getSerializedParameters();
		}

		template<typename T> bool deSerialize(const T& parameters)
		{
			ParameterArchive archive(parameters);
			int error = 0;
			error += archive.get(state, std::wregex(
				L"(?:^|\\s)State\\s+(0|1)(?=\\s|$)", std::regex_constants::icase));
			error += archive.get(referenceLevel,
				std::wregex(L"(?:^|\\s)ReferenceLevel\\s+([-+]?(?:[0-9]+(?:[\\.,][0-9]*)?|[\\.,][0-9]+))(?=\\s|$)",
					std::regex_constants::icase));
			error += archive.get(referenceOffset,
				std::wregex(L"(?:^|\\s)ReferenceOffset\\s+([-+]?(?:[0-9]+(?:[\\.,][0-9]*)?|[\\.,][0-9]+))(?=\\s|$)",
					std::regex_constants::icase));

			// Attenuation and Volume are optional for backward compatibility.
			int attenuationError = archive.get(attenuation,
				std::wregex(L"(?:^|\\s)Attenuation\\s+([-+]?(?:[0-9]+(?:[\\.,][0-9]*)?|[\\.,][0-9]+))(?=\\s|$)",
					std::regex_constants::icase));
			if (attenuationError != 0)
				attenuation = 1.0f;

			int volumeError = archive.get(manualVolume,
				std::wregex(L"(?:^|\\s)Volume\\s+([-+]?(?:[0-9]+(?:[\\.,][0-9]*)?|[\\.,][0-9]+))(?=\\s|$)",
					std::regex_constants::icase));
			useManualVolume = volumeError == 0;
			if (!useManualVolume)
				manualVolume = 0.0f;

			normalize();
			return error != 0;
		}

		FilterParameters()
			: state(true),
			  referenceLevel(80.0f),
			  referenceOffset(0.0f),
			  attenuation(1.0f),
			  manualVolume(0.0f),
			  useManualVolume(false),
			  _isInitialized(false)
		{
		}

		template<typename T> FilterParameters(T input)
			: state(true),
			  referenceLevel(80.0f),
			  referenceOffset(0.0f),
			  attenuation(1.0f),
			  manualVolume(0.0f),
			  useManualVolume(false),
			  _isInitialized(false)
		{
			_isInitialized = !deSerialize<T>(input);
		}

		bool isInitialized() const
		{
			return _isInitialized;
		}

	private:
		void normalize()
		{
			// Version 1.4.x templates used 0 here. Treat it as the documented
			// 80-phon reference so those configurations no longer stay neutral.
			if (!std::isfinite(referenceLevel) || referenceLevel <= 0.0f)
				referenceLevel = 80.0f;
			referenceLevel = (std::max)(1.0f, (std::min)(100.0f, referenceLevel));

			if (!std::isfinite(referenceOffset))
				referenceOffset = 0.0f;
			referenceOffset = (std::max)(-100.0f, (std::min)(100.0f, referenceOffset));

			if (!std::isfinite(attenuation))
				attenuation = 1.0f;
			attenuation = (std::max)(0.0f, (std::min)(1.0f, attenuation));

			if (!std::isfinite(manualVolume))
				manualVolume = 0.0f;
			manualVolume = (std::max)(-100.0f, (std::min)(0.0f, manualVolume));
		}

		bool _isInitialized;
	};

	struct BiquadCoeffs
	{
		double b0;
		double b1;
		double b2;
		double a1;
		double a2;
	};

	explicit LoudnessCorrectionFilter(const FilterParameters& fParameters);
	virtual ~LoudnessCorrectionFilter();
	virtual bool getInPlace() { return true; }
	virtual void setRuntimeContext(const FilterRuntimeContext& context) { _runtimeContext = context; }
	virtual std::vector<std::wstring> initialize(
		float sampleRate,
		unsigned maxFrameCount,
		std::vector<std::wstring> channelNames);
	virtual void process(double** output, double** input, unsigned frameCount);

private:
	friend class LoudnessCorrectionFilterTestAccess;

	static const unsigned UPDATE_POLL_INTERVAL_MS = 50;
	static const unsigned FALLBACK_POLL_INTERVAL_MS = 1000;
	static constexpr double FILTER_Q = 3.0;
	static constexpr double PI = 3.1415926535897932384626433832795;
	static constexpr double MAX_FILTER_GAIN_DB = 48.0;
	static constexpr double HEADROOM_MARGIN_DB = 1.0;
	static constexpr double COEFFICIENT_CROSSFADE_SECONDS = 0.1;
	static const unsigned FIT_ITERATIONS = 4;
	static const unsigned RESPONSE_SCAN_POINTS = 4097;
	static const unsigned RESPONSE_REFINEMENT_ITERATIONS = 32;

	void computeResponseInverse();
	void calculateBandGains(
		double currentVolumeDb,
		std::vector<double>& outGains,
		double& outputGainLinear) const;
	double calculateHeadroomGain(const std::vector<double>& gains) const;
	void publishVolumeUpdate(double currentVolumeDb, std::vector<double>& scratchGains);
	void computeBiquadCoeffs(size_t bandIndex, double gainDb, BiquadCoeffs& coeffs) const;
	double biquadResponseDb(size_t bandIndex, double gainDb, double frequency) const;

	static unsigned long __stdcall parameterUpdateThread(void* parameter);

	void* _parameterUpdateThreadHandle;
	void* _stopParameterUpdateThreadEvent;
	CRITICAL_SECTION _parameterUpdateSection;

	FilterParameters _parameters;
	FilterRuntimeContext _runtimeContext;
	std::atomic<bool> _runtimeBypass;
	std::atomic<bool> _recoveryPending;
	size_t _channelCount;
	size_t _activeBandCount;
	float _sampleRate;

	// Both banks are allocated during initialize(). A new bank starts with
	// cleared state, warms silently, and then crossfades without allocating.
	std::vector<std::vector<BiQuad>> _biquadBanks[2]; // [bank][channel][band]
	size_t _activeBankIndex;
	size_t _transitionBankIndex;
	unsigned _warmupPosition;
	unsigned _crossfadePosition;
	unsigned _crossfadeLength;
	bool _warmupActive;
	bool _crossfadeActive;
	bool _transitionFromBypass;
	double _inverseResponseMatrix[NUM_BANDS][NUM_BANDS];
	BiquadCoeffs _pendingCoeffs[NUM_BANDS];
	double _outputGainLinear;
	double _targetOutputGainLinear;
	double _pendingOutputGainLinear;
	std::atomic<bool> _coeffsUpdated;
};
#pragma AVRT_VTABLES_END
