/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2017  Jonas Thedering

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

#include "Editor/helpers/GUIHelper.h"
#include "LoudnessCorrectionFilterGUIDialog.h"
#include "LoudnessCorrectionFilterGUI.h"
#include "LoudnessCorrectionStudioDialog.h"
#include "ui_LoudnessCorrectionFilterGUI.h"
#include <cmath>
#include <limits>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QToolTip>

LoudnessCorrectionFilterGUI::LoudnessCorrectionFilterGUI(
	bool state,
	double refLevel,
	double refOffset,
	double att,
	LoudnessCorrectionFilter::FilterParameters::BindingMode binding,
	bool useManualVolume,
	double manualVolume,
	const std::wstring& endpointIdentifier,
	bool selectedEndpointIsRender)
	: IFilterGUI(),
	  ui(new Ui::LoudnessCorrectionFilterGUI),
	  state(state),
	  selectedEndpointIsRender(selectedEndpointIsRender),
	  automaticVolumeAvailable(false),
	  endpointIdentifier(endpointIdentifier),
	  endpointId(),
	  lastVolume(std::numeric_limits<double>::quiet_NaN())
{
	ui->setupUi(this);

	const QSize compactDialSize = GUIHelper::scale(QSize(60, 48));
	ui->refLevelDial->setFixedSize(compactDialSize);
	ui->refOffsetDial->setFixedSize(compactDialSize);
	ui->attDial->setFixedSize(compactDialSize);
	ui->refLevelDial->setAccessibleName(ui->refLevelLabel->text());
	ui->refOffsetDial->setAccessibleName(ui->refOffsetLabel->text());
	ui->attDial->setAccessibleName(ui->attLabel->text());

	if (refLevel <= 0)
		refLevel = 80;

	ui->refLevelSpinBox->setValue((int)refLevel);
	ui->refOffsetSpinBox->setValue((int)refOffset);
	ui->attSpinBox->setValue(att);

	bool bindingBlocked = ui->bindingComboBox->blockSignals(true);
	ui->bindingComboBox->setCurrentIndex(
		binding == LoudnessCorrectionFilter::FilterParameters::BINDING_ALL ? 1 : 0);
	ui->bindingComboBox->blockSignals(bindingBlocked);
	refreshVolumeController();
	updateAutomaticVolumeUi();

	bool blocked = ui->manualVolumeCheckBox->blockSignals(true);
	ui->manualVolumeCheckBox->setChecked(
		useManualVolume || !this->automaticVolumeAvailable);
	ui->manualVolumeCheckBox->blockSignals(blocked);
	ui->volumeSpinBox->setEnabled(
		useManualVolume || !this->automaticVolumeAvailable);
	if (useManualVolume || !this->automaticVolumeAvailable)
	{
		ui->volumeSpinBox->setValue(manualVolume);
		lastVolume = manualVolume;
	}
	else
	{
		ui->volumeSpinBox->setValue(lastVolume);
	}

	connect(&timer, SIGNAL(timeout()), this, SLOT(updateVolume()));
	timer.start(250);
}

LoudnessCorrectionFilterGUI::~LoudnessCorrectionFilterGUI()
{
	delete ui;
}

void LoudnessCorrectionFilterGUI::store(QString& command, QString& parameters)
{
	command = "LoudnessCorrection";
	QString binding = getBindingMode() ==
		LoudnessCorrectionFilter::FilterParameters::BINDING_ALL ? "All" : "Single";
	parameters = QString("Schema 1 Model FormulaLoudnessV1 Binding %1 State %2 ReferenceLevel %3 ReferenceOffset %4 Attenuation ")
		.arg(binding)
		.arg(state ? 1 : 0)
		.arg(ui->refLevelSpinBox->value())
		.arg(ui->refOffsetSpinBox->value());
	double att = ui->attSpinBox->value();
	if (att == 0.0 || att == 1.0)
		parameters += QString("%0").arg(att, 0, 'f', 1);
	else
		parameters += QString("%0").arg(att);

	if (ui->manualVolumeCheckBox->isChecked())
		parameters += QString(" Volume %0").arg(
			ui->volumeSpinBox->value(), 0, 'f', 1);
}

