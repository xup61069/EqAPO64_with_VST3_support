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

#include <QByteArray>
#include <QList>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <fftw3.h>

#include "DeviceAPOInfo.h"

struct AnalysisConfigurationFileSnapshot
{
	QString path;
	QByteArray contents;
	bool readable = false;
};

struct AnalysisVolumeSnapshot
{
	QString requestedEndpointId;
	QString resolvedEndpointId;
	double volumeDb = 0.0;
	double volumeScalar = 1.0;
	bool muted = false;
	bool available = false;
};

class AnalysisThread : public QThread
{
	Q_OBJECT

public:
	AnalysisThread();
	~AnalysisThread();
	quint64 setParameters(std::shared_ptr<AbstractAPOInfo> device, int channelMask, int channelIndex, QString configPath, int frameCount);
	void beginGetResult();
	void endGetResult();

	fftw_complex* getFreqData() const;
	int getFreqDataLength() const;
	int getFreqDataSampleRate() const;
	double getPeakGain() const;
	int getLatency() const;
	double getInitializationTime() const;
	double getProcessingTime() const;
	unsigned getProcessedFrames() const;
	quint64 getResultGeneration() const;
	const QList<AnalysisConfigurationFileSnapshot>& getConfigurationFiles() const;
	const QList<AnalysisVolumeSnapshot>& getVolumeSnapshots() const;

signals:
	void analysisFinished();

protected:
	void run() override;

private:
	QMutex mutex;
	QWaitCondition condition;
	bool quit = false;

	// input
	std::shared_ptr<AbstractAPOInfo> device;
	int channelMask;
	int channelIndex;
	QString configPath;
	int frameCount = 0;
	quint64 requestGeneration = 0;

	// output
	fftw_complex* resultFreqData = NULL;
	int freqDataLength = 0;
	int freqDataSampleRate;
	double peakGain;
	int latency;
	double initializationTime;
	double processingTime;
	int processedFrames;
	quint64 resultGeneration = 0;
	QList<AnalysisConfigurationFileSnapshot> resultConfigurationFiles;
	QList<AnalysisVolumeSnapshot> resultVolumeSnapshots;

	// internal (not protected by mutex)
	int lastFrameCount = -1;
	int lastChannelCount = -1;
	double* buf = NULL;
	double* buf2 = NULL;
	double* timeData = NULL;
	fftw_complex* freqData = NULL;
	fftw_plan planForward = NULL;
};
