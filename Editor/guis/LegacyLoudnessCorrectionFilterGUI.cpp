/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2026 Equalizer APO contributors
*/

#include "LegacyLoudnessCorrectionFilterGUI.h"

#include <algorithm>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

LegacyLoudnessCorrectionFilterGUI::LegacyLoudnessCorrectionFilterGUI(
	const QString& command,
	const QString& parameters,
	bool wasEnabled,
	double neutralVolumeDb,
	double strength,
	bool useManualVolume,
	double manualVolumeDb,
	bool originalShelfProfile)
	: originalCommand(command),
	  originalParameters(parameters),
	  neutralVolumeDb(neutralVolumeDb),
	  strength(strength),
	  useManualVolume(useManualVolume),
	  manualVolumeDb(manualVolumeDb),
	  migrated(false)
{
	QHBoxLayout* layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);

	QLabel* warning = new QLabel(
		originalShelfProfile
			? tr("This entry uses the original shelf-based profile. It remains unchanged and bypassed until you explicitly convert it.")
			: tr("This entry uses the retired generic profile. It remains unchanged and bypassed until you explicitly convert it."),
		this);
	if (neutralVolumeDb < -100.0 ||
		(useManualVolume && manualVolumeDb < -100.0))
	{
		warning->setText(warning->text() + " " + tr(
			"Its volume range extends below the formula profile limit; values below -100 dB will be clamped during conversion."));
	}
	if (!originalShelfProfile)
	{
		warning->setText(warning->text() + " " + tr(
			"The retired headroom mode has no direct equivalent; the formula profile uses automatic headroom."));
	}
	warning->setWordWrap(true);
	layout->addWidget(warning, 1);

	QPushButton* migrateButton = new QPushButton(
		tr("Convert and enable formula profile"), this);
	migrateButton->setToolTip(
		wasEnabled
			? tr("Maps the old neutral volume and strength to the formula profile, then enables it.")
			: tr("Converts this disabled entry and enables the new formula profile."));
	layout->addWidget(migrateButton);

	connect(migrateButton, &QPushButton::clicked, this, [this]() {
		migrated = true;
		emit updateModel();
	});
}

void LegacyLoudnessCorrectionFilterGUI::store(QString& command, QString& parameters)
{
	if (!migrated)
	{
		command = originalCommand;
		parameters = originalParameters;
		return;
	}

	command = "LoudnessCorrection";
	// At the former neutral Windows volume, ReferenceOffset makes the current
	// formula level equal the 80-phon reference, preserving the neutral point.
	double convertedNeutralVolume = (std::max)(-100.0, (std::min)(0.0, neutralVolumeDb));
	parameters = QString("Schema 1 Model FormulaLoudnessV1 State 1 ReferenceLevel 80 ReferenceOffset %1 Attenuation %2")
		.arg(convertedNeutralVolume, 0, 'f', 1)
		.arg(strength, 0, 'f', 3);
	if (useManualVolume)
	{
		double convertedManualVolume =
			(std::max)(-100.0, (std::min)(0.0, manualVolumeDb));
		parameters += QString(" Volume %1").arg(convertedManualVolume, 0, 'f', 1);
	}
}