LoudnessCorrectionFilter::FilterParameters::BindingMode
LoudnessCorrectionFilterGUI::getBindingMode() const
{
	return ui->bindingComboBox->currentIndex() == 1 ?
		LoudnessCorrectionFilter::FilterParameters::BINDING_ALL :
		LoudnessCorrectionFilter::FilterParameters::BINDING_SINGLE;
}

std::wstring LoudnessCorrectionFilterGUI::getRequestedEndpointId() const
{
	if (getBindingMode() == LoudnessCorrectionFilter::FilterParameters::BINDING_ALL)
		return L"";
	return endpointIdentifier;
}

void LoudnessCorrectionFilterGUI::refreshVolumeController()
{
	volumeController.reset();
	endpointId.clear();
	automaticVolumeAvailable = false;
	lastVolume = std::numeric_limits<double>::quiet_NaN();
	if (getBindingMode() ==
		LoudnessCorrectionFilter::FilterParameters::BINDING_SINGLE &&
		!selectedEndpointIsRender)
		return;

	std::wstring requestedEndpointId = getRequestedEndpointId();
	if (getBindingMode() == LoudnessCorrectionFilter::FilterParameters::BINDING_SINGLE &&
		requestedEndpointId.empty())
	{
		return;
	}

	// Resolve the actual endpoint used by either binding strategy. Calibration
	// guards and the displayed automatic value both use this resolved identity.
	volumeController.reset(new VolumeController(requestedEndpointId));
	double endpointVolume = 0.0;
	if (FAILED(volumeController->getVolume(endpointVolume)) ||
		!std::isfinite(endpointVolume))
	{
		volumeController.reset();
		return;
	}

	automaticVolumeAvailable = true;
	lastVolume = endpointVolume;
	endpointId = volumeController->getEndpointId();
}

void LoudnessCorrectionFilterGUI::updateAutomaticVolumeUi()
{
	if (automaticVolumeAvailable)
	{
		ui->manualVolumeCheckBox->setText(tr("Manual volume:"));
		ui->manualVolumeCheckBox->setToolTip(tr(
			"Use this when a DAC or amplifier hardware knob controls the real listening level."));
	}
	else
	{
		ui->manualVolumeCheckBox->setText(tr("Manual volume (required):"));
		ui->manualVolumeCheckBox->setToolTip(tr(
			"Automatic volume is unavailable for this playback binding. "
			"Use manual volume for an input or unreadable endpoint."));
	}
	ui->volumeSpinBox->setToolTip(ui->manualVolumeCheckBox->toolTip());
}

void LoudnessCorrectionFilterGUI::on_refLevelSpinBox_valueChanged(int value)
{
	emit updateModel();
}

void LoudnessCorrectionFilterGUI::on_refOffsetSpinBox_valueChanged(int value)
{
	emit updateModel();
}

void LoudnessCorrectionFilterGUI::on_attDial_valueChanged(int value)
{
	ui->attSpinBox->setValue(value / 100.0);
}

void LoudnessCorrectionFilterGUI::on_attSpinBox_valueChanged(double value)
{
	bool previousValue = ui->attDial->blockSignals(true);
	ui->attDial->setValue(round(value * 100.0));
	ui->attDial->blockSignals(previousValue);

	emit updateModel();
}

