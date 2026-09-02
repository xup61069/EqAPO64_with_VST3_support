/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2017  Alexander Walch
    Enhanced with robust endpoint tracking for loudness correction.
*/

#include "stdafx.h"
#include "VolumeController.h"
#include <mmdeviceapi.h>
#include <algorithm>
#include <cmath>

namespace
{
	// PKEY_AudioEndpoint_GUID. The value is a device identifier, not an opaque
	// IMMDevice endpoint ID, so resolve it by enumeration and then call GetId().
	const PROPERTYKEY ENDPOINT_GUID_PROPERTY = {
		{0x1da5d803, 0xd492, 0x4edd,
			{0x8c, 0x23, 0xe0, 0xc0, 0xff, 0xee, 0x7f, 0x0e}},
		4
	};

	HRESULT findDeviceByEndpointGuid(
		IMMDeviceEnumerator* enumerator,
		const std::wstring& endpointGuid,
		IMMDevice** result)
	{
		if (enumerator == NULL || result == NULL)
			return E_POINTER;
		*result = NULL;

		IMMDeviceCollection* collection = NULL;
		HRESULT hr = enumerator->EnumAudioEndpoints(
			eRender, DEVICE_STATEMASK_ALL, &collection);
		if (FAILED(hr) || collection == NULL)
			return hr;

		UINT count = 0;
		hr = collection->GetCount(&count);
		for (UINT index = 0; SUCCEEDED(hr) && index < count; ++index)
		{
			IMMDevice* candidate = NULL;
			HRESULT itemResult = collection->Item(index, &candidate);
			if (FAILED(itemResult) || candidate == NULL)
				continue;

			IPropertyStore* properties = NULL;
			HRESULT propertyResult = candidate->OpenPropertyStore(
				STGM_READ, &properties);
			PROPVARIANT value;
			PropVariantInit(&value);
			if (SUCCEEDED(propertyResult) && properties != NULL)
				propertyResult = properties->GetValue(ENDPOINT_GUID_PROPERTY, &value);

			bool matches = SUCCEEDED(propertyResult) && value.vt == VT_LPWSTR &&
				value.pwszVal != NULL &&
				_wcsicmp(value.pwszVal, endpointGuid.c_str()) == 0;
			PropVariantClear(&value);
			if (properties != NULL)
				properties->Release();

			if (matches)
			{
				*result = candidate;
				collection->Release();
				return S_OK;
			}
			candidate->Release();
		}

		collection->Release();
		return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
	}
}

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

VolumeController::VolumeController(const std::wstring& endpointId)
	: _endpointVolume(NULL),
	  _callback(NULL),
	  _minVol(-65.0f),
	  _maxVol(0.0f),
	  _comInitialized(false),
	  _volumeChanged(true),
	  _lastVolume(0.0),
	  _requestedEndpointId(endpointId),
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

	IMMDevice* device = NULL;
	if (_requestedEndpointId.empty())
		hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
	else
	{
		// Accept a true opaque endpoint ID when one is available. The APO and
		// editor normally provide PKEY_AudioEndpoint_GUID, which must be resolved
		// without assuming an endpoint-ID string format.
		hr = deviceEnumerator->GetDevice(_requestedEndpointId.c_str(), &device);
		if (FAILED(hr) || device == NULL)
			hr = findDeviceByEndpointGuid(
				deviceEnumerator, _requestedEndpointId, &device);
	}
	deviceEnumerator->Release();
	if (FAILED(hr) || !device)
		return false;

	LPWSTR endpointId = NULL;
	hr = device->GetId(&endpointId);
	if (FAILED(hr) || !endpointId)
	{
		device->Release();
		return false;
	}

	IAudioEndpointVolume* endpointVolume = NULL;
	hr = device->Activate(
		__uuidof(IAudioEndpointVolume),
		CLSCTX_INPROC_SERVER,
		NULL,
		reinterpret_cast<LPVOID*>(&endpointVolume));
	device->Release();
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
	// A runtime filter is bound to one APO endpoint. It must never follow the
	// system default when that default changes.
	if (!_requestedEndpointId.empty())
		return;

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
