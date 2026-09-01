/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2017  Alexander Walch
    Enhanced with robust endpoint tracking for loudness correction.
*/

#include "stdafx.h"
#include "VolumeController.h"
#include <mmdeviceapi.h>
#include <mmsystem.h>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "winmm.lib")

class EndpointVolumeCallback : public IAudioEndpointVolumeCallback
{
public:
	EndpointVolumeCallback(std::atomic<bool>* flag)
		: _refCount(1), _flag(flag) {}

	ULONG STDMETHODCALLTYPE AddRef()
	{
		return InterlockedIncrement(&_refCount);
	}

	ULONG STDMETHODCALLTYPE Release()
	{
		ULONG ulRef = InterlockedDecrement(&_refCount);
		if (ulRef == 0)
		{
			delete this;
		}
		return ulRef;
	}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, VOID** ppvInterface)
	{
		if (!ppvInterface)
			return E_POINTER;

		*ppvInterface = NULL;
		if (IsEqualIID(riid, __uuidof(IUnknown)))
		{
			AddRef();
			*ppvInterface = static_cast<IUnknown*>(this);
			return S_OK;
		}
		if (IsEqualIID(riid, __uuidof(IAudioEndpointVolumeCallback)))
		{
			AddRef();
			*ppvInterface = static_cast<IAudioEndpointVolumeCallback*>(this);
			return S_OK;
		}
		return E_NOINTERFACE;
	}

	HRESULT STDMETHODCALLTYPE OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA pNotify)
	{
		if (_flag && pNotify)
		{
			_flag->store(true, std::memory_order_relaxed);
		}
		return S_OK;
	}

private:
	long _refCount;
	std::atomic<bool>* _flag;
};

VolumeController::VolumeController()
	: _endpointVolume(NULL),
	  _callback(NULL),
	  _minVol(-65.0f),
	  _maxVol(0.0f),
	  _comInitialized(false),
	  _volumeChanged(true),
	  _lastVolume(0.0),
	  _nextEndpointCheck(0)
{
	HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if (SUCCEEDED(hr))
	{
		_comInitialized = true;
	}
	else if (hr == RPC_E_CHANGED_MODE)
	{
		// COM already initialized on this thread with different apartment model; valid
		_comInitialized = false;
	}
	initEndpoint();
}

VolumeController::~VolumeController()
{
	cleanup();
	if (_comInitialized)
	{
		CoUninitialize();
		_comInitialized = false;
	}
}

void VolumeController::cleanup()
{
	if (_endpointVolume)
	{
		if (_callback)
		{
			_endpointVolume->UnregisterControlChangeNotify(_callback);
			_callback->Release();
			_callback = NULL;
		}
		_endpointVolume->Release();
		_endpointVolume = NULL;
	}
	else if (_callback)
	{
		_callback->Release();
		_callback = NULL;
	}
	_endpointId.clear();
}

bool VolumeController::initEndpoint()
{
	IMMDeviceEnumerator* deviceEnumerator = NULL;
	HRESULT hr = CoCreateInstance(
		__uuidof(MMDeviceEnumerator),
		NULL,
		CLSCTX_INPROC_SERVER,
		__uuidof(IMMDeviceEnumerator),
		reinterpret_cast<LPVOID*>(&deviceEnumerator));
	if (FAILED(hr) || !deviceEnumerator)
		return false;

	IMMDevice* defaultDevice = NULL;
	hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &defaultDevice);
	deviceEnumerator->Release();
	if (FAILED(hr) || !defaultDevice)
		return false;

	LPWSTR endpointId = NULL;
	hr = defaultDevice->GetId(&endpointId);
	if (FAILED(hr) || !endpointId)
	{
		defaultDevice->Release();
		return false;
	}

	IAudioEndpointVolume* endpointVolume = NULL;
	hr = defaultDevice->Activate(
		__uuidof(IAudioEndpointVolume),
		CLSCTX_INPROC_SERVER,
		NULL,
		reinterpret_cast<LPVOID*>(&endpointVolume));
	defaultDevice->Release();
	if (FAILED(hr) || !endpointVolume)
	{
		CoTaskMemFree(endpointId);
		return false;
	}

	float minimumVolume = -65.0f;
	float maximumVolume = 0.0f;
	float increment = 0.0f;
	endpointVolume->GetVolumeRange(&minimumVolume, &maximumVolume, &increment);

	EndpointVolumeCallback* callback = new EndpointVolumeCallback(&_volumeChanged);
	if (FAILED(endpointVolume->RegisterControlChangeNotify(callback)))
	{
		callback->Release();
		callback = NULL;
	}

	std::wstring newEndpointId(endpointId);
	CoTaskMemFree(endpointId);
	cleanup();
	_endpointVolume = endpointVolume;
	_callback = callback;
	_minVol = minimumVolume;
	_maxVol = maximumVolume;
	_endpointId = newEndpointId;
	_nextEndpointCheck = GetTickCount64() + 2000;
	_volumeChanged.store(true, std::memory_order_relaxed);

	return true;
}

