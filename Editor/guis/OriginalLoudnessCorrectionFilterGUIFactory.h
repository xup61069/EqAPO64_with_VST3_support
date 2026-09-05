/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2017 Alexander Walch
    Copyright (C) 2026 Equalizer APO contributors

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#pragma once

#include <QTimer>
#include <memory>
#include <string>

#include "Editor/IFilterGUIFactory.h"
#include "filters/loudnessCorrection/VolumeController.h"

class FilterTable;

class OriginalLoudnessCorrectionFilterGUIFactory : public IFilterGUIFactory
{
	Q_OBJECT

public:
	OriginalLoudnessCorrectionFilterGUIFactory();
	~OriginalLoudnessCorrectionFilterGUIFactory() override;

	void initialize(FilterTable* filterTable) override;
	QList<FilterTemplate> createFilterTemplates() override;
	IFilterGUI* createFilterGUI(
		QString& command,
		QString& parameters) override;

private:
	void checkVolume();

	FilterTable* filterTable = nullptr;
	QTimer timer;
	std::unique_ptr<VolumeController> volumeController;
	std::wstring endpointId;
	double lastVolumeDb = 0.0;
	double lastVolumeScalar = 1.0;
	bool lastMuted = false;
	bool volumeAvailable = false;
};
