#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

constexpr std::uint32_t OUTPROC_VST_CONFIG_MAGIC = 0x4F505653; // OPVS
constexpr std::uint32_t OUTPROC_VST_CONFIG_VERSION = 4;
constexpr std::uint32_t OUTPROC_VST_CONFIG_MAX_STRING_LENGTH = 64 * 1024 * 1024;
constexpr std::uint32_t OUTPROC_VST_CONFIG_MAX_PARAM_COUNT = 65536;
constexpr std::uint32_t OUTPROC_VST_CONFIG_MAX_DESCRIPTOR_COUNT = 16384;
constexpr std::uint32_t OUTPROC_VST_CONFIG_MAX_PARAMETER_NAME_LENGTH = 4096;
constexpr std::size_t OUTPROC_VST_CONFIG_MAX_DESCRIPTOR_NAME_BYTES = 1024 * 1024;

enum class OutProcVSTParameterApi : std::uint8_t
{
	VST2 = 0,
	VST3 = 1
};

struct OutProcVSTParameterDescriptor
{
	OutProcVSTParameterApi api = OutProcVSTParameterApi::VST2;
	std::uint32_t stableId = 0;
	std::wstring name;
	std::uint32_t stepCount = 0;
	double normalizedValue = 0.0;
	bool readOnly = false;
	bool hidden = false;
};

struct OutProcVSTConfig
{
	std::wstring libraryPath;
	int vst3ClassIndex = 0;
	std::wstring chunkData;
	std::unordered_map<std::wstring, float> paramMap;
	std::wstring midiConfig;
	std::vector<OutProcVSTParameterDescriptor> parameterDescriptors;
};

inline bool OutProcWriteString(
	std::ofstream& stream,
	const std::wstring& value,
	std::uint32_t maxLength = OUTPROC_VST_CONFIG_MAX_STRING_LENGTH)
{
	if (value.size() > maxLength)
		return false;

	const std::uint32_t length = static_cast<std::uint32_t>(value.size());
	stream.write(reinterpret_cast<const char*>(&length), sizeof(length));
	if (length > 0)
		stream.write(reinterpret_cast<const char*>(value.data()), length * sizeof(wchar_t));
	return stream.good();
}

inline bool OutProcReadString(
	std::ifstream& stream,
	std::wstring& value,
	std::uint32_t maxLength = OUTPROC_VST_CONFIG_MAX_STRING_LENGTH)
{
	std::uint32_t length = 0;
	stream.read(reinterpret_cast<char*>(&length), sizeof(length));
	if (!stream.good())
		return false;
	if (length > maxLength)
		return false;

	value.assign(length, L'\0');
	if (length > 0)
		stream.read(reinterpret_cast<char*>(&value[0]), length * sizeof(wchar_t));
	return stream.good();
}

inline bool OutProcWriteVSTConfigPayload(const std::wstring& path, const OutProcVSTConfig& config)
{
	if (config.paramMap.size() > OUTPROC_VST_CONFIG_MAX_PARAM_COUNT ||
		config.parameterDescriptors.size() > OUTPROC_VST_CONFIG_MAX_DESCRIPTOR_COUNT)
		return false;
	std::size_t descriptorNameBytes = 0;
	for (const OutProcVSTParameterDescriptor& descriptor : config.parameterDescriptors)
	{
		if (descriptor.name.size() > OUTPROC_VST_CONFIG_MAX_PARAMETER_NAME_LENGTH ||
			!std::isfinite(descriptor.normalizedValue) ||
			static_cast<std::uint8_t>(descriptor.api) >
				static_cast<std::uint8_t>(OutProcVSTParameterApi::VST3))
			return false;
		const std::size_t nameBytes = descriptor.name.size() * sizeof(wchar_t);
		if (nameBytes > OUTPROC_VST_CONFIG_MAX_DESCRIPTOR_NAME_BYTES - descriptorNameBytes)
			return false;
		descriptorNameBytes += nameBytes;
	}

	std::ofstream stream(path, std::ios::binary | std::ios::trunc);
	if (!stream)
		return false;

	const std::uint32_t magic = OUTPROC_VST_CONFIG_MAGIC;
	const std::uint32_t version = OUTPROC_VST_CONFIG_VERSION;
	const std::uint32_t paramCount = static_cast<std::uint32_t>(config.paramMap.size());

	stream.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
	stream.write(reinterpret_cast<const char*>(&version), sizeof(version));
	if (!OutProcWriteString(stream, config.libraryPath))
		return false;
	stream.write(reinterpret_cast<const char*>(&config.vst3ClassIndex), sizeof(config.vst3ClassIndex));
	if (!OutProcWriteString(stream, config.chunkData))
		return false;
	stream.write(reinterpret_cast<const char*>(&paramCount), sizeof(paramCount));

	for (const auto& entry : config.paramMap)
	{
		if (!OutProcWriteString(stream, entry.first))
			return false;
		stream.write(reinterpret_cast<const char*>(&entry.second), sizeof(entry.second));
	}
	if (!OutProcWriteString(stream, config.midiConfig))
		return false;

	const std::uint32_t descriptorCount =
		static_cast<std::uint32_t>(config.parameterDescriptors.size());
	stream.write(reinterpret_cast<const char*>(&descriptorCount), sizeof(descriptorCount));
	for (const OutProcVSTParameterDescriptor& descriptor : config.parameterDescriptors)
	{
		const std::uint8_t api = static_cast<std::uint8_t>(descriptor.api);
		const std::uint8_t readOnly = descriptor.readOnly ? 1 : 0;
		const std::uint8_t hidden = descriptor.hidden ? 1 : 0;
		stream.write(reinterpret_cast<const char*>(&api), sizeof(api));
		stream.write(reinterpret_cast<const char*>(&descriptor.stableId), sizeof(descriptor.stableId));
		if (!OutProcWriteString(
			stream, descriptor.name, OUTPROC_VST_CONFIG_MAX_PARAMETER_NAME_LENGTH))
			return false;
		stream.write(reinterpret_cast<const char*>(&descriptor.stepCount), sizeof(descriptor.stepCount));
		stream.write(reinterpret_cast<const char*>(&descriptor.normalizedValue), sizeof(descriptor.normalizedValue));
		stream.write(reinterpret_cast<const char*>(&readOnly), sizeof(readOnly));
		stream.write(reinterpret_cast<const char*>(&hidden), sizeof(hidden));
	}

	return stream.good();
}

