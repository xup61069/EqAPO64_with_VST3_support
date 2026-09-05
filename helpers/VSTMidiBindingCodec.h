/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2026

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

constexpr std::uint16_t VST_MIDI_CONFIG_VERSION = 1;
constexpr std::size_t VST_MIDI_MAX_BINDINGS = 256;
constexpr std::size_t VST_MIDI_MAX_STRING_LENGTH = 1024;
constexpr std::size_t VST_MIDI_MAX_PAYLOAD_BYTES = 64 * 1024;

enum class VSTMidiMessageType : std::uint8_t
{
	ControlChange = 1,
	Note = 2,
	PitchBend = 3
};

enum class VSTMidiValueMode : std::uint8_t
{
	Absolute = 1,
	Toggle = 2
};

enum class VSTMidiParameterType : std::uint8_t
{
	VST3ParamID = 1,
	VST2ParameterIndex = 2
};

struct VSTMidiDeviceIdentity
{
	std::uint16_t manufacturerId = 0;
	std::uint16_t productId = 0;
	std::uint32_t driverVersion = 0;
	std::uint32_t nameOrdinal = 0;
	std::wstring name;

	bool empty() const
	{
		return name.empty() && manufacturerId == 0 && productId == 0 && driverVersion == 0;
	}
};

struct VSTMidiBinding
{
	VSTMidiMessageType messageType = VSTMidiMessageType::ControlChange;
	// 0..15, or 0xff for all channels.
	std::uint8_t channel = 0xff;
	// CC or note number. Ignored for pitch bend.
	std::uint8_t controlNumber = 0;
	VSTMidiValueMode valueMode = VSTMidiValueMode::Absolute;
	VSTMidiParameterType parameterType = VSTMidiParameterType::VST3ParamID;
	// VST3 ParamID or VST2 parameter index, according to parameterType.
	std::uint32_t parameterId = 0;
	// VST2 index mappings require this name guard. It is also retained as a
	// human-readable fallback for migrated VST3 mappings.
	std::wstring parameterName;
	// VST3 ParameterInfo::stepCount (0 means continuous). VST2 mappings may
	// use a UI-supplied step count, otherwise they remain continuous.
	std::uint32_t stepCount = 0;
};

struct VSTMidiConfiguration
{
	std::uint16_t version = VST_MIDI_CONFIG_VERSION;
	VSTMidiDeviceIdentity device;
	std::vector<VSTMidiBinding> bindings;
};

namespace VSTMidiBindingCodec
{
	namespace detail
	{
		inline bool validBinding(const VSTMidiBinding& binding)
		{
			const bool messageValid = binding.messageType == VSTMidiMessageType::ControlChange ||
				binding.messageType == VSTMidiMessageType::Note || binding.messageType == VSTMidiMessageType::PitchBend;
			const bool modeValid = binding.valueMode == VSTMidiValueMode::Absolute || binding.valueMode == VSTMidiValueMode::Toggle;
			const bool parameterValid = binding.parameterType == VSTMidiParameterType::VST3ParamID ||
				binding.parameterType == VSTMidiParameterType::VST2ParameterIndex;
			return messageValid && modeValid && parameterValid &&
				(binding.channel == 0xff || binding.channel <= 15) && binding.controlNumber <= 127 &&
				(binding.parameterType != VSTMidiParameterType::VST2ParameterIndex || !binding.parameterName.empty());
		}

		inline bool bindingsConflict(
			const VSTMidiBinding& first,
			const VSTMidiBinding& second)
		{
			if (first.messageType != second.messageType)
				return false;
			if (first.messageType != VSTMidiMessageType::PitchBend &&
				first.controlNumber != second.controlNumber)
			{
				return false;
			}
			return first.channel == second.channel || first.channel == 0xff ||
				second.channel == 0xff;
		}

		inline bool hasConflictingSource(
			const std::vector<VSTMidiBinding>& bindings,
			const VSTMidiBinding& candidate)
		{
			return std::any_of(
				bindings.begin(), bindings.end(),
				[&candidate](const VSTMidiBinding& existing) {
					return bindingsConflict(existing, candidate);
				});
		}

		inline void appendU8(std::vector<std::uint8_t>& out, std::uint8_t value)
		{
			out.push_back(value);
		}