void LoudnessCorrectionFilterGUI::on_bindingComboBox_currentIndexChanged(int index)
{
	(void)index;
	bool manual = ui->manualVolumeCheckBox->isChecked();
	double manualVolume = ui->volumeSpinBox->value();
	refreshVolumeController();
	updateAutomaticVolumeUi();

	if (!automaticVolumeAvailable)
	{
		bool blocked = ui->manualVolumeCheckBox->blockSignals(true);
		ui->manualVolumeCheckBox->setChecked(true);
		ui->manualVolumeCheckBox->blockSignals(blocked);
		ui->volumeSpinBox->setValue(manualVolume);
		ui->volumeSpinBox->setEnabled(true);
	}
	else if (!manual)
	{
		ui->volumeSpinBox->setValue(lastVolume);
	}

	emit updateModel();
}

void LoudnessCorrectionFilterGUI::on_manualVolumeCheckBox_toggled(bool checked)
{
	if (!checked && !automaticVolumeAvailable)
	{
		bool blocked = ui->manualVolumeCheckBox->blockSignals(true);
		ui->manualVolumeCheckBox->setChecked(true);
		ui->manualVolumeCheckBox->blockSignals(blocked);
		ui->volumeSpinBox->setEnabled(true);
		QToolTip::showText(
			ui->manualVolumeCheckBox->mapToGlobal(
				QPoint(0, ui->manualVolumeCheckBox->height())),
			ui->manualVolumeCheckBox->toolTip(),
			ui->manualVolumeCheckBox);
		emit updateModel();
		return;
	}

	ui->volumeSpinBox->setEnabled(checked);
	if (checked)
	{
		lastVolume = ui->volumeSpinBox->value();
	}
	else
	{
		if (!volumeController)
			volumeController.reset(new VolumeController(getRequestedEndpointId()));
		double endpointVolume = 0.0;
		if (FAILED(volumeController->getVolume(endpointVolume)) ||
			!std::isfinite(endpointVolume))
		{
			automaticVolumeAvailable = false;
			volumeController.reset();
			updateAutomaticVolumeUi();
			bool blocked = ui->manualVolumeCheckBox->blockSignals(true);
			ui->manualVolumeCheckBox->setChecked(true);
			ui->manualVolumeCheckBox->blockSignals(blocked);
			ui->volumeSpinBox->setEnabled(true);
			QToolTip::showText(
				ui->manualVolumeCheckBox->mapToGlobal(
					QPoint(0, ui->manualVolumeCheckBox->height())),
				ui->manualVolumeCheckBox->toolTip(),
				ui->manualVolumeCheckBox);
			emit updateModel();
			return;
		}
		lastVolume = endpointVolume;
		endpointId = volumeController->getEndpointId();
		ui->volumeSpinBox->setValue(endpointVolume);
	}
	emit updateModel();
}

void LoudnessCorrectionFilterGUI::on_volumeSpinBox_valueChanged(double value)
{
	if (ui->manualVolumeCheckBox->isChecked())
	{
		lastVolume = value;
		emit updateModel();
	}
}

