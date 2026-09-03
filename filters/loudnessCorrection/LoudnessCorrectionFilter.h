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
#include <complex>
#include <regex>
#include <vector>

#pragma AVRT_VTABLES_BEGIN
class LoudnessCorrectionFilter : public IFilter
{
public:
	static const size_t NUM_BANDS = LoudnessProfile::FREQUENCY_COUNT;

	struct FilterParameters
	{
		enum BindingMode
		{
			BINDING_SINGLE = 0,
			BINDING_ALL = 1
		};

		bool state;
		float referenceLevel;
		float referenceOffset;
		float attenuation;
		float manualVolume;
		bool useManualVolume;
		BindingMode binding;

		std::vector<char> serialize()
		{
			ParameterArchive archive;
			archive.add(1, L"Schema");
			archive.add(std::wstring(L"FormulaLoudnessV1"), L"Model");
			archive.add(std::wstring(
				binding == BINDING_ALL ? L"All" : L"Single"), L"Binding");
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
			const bool isFormulaSchema = archive.find(std::wregex(
				L"(?:^|\\s)Schema\\s+1(?=\\s|$)", std::regex_constants::icase));
			const bool isFormulaModel = archive.find(std::wregex(
				L"(?:^|\\s)Model\\s+FormulaLoudnessV1(?=\\s|$)",
				std::regex_constants::icase));

			// Only an explicit, complete marker can select this model. Older
			// unmarked formula settings overlap the original Mixomo shelf syntax,
			// so the editor must ask the user which interpretation to preserve.
			if (!(isFormulaSchema && isFormulaModel))
				return true;
			size_t bindingCount = archive.count(std::wregex(
				L"(?:^|\\s)Binding(?=\\s|$)", std::regex_constants::icase));
			if (archive.count(std::wregex(L"(?:^|\\s)Schema(?=\\s|$)",
					std::regex_constants::icase)) != 1 ||
				archive.count(std::wregex(L"(?:^|\\s)Model(?=\\s|$)",
					std::regex_constants::icase)) != 1 ||
				archive.count(std::wregex(L"(?:^|\\s)State(?=\\s|$)",
					std::regex_constants::icase)) != 1 ||
				archive.count(std::wregex(L"(?:^|\\s)ReferenceLevel(?=\\s|$)",
					std::regex_constants::icase)) != 1 ||
				archive.count(std::wregex(L"(?:^|\\s)ReferenceOffset(?=\\s|$)",
					std::regex_constants::icase)) != 1 ||
				bindingCount > 1 ||
				archive.count(std::wregex(L"(?:^|\\s)Attenuation(?=\\s|$)",
					std::regex_constants::icase)) > 1 ||
				archive.count(std::wregex(L"(?:^|\\s)Volume(?=\\s|$)",
					std::regex_constants::icase)) > 1)
			{
				return true;
			}
			if (bindingCount == 0)
			{
				// Schema 1 profiles written before Binding was introduced used
				// exact APO-endpoint tracking.
				binding = BINDING_SINGLE;
			}
			else if (archive.find(std::wregex(
				L"(?:^|\\s)Binding\\s+All(?=\\s|$)",
				std::regex_constants::icase)))
			{
				binding = BINDING_ALL;
			}
			else if (archive.find(std::wregex(
				L"(?:^|\\s)Binding\\s+Single(?=\\s|$)",
				std::regex_constants::icase)))
			{
				binding = BINDING_SINGLE;
			}
			else
			{
				return true;
			}

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

			if (error != 0)
				return true;

			// Invalid levels fail closed instead of being normalized into a
			// different audible setting.
			if (referenceLevel <= 0.0f)
				return true;

			normalize();
			return false;
		}

		FilterParameters()
			: state(true),
			  referenceLevel(80.0f),
			  referenceOffset(0.0f),
			  attenuation(1.0f),
			  manualVolume(0.0f),
			  useManualVolume(false),
			  binding(BINDING_SINGLE),
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
			  binding(BINDING_SINGLE),
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
			if (!std::isfinite(referenceLevel) || referenceLevel <= 0.0f)
				referenceLevel = 1.0f;
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
	static constexpr double FILTER_Q = 2.2;
	static constexpr double HIGH_SHELF_Q = 0.9;
	static constexpr double PI = 3.1415926535897932384626433832795;
	static constexpr double MAX_FILTER_GAIN_DB = 48.0;
	static constexpr double HEADROOM_MARGIN_DB = 1.0;
	static constexpr double FINAL_RESPONSE_NUMERICAL_TOLERANCE_DB = 1.0e-6;
	static constexpr double SUBSONIC_CROSSOVER_FREQUENCY_HZ = 25.0;
	static const size_t CROSSOVER_SECTION_COUNT = 14;
	static const unsigned CROSSOVER_BUTTERWORTH_ORDER = 14;
	static constexpr double CROSSOVER_HISTORY_PREWARM_SECONDS = 1.0;
	static constexpr double BYPASS_FADE_SECONDS = 0.01;
	static constexpr double FILTER_WARMUP_SECONDS = 0.25;
	static constexpr double COEFFICIENT_CROSSFADE_SECONDS = 0.1;
	static const unsigned FIT_ITERATIONS = 4;
	static const unsigned RESPONSE_SCAN_POINTS = 4097;
	static const unsigned RESPONSE_REFINEMENT_ITERATIONS = 32;

