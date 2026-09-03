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
	double referenceLevel,
	double referenceOffset,
	double neutralVolumeDb,
	double strength,
	bool useManualVolume,
	double manualVolumeDb,
	bool unmarkedEntry,
	bool canConvertShelf,
	bool canKeepFormula)
	: originalCommand(command),
	  originalParameters(parameters),
	  wasEnabled(wasEnabled),
	  referenceLevel(referenceLevel),
	  referenceOffset(referenceOffset),
	  neutralVolumeDb(neutralVolumeDb),
	  strength(strength),
	  useManualVolume(useManualVolume),
	  manualVolumeDb(manualVolumeDb),
	  migration(Migration::None)
{
	QHBoxLayout* layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);

	QString warningText;
	if (unmarkedEntry && canConvertShelf && canKeepFormula)
	{
		warningText = tr("This unmarked entry could be an original shelf profile or a previously released formula profile. It remains unchanged and bypassed until you choose an interpretation.");
	}
	else if (unmarkedEntry && canConvertShelf)
	{
		warningText = tr("This entry uses the original shelf-based profile. It remains unchanged and bypassed until you explicitly convert it.");
	}
	else if (unmarkedEntry)
	{
		warningText = tr("This unmarked entry matches a previously released formula profile. It remains unchanged and bypassed until you explicitly preserve that interpretation.");
	}
	else
	{
		warningText = tr("This entry uses the retired generic profile. It remains unchanged and bypassed until you explicitly convert it.");
	}
	QLabel* warning = new QLabel(warningText, this);
	if (canConvertShelf &&
		(neutralVolumeDb < -100.0 ||
			(useManualVolume && manualVolumeDb < -100.0)))
	{
		warning->setText(warning->text() + " " + tr(
			"Its volume range extends below the formula profile limit; values below -100 dB will be clamped during conversion."));
	}
	if (!unmarkedEntry)
	{
		warning->setText(warning->text() + " " + tr(
			"The retired headroom mode has no direct equivalent; the formula profile uses automatic headroom."));
	}
	warning->setWordWrap(true);
	layout->addWidget(warning, 1);

	if (canKeepFormula)
	{
		QPushButton* keepFormulaButton = new QPushButton(
			tr("Keep existing formula values"), this);
		keepFormulaButton->setToolTip(tr(
			"Adds the formula marker while preserving the current values and enabled state."));
		layout->addWidget(keepFormulaButton);
		connect(keepFormulaButton, &QPushButton::clicked, this, [this]() {
			migration = Migration::KeepFormula;
			emit updateModel();
		});
	}

	if (canConvertShelf)
	{
		QPushButton* migrateButton = new QPushButton(
			unmarkedEntry
				? tr("Convert original shelf profile")
				: tr("Convert and enable formula profile"),
			this);
		migrateButton->setToolTip(
			unmarkedEntry
				? tr("Maps the original neutral volume and correction strength to the formula profile, then enables it.")
				: wasEnabled
				? tr("Maps the old neutral volume and strength to the formula profile, then enables it.")
				: tr("Converts this disabled entry and enables the new formula profile."));
		layout->addWidget(migrateButton);

		connect(migrateButton, &QPushButton::clicked, this, [this]() {
			migration = Migration::ConvertShelf;
			emit updateModel();
		});
	}
}

void LegacyLoudnessCorrectionFilterGUI::store(QString& command, QString& parameters)
{
	if (migration == Migration::None)
	{
		command = originalCommand;
		parameters = originalParameters;
		return;
	}

	command = "LoudnessCorrection";
	if (migration == Migration::KeepFormula)
	{
		parameters = QString("Schema 1 Model FormulaLoudnessV1 Binding Single State %1 ReferenceLevel %2 ReferenceOffset %3 Attenuation %4")
			.arg(wasEnabled ? 1 : 0)
			.arg(referenceLevel, 0, 'f', 1)
			.arg(referenceOffset, 0, 'f', 1)
			.arg(strength, 0, 'f', 3);
		if (useManualVolume)
			parameters += QString(" Volume %1").arg(manualVolumeDb, 0, 'f', 1);
		return;
	}

	// At the former neutral Windows volume, ReferenceOffset makes the current
	// formula level equal the 80-phon reference, preserving the neutral point.
	double convertedNeutralVolume = (std::max)(-100.0, (std::min)(0.0, neutralVolumeDb));
	parameters = QString("Schema 1 Model FormulaLoudnessV1 Binding All State 1 ReferenceLevel 80 ReferenceOffset %1 Attenuation %2")
		.arg(convertedNeutralVolume, 0, 'f', 1)
		.arg(strength, 0, 'f', 3);
	if (useManualVolume)
	{
		double convertedManualVolume =
			(std::max)(-100.0, (std::min)(0.0, manualVolumeDb));
		parameters += QString(" Volume %1").arg(convertedManualVolume, 0, 'f', 1);
	}
}