		inline void appendU16(std::vector<std::uint8_t>& out, std::uint16_t value)
		{
			out.push_back(static_cast<std::uint8_t>(value));
			out.push_back(static_cast<std::uint8_t>(value >> 8));
		}

		inline void appendU32(std::vector<std::uint8_t>& out, std::uint32_t value)
		{
			for (unsigned shift = 0; shift < 32; shift += 8)
				out.push_back(static_cast<std::uint8_t>(value >> shift));
		}

		inline bool appendString(std::vector<std::uint8_t>& out, const std::wstring& value)
		{
			if (value.size() > VST_MIDI_MAX_STRING_LENGTH || value.size() > UINT16_MAX)
				return false;
			appendU16(out, static_cast<std::uint16_t>(value.size()));
			for (wchar_t ch : value)
				appendU16(out, static_cast<std::uint16_t>(ch));
			return true;
		}

		inline bool readU8(const std::vector<std::uint8_t>& data, std::size_t& offset, std::uint8_t& value)
		{
			if (offset >= data.size())
				return false;
			value = data[offset++];
			return true;
		}

		inline bool readU16(const std::vector<std::uint8_t>& data, std::size_t& offset, std::uint16_t& value)
		{
			if (offset + 2 > data.size())
				return false;
			value = static_cast<std::uint16_t>(data[offset]) |
				(static_cast<std::uint16_t>(data[offset + 1]) << 8);
			offset += 2;
			return true;
		}

		inline bool readU32(const std::vector<std::uint8_t>& data, std::size_t& offset, std::uint32_t& value)
		{
			if (offset + 4 > data.size())
				return false;
			value = static_cast<std::uint32_t>(data[offset]) |
				(static_cast<std::uint32_t>(data[offset + 1]) << 8) |
				(static_cast<std::uint32_t>(data[offset + 2]) << 16) |
				(static_cast<std::uint32_t>(data[offset + 3]) << 24);
			offset += 4;
			return true;
		}

		inline bool readString(const std::vector<std::uint8_t>& data, std::size_t& offset, std::wstring& value)
		{
			std::uint16_t length = 0;
			if (!readU16(data, offset, length) || length > VST_MIDI_MAX_STRING_LENGTH || offset + static_cast<std::size_t>(length) * 2 > data.size())
				return false;
			value.clear();
			value.reserve(length);
			for (std::uint16_t i = 0; i < length; ++i)
			{
				std::uint16_t ch = 0;
				if (!readU16(data, offset, ch))
					return false;
				value.push_back(static_cast<wchar_t>(ch));
			}
			return true;
		}

		inline wchar_t hexDigit(std::uint8_t value)
		{
			return value < 10 ? static_cast<wchar_t>(L'0' + value) : static_cast<wchar_t>(L'A' + value - 10);
		}

		inline int hexValue(wchar_t value)
		{
			if (value >= L'0' && value <= L'9')
				return value - L'0';
			if (value >= L'A' && value <= L'F')
				return value - L'A' + 10;
			if (value >= L'a' && value <= L'f')
				return value - L'a' + 10;
			return -1;
		}
	}

	// Shared source-collision contract for editors and serialization. In
	// particular, pitch bend has no control number, and wildcard channels
	// overlap every concrete channel.
	inline bool sourcesConflict(
		const VSTMidiBinding& first,
		const VSTMidiBinding& second)
	{
		return detail::bindingsConflict(first, second);
	}

