/*
	This file is part of EqualizerAPO, a system-wide equalizer.
	Copyright (C) 2015  Jonas Thedering

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

#include <atomic>
#include <cstdint>
#include <vector>

#include "IFilter.h"
#include "libHybridConv-0.1.1/libHybridConv_eapo.h"

#pragma AVRT_VTABLES_BEGIN
class ConvolutionFilter : public IFilter
{
public:
	ConvolutionFilter(std::wstring filename);
	virtual ~ConvolutionFilter();
	bool getInPlace() override { return true; }
	std::vector<std::wstring> initialize(float sampleRate, unsigned maxFrameCount, std::vector<std::wstring> channelNames) override;
	void process(double** output, double** input, unsigned frameCount) override;

protected:
	virtual bool prepareImpulseResponse(
		std::vector<std::vector<double>>& impulseResponses);
	float sampleRate;

private:
	struct ConvolutionBank;

	static unsigned long __stdcall bankWorkerEntry(void* context);
	void bankWorkerLoop();
	bool startWorker();
	void stopWorker();
	ConvolutionBank* createBank(unsigned frameCount);
	void destroyBank(ConvolutionBank* bank);
	void cleanup();
	void copyDry(double** output, double** input, unsigned frameCount) const;

	std::wstring filename;
	unsigned channelCount;
	unsigned maxFrameCount;
	std::vector<std::vector<double>> impulseResponses;
	double* fadeScratch;
	ConvolutionBank* activeBank;
	std::atomic<ConvolutionBank*> pendingBank;
	std::atomic<ConvolutionBank*> retiredBank;
	std::atomic<unsigned> requestedFrameCount;
	std::atomic<std::uint64_t> requestGeneration;
	void* workerStopEvent;
	void* workerThread;
	unsigned fadeLength;
	unsigned fadePosition;
	unsigned lastRequestedFrame;
	bool activeUsable;
};
#pragma AVRT_VTABLES_END
