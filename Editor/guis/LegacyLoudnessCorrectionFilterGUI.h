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
		double referenceLevel,
		double referenceOffset,
		double neutralVolumeDb,
		double strength,
		bool useManualVolume,
		double manualVolumeDb,
		bool unmarkedEntry,
		bool canConvertShelf,
		bool canKeepFormula);

	void store(QString& command, QString& parameters) override;

private:
	enum class Migration
	{
		None,
		ConvertShelf,
		KeepFormula
	};

	QString originalCommand;
	QString originalParameters;
	bool wasEnabled;
	double referenceLevel;
	double referenceOffset;
	double neutralVolumeDb;
	double strength;
	bool useManualVolume;
	double manualVolumeDb;
	Migration migration;
};