	// Wire format: "EAMIDI1:" followed by uppercase hex for a bounded little-
	// endian binary payload. The prefix lets future codecs coexist without
	// relying on configuration-line quoting or the Windows code page.
	inline bool serialize(const VSTMidiConfiguration& config, std::wstring& encoded)
	{
		encoded.clear();
		if (config.version != VST_MIDI_CONFIG_VERSION || config.bindings.size() > VST_MIDI_MAX_BINDINGS ||
			(!config.bindings.empty() && config.device.empty()))
			return false;

		std::vector<std::uint8_t> data;
		data.reserve(64 + config.bindings.size() * 24);
		detail::appendU16(data, config.version);
		detail::appendU16(data, config.device.manufacturerId);
		detail::appendU16(data, config.device.productId);
		detail::appendU32(data, config.device.driverVersion);
		detail::appendU32(data, config.device.nameOrdinal);
		if (!detail::appendString(data, config.device.name))
			return false;
		detail::appendU16(data, static_cast<std::uint16_t>(config.bindings.size()));
		std::vector<VSTMidiBinding> validatedBindings;
		validatedBindings.reserve(config.bindings.size());
		for (const VSTMidiBinding& binding : config.bindings)
		{
			if (!detail::validBinding(binding) ||
				detail::hasConflictingSource(validatedBindings, binding))
				return false;
			validatedBindings.push_back(binding);
			detail::appendU8(data, static_cast<std::uint8_t>(binding.messageType));
			detail::appendU8(data, binding.channel);
			detail::appendU8(data, binding.controlNumber);
			detail::appendU8(data, static_cast<std::uint8_t>(binding.valueMode));
			detail::appendU8(data, static_cast<std::uint8_t>(binding.parameterType));
			detail::appendU32(data, binding.parameterId);
			detail::appendU32(data, binding.stepCount);
			if (!detail::appendString(data, binding.parameterName) || data.size() > VST_MIDI_MAX_PAYLOAD_BYTES)
				return false;
		}

		static const wchar_t prefix[] = L"EAMIDI1:";
		encoded.assign(prefix);
		encoded.reserve(encoded.size() + data.size() * 2);
		for (std::uint8_t byte : data)
		{
			encoded.push_back(detail::hexDigit(byte >> 4));
			encoded.push_back(detail::hexDigit(byte & 0x0f));
		}
		return true;
	}

	inline std::wstring serialize(const VSTMidiConfiguration& config)
	{
		std::wstring encoded;
		serialize(config, encoded);
		return encoded;
	}

	inline bool deserialize(const std::wstring& encoded, VSTMidiConfiguration& config)
	{
		static const std::wstring prefix = L"EAMIDI1:";
		if (encoded.compare(0, prefix.size(), prefix) != 0 || ((encoded.size() - prefix.size()) & 1) != 0)
			return false;
		const std::size_t byteCount = (encoded.size() - prefix.size()) / 2;
		if (byteCount > VST_MIDI_MAX_PAYLOAD_BYTES)
			return false;

		std::vector<std::uint8_t> data;
		data.reserve(byteCount);
		for (std::size_t i = prefix.size(); i < encoded.size(); i += 2)
		{
			const int hi = detail::hexValue(encoded[i]);
			const int lo = detail::hexValue(encoded[i + 1]);
			if (hi < 0 || lo < 0)
				return false;
			data.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
		}

		VSTMidiConfiguration decoded;
		std::size_t offset = 0;
		std::uint16_t bindingCount = 0;
		if (!detail::readU16(data, offset, decoded.version) || decoded.version != VST_MIDI_CONFIG_VERSION ||
			!detail::readU16(data, offset, decoded.device.manufacturerId) ||
			!detail::readU16(data, offset, decoded.device.productId) ||
			!detail::readU32(data, offset, decoded.device.driverVersion) ||
			!detail::readU32(data, offset, decoded.device.nameOrdinal) ||
			!detail::readString(data, offset, decoded.device.name) ||
			!detail::readU16(data, offset, bindingCount) || bindingCount > VST_MIDI_MAX_BINDINGS)
			return false;

		decoded.bindings.reserve(bindingCount);
		for (std::uint16_t i = 0; i < bindingCount; ++i)
		{
			VSTMidiBinding binding;
			std::uint8_t messageType = 0;
			std::uint8_t valueMode = 0;
			std::uint8_t parameterType = 0;
			if (!detail::readU8(data, offset, messageType) ||
				!detail::readU8(data, offset, binding.channel) ||
				!detail::readU8(data, offset, binding.controlNumber) ||
				!detail::readU8(data, offset, valueMode) ||
				!detail::readU8(data, offset, parameterType) ||
				!detail::readU32(data, offset, binding.parameterId) ||
				!detail::readU32(data, offset, binding.stepCount) ||
				!detail::readString(data, offset, binding.parameterName))
				return false;

			binding.messageType = static_cast<VSTMidiMessageType>(messageType);
			binding.valueMode = static_cast<VSTMidiValueMode>(valueMode);
			binding.parameterType = static_cast<VSTMidiParameterType>(parameterType);
			if (!detail::validBinding(binding) ||
				detail::hasConflictingSource(decoded.bindings, binding))
				return false;
			decoded.bindings.push_back(std::move(binding));
		}

		if (offset != data.size() || (!decoded.bindings.empty() && decoded.device.empty()))
			return false;
		config = std::move(decoded);
		return true;
	}
}
