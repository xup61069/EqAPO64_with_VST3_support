/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2017  Alexander Walch
    Enhanced with robust endpoint tracking for loudness correction.
*/

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <EndpointVolume.h>
#include <atomic>
#include <string>

class EndpointVolumeCallback;

class VolumeController
{
public:
	explicit VolumeController(const std::wstring& endpointId = L"");
	~VolumeController();
	HRESULT getVolume(double& currentVolume);
	HRESULT setVolume(double volume);
	bool hasVolumeChanged();
	const std::wstring& getEndpointId() const { return _endpointId; }

private:
	bool initEndpoint();
	void refreshEndpointIfChanged();
	void cleanup();

	IAudioEndpointVolume* _endpointVolume;
	EndpointVolumeCallback* _callback;
	float _minVol;
	float _maxVol;
	bool _comInitialized;
	std::atomic<bool> _volumeChanged;
	double _lastVolume;
	std::wstring _requestedEndpointId;
	std::wstring _endpointId;
	ULONGLONG _nextEndpointCheck;
};
