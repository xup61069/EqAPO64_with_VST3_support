/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2026 Equalizer APO contributors

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#pragma once

#include <mutex>

// FFTW execution is thread-safe for distinct plans, but planner creation and
// destruction share global state. Every product translation unit uses this
// guard so Editor analysis and IR regeneration cannot race the audio engine.
class FFTWPlannerGuard
{
public:
	FFTWPlannerGuard()
		: lock(plannerMutex())
	{
	}

	FFTWPlannerGuard(const FFTWPlannerGuard&) = delete;
	FFTWPlannerGuard& operator=(const FFTWPlannerGuard&) = delete;

private:
	static std::mutex& plannerMutex()
	{
		static std::mutex plannerMutex;
		return plannerMutex;
	}

	std::lock_guard<std::mutex> lock;
};
