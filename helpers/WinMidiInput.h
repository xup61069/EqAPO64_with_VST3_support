/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2026
*/

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "VSTMidiBindingCodec.h"
#include "VSTPluginInstance.h"

struct WinMidiDeviceInfo
{
	UINT deviceId = 0;
	VSTMidiDeviceIdentity identity;
};

struct WinMidiShortMessage
{
	std::uint32_t packedMessage = 0;
	std::uint32_t timestampMs = 0;
};

enum class VSTMidiConnectionState : std::uint8_t
{
	Stopped,
	Connecting,
	Connected,
	DeviceUnavailable,
	Busy,
	BrokerCapacityExceeded
};

class WinMidiInputBroker;

class WinMidiInput
{
public:
	WinMidiInput();
	~WinMidiInput();

	WinMidiInput(const WinMidiInput&) = delete;
	WinMidiInput& operator=(const WinMidiInput&) = delete;

	static std::vector<WinMidiDeviceInfo> enumerateDevices();
	bool start(const VSTMidiDeviceIdentity& identity);
	void stop();
	bool tryPop(WinMidiShortMessage& message);
	bool isConnected() const { return connectionState() == VSTMidiConnectionState::Connected; }
	VSTMidiConnectionState connectionState() const { return state.load(std::memory_order_acquire); }
	std::uint32_t droppedMessageCount() const { return droppedMessages.load(std::memory_order_relaxed); }
	// A Busy/DeviceUnavailable broker session remains subscribed and retries at
	// this interval. Busy commonly means another process owns a single-client
	// WinMM driver; same-process consumers share one broker session instead.
	static constexpr std::uint32_t reconnectIntervalMs = 1000;

private:
	class ShortMessageQueue
	{
	public:
		bool tryPush(const WinMidiShortMessage& message);
		bool tryPop(WinMidiShortMessage& message);
		void clear();

	private:
		static constexpr std::uint32_t capacity = 1024;
		static_assert((capacity & (capacity - 1)) == 0, "MIDI queue capacity must be a power of two");
		static_assert(std::atomic<std::uint32_t>::is_always_lock_free, "MIDI callback queue requires lock-free atomics");
		std::array<WinMidiShortMessage, capacity> messages = {};
		std::atomic<std::uint32_t> readIndex{ 0 };
		std::atomic<std::uint32_t> writeIndex{ 0 };
	};

	friend class WinMidiInputBroker;
	void publishFromBroker(const WinMidiShortMessage& message);
	void setConnectionStateFromBroker(VSTMidiConnectionState value);

	ShortMessageQueue queue;
	std::atomic<VSTMidiConnectionState> state{ VSTMidiConnectionState::Stopped };
	std::atomic<std::uint32_t> droppedMessages{ 0 };
	std::uint32_t brokerSessionIndex = UINT32_MAX;
	std::uint32_t brokerSubscriberIndex = UINT32_MAX;
};

struct VSTMidiParameterUpdate
{
	const VSTParameterDescriptor* parameter = nullptr;
	double normalizedValue = 0.0;
};

// Owns decoded/resolved mappings and the WinMM device. configure() is a
// non-realtime operation; tryPopParameterUpdate() performs bounded work and
// does not allocate, lock or call WinMM.
class VSTMidiRuntime
{
public:
	bool configure(const std::wstring& encodedConfiguration, const std::vector<VSTParameterDescriptor>& parameters);
	void stop();
	bool tryPopParameterUpdate(VSTMidiParameterUpdate& update);
	bool isEnabled() const { return !bindings.empty(); }
	VSTMidiConnectionState connectionState() const { return input.connectionState(); }

private:
	struct ResolvedBinding
	{
		VSTMidiBinding binding;
		VSTParameterDescriptor parameter;
		bool inputHigh = false;
		bool toggleHigh = false;
	};
	struct PendingUpdate
	{
		std::uint16_t bindingIndex = 0;
		double normalizedValue = 0.0;
	};

	bool fillPendingUpdates(const WinMidiShortMessage& message);
	std::vector<ResolvedBinding> bindings;
	std::array<PendingUpdate, VST_MIDI_MAX_BINDINGS> pendingUpdates = {};
	std::size_t pendingUpdateCount = 0;
	std::size_t pendingUpdateIndex = 0;
	WinMidiInput input;
};