inline bool OutProcWriteVSTConfig(const std::wstring& path, const OutProcVSTConfig& config)
{
	const std::wstring tempPath = path + L".tmp." + std::to_wstring(GetCurrentProcessId());
	if (!OutProcWriteVSTConfigPayload(tempPath, config))
	{
		DeleteFileW(tempPath.c_str());
		return false;
	}

	if (!MoveFileExW(tempPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		DeleteFileW(tempPath.c_str());
		return false;
	}

	return true;
}

inline bool OutProcReadVSTConfig(const std::wstring& path, OutProcVSTConfig& config)
{
	std::ifstream stream(path, std::ios::binary);
	if (!stream)
		return false;

	std::uint32_t magic = 0;
	std::uint32_t version = 0;
	stream.read(reinterpret_cast<char*>(&magic), sizeof(magic));
	stream.read(reinterpret_cast<char*>(&version), sizeof(version));
	const bool supportedVersion = version == 1 || version == 2 || version == 3 ||
		version == OUTPROC_VST_CONFIG_VERSION;
	if (!stream.good() || magic != OUTPROC_VST_CONFIG_MAGIC || !supportedVersion)
		return false;

	if (!OutProcReadString(stream, config.libraryPath))
		return false;
	config.vst3ClassIndex = 0;
	if (version >= 2)
	{
		stream.read(reinterpret_cast<char*>(&config.vst3ClassIndex), sizeof(config.vst3ClassIndex));
		if (!stream.good())
			return false;
	}
	if (!OutProcReadString(stream, config.chunkData))
		return false;

	std::uint32_t paramCount = 0;
	stream.read(reinterpret_cast<char*>(&paramCount), sizeof(paramCount));
	if (!stream.good())
		return false;
	if (paramCount > OUTPROC_VST_CONFIG_MAX_PARAM_COUNT)
		return false;

	config.paramMap.clear();
	for (std::uint32_t i = 0; i < paramCount; ++i)
	{
		std::wstring key;
		float value = 0.0f;
		if (!OutProcReadString(stream, key))
			return false;
		stream.read(reinterpret_cast<char*>(&value), sizeof(value));
		if (!stream.good())
			return false;
		config.paramMap[key] = value;
	}
	config.midiConfig.clear();
	if (version >= 3 && !OutProcReadString(stream, config.midiConfig))
		return false;

	config.parameterDescriptors.clear();
	if (version >= 4)
	{
		std::uint32_t descriptorCount = 0;
		stream.read(reinterpret_cast<char*>(&descriptorCount), sizeof(descriptorCount));
		if (!stream.good() || descriptorCount > OUTPROC_VST_CONFIG_MAX_DESCRIPTOR_COUNT)
			return false;
		config.parameterDescriptors.reserve(descriptorCount);
		std::size_t descriptorNameBytes = 0;
		for (std::uint32_t i = 0; i < descriptorCount; ++i)
		{
			std::uint8_t api = 0;
			std::uint8_t readOnly = 0;
			std::uint8_t hidden = 0;
			OutProcVSTParameterDescriptor descriptor;
			stream.read(reinterpret_cast<char*>(&api), sizeof(api));
			stream.read(reinterpret_cast<char*>(&descriptor.stableId), sizeof(descriptor.stableId));
			if (!stream.good() || !OutProcReadString(
				stream,
				descriptor.name,
				OUTPROC_VST_CONFIG_MAX_PARAMETER_NAME_LENGTH) ||
				descriptor.name.size() > OUTPROC_VST_CONFIG_MAX_PARAMETER_NAME_LENGTH)
				return false;
			const std::size_t nameBytes = descriptor.name.size() * sizeof(wchar_t);
			if (nameBytes > OUTPROC_VST_CONFIG_MAX_DESCRIPTOR_NAME_BYTES - descriptorNameBytes)
				return false;
			descriptorNameBytes += nameBytes;
			stream.read(reinterpret_cast<char*>(&descriptor.stepCount), sizeof(descriptor.stepCount));
			stream.read(reinterpret_cast<char*>(&descriptor.normalizedValue), sizeof(descriptor.normalizedValue));
			stream.read(reinterpret_cast<char*>(&readOnly), sizeof(readOnly));
			stream.read(reinterpret_cast<char*>(&hidden), sizeof(hidden));
			if (!stream.good() || api > static_cast<std::uint8_t>(OutProcVSTParameterApi::VST3) ||
				readOnly > 1 || hidden > 1 || !std::isfinite(descriptor.normalizedValue))
				return false;
			descriptor.api = static_cast<OutProcVSTParameterApi>(api);
			descriptor.readOnly = readOnly != 0;
			descriptor.hidden = hidden != 0;
			config.parameterDescriptors.push_back(std::move(descriptor));
		}
	}

	const std::ifstream::int_type trailing = stream.peek();
	return trailing == std::char_traits<char>::eof() && stream.eof() && !stream.bad();
}
