/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2026 Equalizer APO contributors

    Settings from the short-lived generic profile must never be silently
    reinterpreted as the formula-based loudness profile.
*/

#pragma once

#include "Editor/IFilterGUI.h"

class LegacyLoudnessCorrectionFilterGUI : public IFilterGUI
{
	Q_OBJECT

public:
	explicit LegacyLoudnessCorrectionFilterGUI(
		const QString& command,
		const QString& parameters,
		bool wasEnabled,
		double neutralVolumeDb,
		double strength,
		bool useManualVolume,
		double manualVolumeDb);

	void store(QString& command, QString& parameters) override;

private:
	QString originalCommand;
	QString originalParameters;
	double neutralVolumeDb;
	double strength;
	bool useManualVolume;
	double manualVolumeDb;
	bool migrated;
};
