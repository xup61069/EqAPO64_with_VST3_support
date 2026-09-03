#pragma once

#include <cerrno>
#include <cmath>
#include <cwchar>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

inline bool VSTParseFiniteFloat(const std::wstring& token, float& value)
{
	if (token.empty())
		return false;

	errno = 0;
	wchar_t* end = nullptr;
	const float parsed = wcstof(token.c_str(), &end);
	if (end == token.c_str() || end == nullptr || *end != L'\0' || errno == ERANGE || !std::isfinite(parsed))
		return false;

	value = parsed;
	return true;
}

inline bool VSTParseLegacyParameterIndex(const std::wstring& token)
{
	if (token.empty() || token[0] < L'0' || token[0] > L'9')
		return false;

	errno = 0;
	wchar_t* end = nullptr;
	const unsigned long parsed = wcstoul(token.c_str(), &end, 10);
	return end != token.c_str()
		&& end != nullptr
		&& *end == L'\0'
		&& errno != ERANGE
		&& parsed <= (std::numeric_limits<unsigned>::max)();
}

// Current rows encode a parameter as <name> <value>. Very old rows used
// <index> <name> <value>; consume all three tokens so the next field stays
// aligned. Unknown/malformed tokens are left for the caller to skip and log.
inline bool VSTConsumeParameter(
	const std::vector<std::wstring>& parts,
	std::size_t& index,
	std::unordered_map<std::wstring, float>& paramMap)
{
	if (index + 1 >= parts.size())
		return false;

	float value = 0.0f;
	if (VSTParseFiniteFloat(parts[index + 1], value))
	{
		if (!parts[index].empty())
			paramMap[parts[index]] = value;
		index += 2;
		return true;
	}

	if (VSTParseLegacyParameterIndex(parts[index])
		&& index + 2 < parts.size()
		&& !parts[index + 1].empty()
		&& VSTParseFiniteFloat(parts[index + 2], value))
	{
		paramMap[parts[index + 1]] = value;
		index += 3;
		return true;
	}

	return false;
}