void LoudnessCorrectionFilterGUI::on_studioButton_clicked()
{
	LoudnessCorrectionStudioDialog dialog(
		ui->refLevelSpinBox->value(),
		ui->refOffsetSpinBox->value(),
		ui->attSpinBox->value(),
		getBindingMode() == LoudnessCorrectionFilter::FilterParameters::BINDING_ALL,
		ui->manualVolumeCheckBox->isChecked(),
		ui->volumeSpinBox->value(),
		automaticVolumeAvailable,
		this);
	if (dialog.exec() != QDialog::Accepted)
		return;

	{
		QSignalBlocker refLevelDialBlocker(ui->refLevelDial);
		QSignalBlocker refLevelSpinBlocker(ui->refLevelSpinBox);
		QSignalBlocker refOffsetDialBlocker(ui->refOffsetDial);
		QSignalBlocker refOffsetSpinBlocker(ui->refOffsetSpinBox);
		QSignalBlocker attenuationDialBlocker(ui->attDial);
		QSignalBlocker attenuationSpinBlocker(ui->attSpinBox);
		QSignalBlocker bindingBlocker(ui->bindingComboBox);

		ui->refLevelDial->setValue(dialog.getReferenceLevel());
		ui->refLevelSpinBox->setValue(dialog.getReferenceLevel());
		ui->refOffsetDial->setValue(dialog.getReferenceOffset());
		ui->refOffsetSpinBox->setValue(dialog.getReferenceOffset());
		ui->attDial->setValue(qRound(dialog.getAttenuation() * 100.0));
		ui->attSpinBox->setValue(dialog.getAttenuation());
		ui->bindingComboBox->setCurrentIndex(dialog.getGlobalBinding() ? 1 : 0);
	}

	refreshVolumeController();
	updateAutomaticVolumeUi();
	const bool useManualVolume =
		dialog.getUseManualVolume() || !automaticVolumeAvailable;
	{
		QSignalBlocker manualBlocker(ui->manualVolumeCheckBox);
		QSignalBlocker volumeBlocker(ui->volumeSpinBox);
		ui->manualVolumeCheckBox->setChecked(useManualVolume);
		ui->volumeSpinBox->setEnabled(useManualVolume);
		if (useManualVolume)
		{
			ui->volumeSpinBox->setValue(dialog.getVolume());
			lastVolume = dialog.getVolume();
		}
		else
		{
			ui->volumeSpinBox->setValue(lastVolume);
		}
	}

	emit updateModel();
	if (dialog.shouldCalibrateAfterApply())
		on_calibrateButton_clicked();
}

void LoudnessCorrectionFilterGUI::on_calibrateButton_clicked()
{
	if (!ui->manualVolumeCheckBox->isChecked() && !tryUpdateVolume())
	{
		QMessageBox::warning(
			this,
			tr("Calibration not applied"),
			tr("The bound playback volume could not be read. Reconnect the device, choose another binding, or use manual volume and try again."));
		return;
	}

	bool previousState = state;
	state = false;
	emit updateModel();

	LoudnessCorrectionFilterGUIDialog dialog(
		endpointId,
		automaticVolumeAvailable,
		getBindingMode() == LoudnessCorrectionFilter::FilterParameters::BINDING_ALL);
	if (dialog.exec() == QDialog::Accepted)
	{
		if (!ui->manualVolumeCheckBox->isChecked() && !tryUpdateVolume())
		{
			QMessageBox::warning(
				this,
				tr("Calibration not applied"),
				tr("The bound playback volume could not be read. The measured level was not applied; reconnect the device, choose another binding, or use manual volume and try again."));
			state = previousState;
			emit updateModel();
			return;
		}
		double measuredSpl = dialog.getMeasuredLevel();
		double effectiveVolume = lastVolume;
		double refLevel = measuredSpl - effectiveVolume +
			ui->refOffsetSpinBox->value();
		if (refLevel < 1.0) refLevel = 1.0;
		if (refLevel > 100.0) refLevel = 100.0;
		ui->refLevelSpinBox->setValue((int)round(refLevel));
	}

	state = previousState;
	emit updateModel();
}

bool LoudnessCorrectionFilterGUI::tryUpdateVolume()
{
	if (ui->manualVolumeCheckBox->isChecked() || !automaticVolumeAvailable ||
		!volumeController)
		return false;

	double volume;
	HRESULT res = volumeController->getVolume(volume);

	if (SUCCEEDED(res) && std::isfinite(volume))
	{
		endpointId = volumeController->getEndpointId();
		if (!std::isfinite(lastVolume) || std::abs(volume - lastVolume) > 0.05)
			ui->volumeSpinBox->setValue(volume);
		lastVolume = volume;
		return true;
	}

	if (FAILED(res) || !std::isfinite(volume))
		lastVolume = std::numeric_limits<double>::quiet_NaN();
	return false;
}

void LoudnessCorrectionFilterGUI::updateVolume()
{
	(void)tryUpdateVolume();
}
