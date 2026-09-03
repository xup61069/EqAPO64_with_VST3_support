#include "stdafx.h"
#define _USE_MATH_DEFINES
#include <algorithm>
#include <cmath>
#include "PanFilter.h"
#include "AudioToolsHelper.h"

using namespace std;

PanFilter::PanFilter(double position, double widthPercent)
	: position(max(-100.0, min(100.0, position)) / 100.0), width(max(0.0, min(200.0, widthPercent)) / 100.0)
{
}

vector<wstring> PanFilter::initialize(float sampleRate, unsigned maxFrameCount, vector<wstring> channelNames)
{
	channelCount = static_cast<unsigned>(channelNames.size());
	return channelNames;
}

#pragma AVRT_CODE_BEGIN
void PanFilter::process(double** output, double** input, unsigned frameCount)
{
	if (channelCount < 2)
	{
		for (unsigned c = 0; c < channelCount; c++)
			if (output[c] != input[c])
				memcpy(output[c], input[c], frameCount * sizeof(double));
		return;
	}

	const double angle = (position + 1.0) * M_PI_4;
	const double leftPan = cos(angle);
	const double rightPan = sin(angle);
	const double midGain = max(0.0, 1.0 - width);
	const double sideGain = width;

	for (unsigned i = 0; i < frameCount; i++)
	{
		const double left = input[0][i];
		const double right = input[1][i];
		const double mid = 0.5 * (left + right) * midGain;
		const double sideLeft = left * sideGain * leftPan;
		const double sideRight = right * sideGain * rightPan;
		output[0][i] = mid + sideLeft;
		output[1][i] = mid + sideRight;
	}

	for (unsigned c = 2; c < channelCount; c++)
		if (output[c] != input[c])
			memcpy(output[c], input[c], frameCount * sizeof(double));
}
#pragma AVRT_CODE_END