	void computeResponseInverse();
	static bool isSafeCrossoverHandoff(
		double previousRaw,
		double currentRaw,
		double previousIdentity,
		double currentIdentity,
		double& handoffStep,
		double& naturalStep);
	bool canTrackAutomaticVolume() const;
	std::wstring getVolumeControllerEndpointId() const;
	void calculateBandGains(
		double currentVolumeDb,
		std::vector<double>& outGains,
		double& outputGainLinear) const;
	double calculateHeadroomGain(const std::vector<double>& gains) const;
	void publishVolumeUpdate(double currentVolumeDb, std::vector<double>& scratchGains);
	void computeBiquadCoeffs(size_t bandIndex, double gainDb, BiquadCoeffs& coeffs) const;
	void computeCrossoverCoeffs(
		bool highPass,
		size_t sectionIndex,
		BiquadCoeffs& coeffs) const;
	std::complex<double> biquadResponse(
		const BiquadCoeffs& coeffs,
		double frequency) const;
	double biquadResponseDb(size_t bandIndex, double gainDb, double frequency) const;
	double guardedResponseDb(
		const std::vector<double>& gains,
		double outputGainLinear,
		double frequency) const;
	double findMaximumResponseDb(
		const std::vector<double>& gains,
		double outputGainLinear,
		bool includeSubsonicCrossover) const;

	static unsigned long __stdcall parameterUpdateThread(void* parameter);

	void* _parameterUpdateThreadHandle;
	void* _stopParameterUpdateThreadEvent;
	CRITICAL_SECTION _parameterUpdateSection;

	FilterParameters _parameters;
	FilterRuntimeContext _runtimeContext;
	bool _hasInitialAutomaticVolume;
	double _initialAutomaticVolume;
	std::atomic<bool> _runtimeBypass;
	std::atomic<bool> _recoveryPending;
	size_t _channelCount;
	size_t _activeBandCount;
	float _sampleRate;

	// Both banks are allocated during initialize(). A new bank starts with
	// cleared state, warms silently, and then crossfades without allocating.
	std::vector<std::vector<BiQuad>> _biquadBanks[2]; // [bank][channel][band]
	std::vector<std::vector<BiQuad>> _lowpassBanks[2]; // [bank][channel][section]
	std::vector<std::vector<BiQuad>> _highpassBanks[2]; // [bank][channel][section]
	size_t _activeBankIndex;
	size_t _transitionBankIndex;
	unsigned _crossoverPrewarmPosition;
	unsigned _crossoverPrewarmLength;
	unsigned _warmupPosition;
	unsigned _warmupLength;
	unsigned _crossfadePosition;
	unsigned _crossfadeLength;
	unsigned _bypassFadePosition;
	unsigned _bypassFadeLength;
	bool _warmupActive;
	bool _crossfadeActive;
	bool _bypassFadeActive;
	bool _runtimeBypassWasActive;
	bool _transitionFromBypass;
	bool _crossoverPrewarmActive;
	bool _crossoverHandoffActive;
	std::vector<unsigned char> _crossoverDomainActive;
	std::vector<unsigned char> _crossoverHandoffHasPrevious;
	std::vector<double> _crossoverHandoffPreviousRaw;
	std::vector<double> _crossoverHandoffPreviousIdentity;
	size_t _crossoverDomainChannelCount;
	double _maximumCrossoverHandoffStep;
	double _inverseResponseMatrix[NUM_BANDS][NUM_BANDS];
	BiquadCoeffs _pendingCoeffs[NUM_BANDS];
	BiquadCoeffs _lowpassCoeffs[CROSSOVER_SECTION_COUNT];
	BiquadCoeffs _highpassCoeffs[CROSSOVER_SECTION_COUNT];
	bool _bankIdentity[2];
	bool _pendingIdentity;
	double _outputGainLinear;
	double _targetOutputGainLinear;
	double _pendingOutputGainLinear;
	std::atomic<bool> _coeffsUpdated;
};
#pragma AVRT_VTABLES_END
