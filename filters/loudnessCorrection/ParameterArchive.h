/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2017  Alexander Walch

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#pragma once

#include <algorithm>
#include <regex>
#include <string>
#include <vector>
#include "../../helpers/StringHelper.h"

template<typename type> struct to_WString_type_traits
{
	static inline std::wstring cast_ToWString(const type& input) {return std::wstring();}
};

template<> struct to_WString_type_traits<float>
{
	static inline std::wstring cast_ToWString(const float& input) {return std::to_wstring((long double)input);}
};

template<> struct to_WString_type_traits<double>
{
	static inline std::wstring cast_ToWString(const double& input) {return std::to_wstring((long double)input);}
};

template<> struct to_WString_type_traits<int>
{
	static inline std::wstring cast_ToWString(const int& input) {return std::to_wstring((long long)input);}
};

template<> struct to_WString_type_traits<bool>
{
	static inline std::wstring cast_ToWString(const bool& input) {return std::to_wstring((long long)input);}
};

template<> struct to_WString_type_traits<std::string>
{
	static inline std::wstring cast_ToWString(const std::string& input) {return StringHelper::toWString(input, CP_ACP);}
};

template<> struct to_WString_type_traits<std::wstring>
{
	static inline std::wstring cast_ToWString(const std::wstring& input) {return input;}
};

template<typename type> struct from_WString_type_traits
{
	static inline type cast_fromWString(const std::wstring& input) {return (type)wcstod(input.c_str(), NULL);}
};

template<> struct from_WString_type_traits<float>
{
	static inline float cast_fromWString(std::wstring input)
	{
		std::replace(input.begin(), input.end(), L',', L'.');
		return (float)wcstod(input.c_str(), NULL);
	}
};

template<> struct from_WString_type_traits<double>
{
	static inline double cast_fromWString(std::wstring input)
	{
		std::replace(input.begin(), input.end(), L',', L'.');
		return wcstod(input.c_str(), NULL);
	}
};

template<> struct from_WString_type_traits<int>
{
	static inline int cast_fromWString(const std::wstring& input) {return (int)wcstol(input.c_str(), NULL, 10);}
};

template<> struct from_WString_type_traits<bool>
{
	static inline bool cast_fromWString(const std::wstring& input) {return wcstol(input.c_str(), NULL, 10) == 0 ? false : true;}
};

template<> struct from_WString_type_traits<std::wstring>
{
	static inline std::wstring cast_fromWString(const std::wstring& input) {return input;}
};

template<typename type> struct constructor_traits
{
	static inline std::wstring initFrom(type input) {return std::wstring();}
};

template<> struct constructor_traits<std::wstring>
{
	static inline std::wstring initFrom(std::wstring input) {return input;}
};

template<> struct constructor_traits<std::vector<char>>
{
	static inline std::wstring initFrom(std::vector<char> input)
	{
		if (input.empty())
			return std::wstring();

		auto terminator = std::find(input.begin(), input.end(), '\0');
		std::string tempParams(input.begin(), terminator);
		return to_WString_type_traits<std::string>::cast_ToWString(tempParams);
	}
};

class ParameterArchive
{
public:
	ParameterArchive()
		: _serializedParamters(std::wstring())
	{
	}
	template<typename T> ParameterArchive(T input)
	{
		_serializedParamters = constructor_traits<T>::initFrom(input);
	}

	std::vector<char> getSerializedParameters()
	{
		std::string serialized = StringHelper::toString(_serializedParamters, CP_ACP);
		return std::vector<char>(serialized.begin(), serialized.end());
	}

	template<typename T, typename U> void add(const T& parameter, const U& name)
	{
		std::wstring nameStr = to_WString_type_traits<U>::cast_ToWString(name);
		if (!nameStr.empty())
		{
			_serializedParamters += nameStr;
			_serializedParamters += L" ";
		}
		_serializedParamters += to_WString_type_traits<T>::cast_ToWString(parameter);
		_serializedParamters += L" ";
	};

	template<typename T> int get(T& parameter, const std::wregex& searchArgument)
	{
		std::wsmatch match;
		bool found = regex_search(_serializedParamters, match, searchArgument);
		if (!found)
		{
			return 1;
		}
		std::vector<std::wstring> splittedStrings = StringHelper::split(match.str(1), L' ');
		if (splittedStrings.empty())
		{
			return 1;
		}
		parameter = from_WString_type_traits<T>::cast_fromWString(splittedStrings.back());
		return 0;
	};

	bool find(const std::wregex& searchArgument)
	{
		std::wsmatch match;
		return regex_search(_serializedParamters, match, searchArgument);
	}

private:
	std::wstring _serializedParamters;
};
