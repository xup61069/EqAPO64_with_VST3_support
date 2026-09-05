/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2017 Alexander Walch
    Copyright (C) 2026 Equalizer APO contributors

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "OriginalLoudnessCorrectionFilterGUIFactory.h"

#include <cmath>

#include "Editor/FilterTable.h"
#include "OriginalLoudnessCorrectionFilterGUI.h"
#include "filters/loudnessCorrection/OriginalLoudnessCorrectionFilter.h"
#include "helpers/UiSnapshot.h"

OriginalLoudnessCorrectionFilterGUIFactory::
	OriginalLoudnessCorrectionFilterGUIFactory()
{
	timer.setTimerType(Qt::CoarseTimer);
	timer.setInterval(250);
	connect(&timer, &QTimer::timeout,
		this, &OriginalLoudnessCorrectionFilterGUIFactory::checkVolume);
}

OriginalLoudnessCorrectionFilterGUIFactory::
	~OriginalLoudnessCorrectionFilterGUIFactory()
{
	timer.stop();
}

void OriginalLoudnessCorrectionFilterGUIFactory::initialize(
	FilterTable* filterTable)
{
	this->filterTable = filterTable;
}

QList<FilterTemplate>
OriginalLoudnessCorrectionFilterGUIFactory::createFilterTemplates()
{
	QList<FilterTemplate> result;
	result.append(FilterTemplate(
		tr("Loudness correction (original)"),
		"LoudnessCorrectionOriginal: Schema 1 Model MixomoShelfV1 State 1 ReferenceLevel 0 ReferenceOffset 0 Attenuation 1.0",
		QStringList(tr("Advanced filters"))));
	return result;
}

IFilterGUI* OriginalLoudnessCorrectionFilterGUIFactory::createFilterGUI(
	QString& command,
	QString& parameters)
{
	if (command != "LoudnessCorrectionOriginal")
		return nullptr;

	OriginalLoudnessCorrectionFilter::FilterParameters parsed(
		parameters.toStdWString());
	if (!parsed.isInitialized())
		return nullptr;

	if (!UiSnapshot::requested() && !timer.isActive())
		timer.start();
	return new OriginalLoudnessCorrectionFilterGUI(
		parsed.state,
		parsed.referenceLevel,
		parsed.referenceOffset,
		parsed.attenuation);
}

void OriginalLoudnessCorrectionFilterGUIFactory::checkVolume()
{
	if (filterTable == nullptr)
		return;
	if (!volumeController)
		volumeController.reset(new VolumeController());

	EndpointVolumeState state;
	const bool available =
		SUCCEEDED(volumeController->getVolumeState(state)) &&
		std::isfinite(state.levelDb) &&
		std::isfinite(state.scalar) &&
		!volumeController->getEndpointId().empty();
	const std::wstring currentEndpointId = available
		? volumeController->getEndpointId() : std::wstring();
	const bool changed = available != volumeAvailable ||
		(available && (
			std::abs(state.levelDb - lastVolumeDb) > 0.05 ||
			std::abs(state.scalar - lastVolumeScalar) > 1.0e-6 ||
			state.muted != lastMuted ||
			_wcsicmp(currentEndpointId.c_str(), endpointId.c_str()) != 0));

	if (!available)
	{
		volumeController.reset();
		endpointId.clear();
	}
	else
	{
		endpointId = currentEndpointId;
		lastVolumeDb = state.levelDb;
		lastVolumeScalar = state.scalar;
		lastMuted = state.muted;
	}
	volumeAvailable = available;
	if (changed)
		filterTable->updateAnalysis();
}
