#pragma once

#include <cstddef>
#include <cstdint>
#include <cwctype>
#include <string>

static const std::uint32_t VUMETER_MAGIC = 0x4F505556u; // VUPO
static const std::uint32_t VUMETER_VERSION = 3;
static const unsigned VUMETER_MAX_CHANNELS = 16;

inline std::wstring VUMeterTrimToken(const std::wstring& value)
{
	std::size_t first = 0;
	while (first < value.size() && iswspace(value[first]))
		++first;
	std::size_t last = value.size();
	while (last > first && iswspace(value[last - 1]))
		--last;
	return value.substr(first, last - first);
}

inline std::wstring VUMeterNormalizedId(const std::wstring& value)
{
	const std::wstring normalized = VUMeterTrimToken(value);
	return normalized.empty() ? L"default" : normalized;
}

// Encode every non-ASCII code unit (and '_' itself) so the Editor and the
// audio-service process derive exactly the same Win32 object name regardless
// of their active C locale. Escaping '_' keeps the mapping collision-free.
inline std::wstring VUMeterCanonicalId(const std::wstring& value)
{
	const std::wstring input = VUMeterNormalizedId(value);
	static const wchar_t hex[] = L"0123456789ABCDEF";
	std::wstring result;
	result.reserve(input.size());
	for (wchar_t ch : input)
	{
		const unsigned codeUnit = static_cast<unsigned>(static_cast<unsigned short>(ch));
		const bool asciiAlphaNumeric = (codeUnit >= L'0' && codeUnit <= L'9')
			|| (codeUnit >= L'A' && codeUnit <= L'Z')
			|| (codeUnit >= L'a' && codeUnit <= L'z');
		if (asciiAlphaNumeric || codeUnit == L'-')
			result.push_back(ch);
		else
		{
			result.push_back(L'_');
			result.push_back(hex[(codeUnit >> 12) & 0xF]);
			result.push_back(hex[(codeUnit >> 8) & 0xF]);
			result.push_back(hex[(codeUnit >> 4) & 0xF]);
			result.push_back(hex[codeUnit & 0xF]);
		}
	}
	return result;
}

struct VUMeterSharedData
{
	std::uint32_t magic;
	std::uint32_t version;
	std::uint32_t channelCount;
	std::uint32_t sampleRate;
	std::uint64_t sequence;
	double peak[VUMETER_MAX_CHANNELS];
	double peakHold[VUMETER_MAX_CHANNELS];
	double rms[VUMETER_MAX_CHANNELS];
	double lufsMomentary;
	double lufsShortTerm;
	double lufsIntegrated;
	double channelLufsMomentary[VUMETER_MAX_CHANNELS];
	double channelLufsShortTerm[VUMETER_MAX_CHANNELS];
	double channelLufsIntegrated[VUMETER_MAX_CHANNELS];
	std::uint32_t clip[VUMETER_MAX_CHANNELS];
	std::uint32_t resetRequest;
};

static_assert(offsetof(VUMeterSharedData, sequence) % alignof(std::uint64_t) == 0,
	"The cross-process seqlock must remain naturally aligned");
static_assert(sizeof(VUMeterSharedData) == 888,
	"Changing the mapping size requires a new Win32 object namespace");