void VolumeController::refreshEndpointIfChanged()
{
	ULONGLONG now = GetTickCount64();
	if (now < _nextEndpointCheck)
		return;
	_nextEndpointCheck = now + 2000;

	IMMDeviceEnumerator* deviceEnumerator = NULL;
	HRESULT hr = CoCreateInstance(
		__uuidof(MMDeviceEnumerator),
		NULL,
		CLSCTX_INPROC_SERVER,
		__uuidof(IMMDeviceEnumerator),
		reinterpret_cast<LPVOID*>(&deviceEnumerator));
	if (FAILED(hr) || !deviceEnumerator)
		return;

	IMMDevice* defaultDevice = NULL;
	hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &defaultDevice);
	deviceEnumerator->Release();
	if (FAILED(hr) || !defaultDevice)
		return;

	LPWSTR endpointId = NULL;
	hr = defaultDevice->GetId(&endpointId);
	defaultDevice->Release();
	if (FAILED(hr) || !endpointId)
		return;

	bool changed = _endpointId != endpointId;
	CoTaskMemFree(endpointId);
	if (changed)
		initEndpoint();
}

HRESULT VolumeController::getVolume(double& currentVolume)
{
	refreshEndpointIfChanged();
	if (!_endpointVolume)
	{
		if (!initEndpoint())
		{
			// Fallback to legacy waveOutGetVolume
			DWORD dwVol = 0;
			if (waveOutGetVolume(0, &dwVol) == MMSYSERR_NOERROR)
			{
				WORD left = LOWORD(dwVol);
				WORD right = HIWORD(dwVol);
				double maxChan = static_cast<double>((std::max)(left, right)) / 65535.0;
				if (maxChan > 1e-4)
				{
					currentVolume = 20.0 * log10(maxChan);
				}
				else
				{
					currentVolume = -65.0;
				}
				_lastVolume = currentVolume;
				return S_OK;
			}
			currentVolume = _lastVolume;
			return E_FAIL;
		}
	}

	float vol = 0.0f;
	HRESULT res = _endpointVolume->GetMasterVolumeLevel(&vol);
	if (FAILED(res) && initEndpoint())
		res = _endpointVolume->GetMasterVolumeLevel(&vol);
	if (SUCCEEDED(res))
	{
		currentVolume = vol;
		_lastVolume = vol;
		return S_OK;
	}

	// Fallback to Scalar volume if dB reporting is unsupported by driver
	float scalar = 1.0f;
	res = _endpointVolume->GetMasterVolumeLevelScalar(&scalar);
	if (SUCCEEDED(res))
	{
		if (scalar > 1e-4f)
		{
			currentVolume = 20.0 * log10((double)scalar);
		}
		else
		{
			currentVolume = -65.0;
		}
		_lastVolume = currentVolume;
		return S_OK;
	}

	currentVolume = _lastVolume;
	return res;
}

HRESULT VolumeController::setVolume(double volume)
{
	refreshEndpointIfChanged();
	if (!_endpointVolume)
	{
		if (!initEndpoint())
		{
			return E_FAIL;
		}
	}
	volume = (std::min)(volume, static_cast<double>(_maxVol));
	volume = (std::max)(volume, static_cast<double>(_minVol));
	return _endpointVolume->SetMasterVolumeLevel((float)volume, NULL);
}

bool VolumeController::hasVolumeChanged()
{
	return _volumeChanged.exchange(false, std::memory_order_relaxed);
}
