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

#include <QFile>
#include <QMessageBox>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <mmdeviceapi.h>
#include "Editor/helpers/QtSndfileHandle.h"

#include "LoudnessCorrectionFilterGUIDialog.h"
#include "ui_LoudnessCorrectionFilterGUIDialog.h"

namespace
{
	bool isDefaultRenderEndpoint(
		const std::wstring& endpointId,
		ERole role)
	{
		if (endpointId.empty())
			return false;

		HRESULT initResult = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
		bool uninitialize = SUCCEEDED(initResult);
		IMMDeviceEnumerator* enumerator = NULL;
		IMMDevice* device = NULL;
		LPWSTR defaultEndpointId = NULL;
		bool matches = false;
		HRESULT result = CoCreateInstance(
			__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER,
			__uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator));
		if (SUCCEEDED(result) && enumerator != NULL)
			result = enumerator->GetDefaultAudioEndpoint(eRender, role, &device);
		if (SUCCEEDED(result) && device != NULL)
			result = device->GetId(&defaultEndpointId);
		if (SUCCEEDED(result) && defaultEndpointId != NULL)
			matches = _wcsicmp(defaultEndpointId, endpointId.c_str()) == 0;

		if (defaultEndpointId != NULL)
			CoTaskMemFree(defaultEndpointId);
		if (device != NULL)
			device->Release();
		if (enumerator != NULL)
			enumerator->Release();
		if (uninitialize)
			CoUninitialize();
		return matches;
	}
}

LoudnessCorrectionFilterGUIDialog::LoudnessCorrectionFilterGUIDialog(
	const std::wstring& endpointId,
	bool automaticVolumeAvailable,
	bool followsDefaultMultimedia,
	QWidget* parent)
	: QDialog(parent),
	ui(new Ui::LoudnessCorrectionFilterGUIDialog),
	endpointId(endpointId),
	followsDefaultMultimedia(followsDefaultMultimedia),
	playbackUsesSelectedEndpoint(
		automaticVolumeAvailable &&
		isDefaultRenderEndpoint(endpointId, eConsole) &&
		(!followsDefaultMultimedia ||
			isDefaultRenderEndpoint(endpointId, eMultimedia)))
{
	ui->setupUi(this);
	ui->levelSpinBox->setRange(1, 100);
	ui->levelSpinBox->setSuffix(tr(" dB SPL"));
	ui->levelSpinBox->setToolTip(tr(
		"Enter the slow-response, Z-weighted (flat) reading measured at the listening position."));
	// The procedure is defined for one speaker. Playing both changes the level
	// at the meter and makes the resulting reference ambiguous.
	ui->bothRadioButton->hide();
	if (!playbackUsesSelectedEndpoint)
	{
		ui->playButton->setToolTip(tr(
			"The selected playback device is not the Windows default playback device. "
			"Test-noise playback is blocked to prevent calibration on the wrong speaker."));
	}
	connect(&endpointGuardTimer, &QTimer::timeout, this, [this]() {
		if (buffer.size() > 0 && !isPlaybackEndpointStillValid())
		{
			on_stopButton_clicked();
			QMessageBox::warning(
				this,
				tr("Playback device mismatch"),
				tr("The Windows default playback device changed. Test noise was stopped to prevent calibration on the wrong speaker."));
		}
	});
	endpointGuardTimer.start(250);
}

bool LoudnessCorrectionFilterGUIDialog::isPlaybackEndpointStillValid() const
{
	// PlaySound/Wave routing follows eConsole. Global binding reads the
	// eMultimedia endpoint, so both roles must still resolve to the controller's
	// actual endpoint before calibration noise is allowed to play.
	return playbackUsesSelectedEndpoint &&
		isDefaultRenderEndpoint(endpointId, eConsole) &&
		(!followsDefaultMultimedia ||
			isDefaultRenderEndpoint(endpointId, eMultimedia));
}

LoudnessCorrectionFilterGUIDialog::~LoudnessCorrectionFilterGUIDialog()
{
	on_stopButton_clicked();
	delete ui;
}

int LoudnessCorrectionFilterGUIDialog::getMeasuredLevel()
{
	return ui->levelSpinBox->value();
}

void LoudnessCorrectionFilterGUIDialog::on_playButton_clicked()
{
	// Recheck immediately before playback: the Windows default can change while
	// the dialog is open, and PlaySound would then route to a different speaker.
	if (!isPlaybackEndpointStillValid())
	{
		QMessageBox::warning(
			this,
			tr("Playback device mismatch"),
			tr("Make the selected playback device the Windows default playback device, "
				"then reopen calibration. No test noise was played."));
		return;
	}

	if (buffer.size() > 0)
		on_stopButton_clicked();

	QFile file(":/sounds/pinkNoise.flac");
	if (!file.open(QIODevice::ReadOnly))
		return;

	QtSndfileHandle fileHandle(file, SFM_READ);
	if (fileHandle.error() != SF_ERR_NO_ERROR || fileHandle.samplerate() <= 0)
		return;

	if (!buffer.open(QIODevice::WriteOnly))
		return;

	bool writeSucceeded = true;
	{
		QtSndfileHandle bufferHandle(buffer, SFM_WRITE, SF_FORMAT_WAV | SF_FORMAT_PCM_24, 2, fileHandle.samplerate());
		if (bufferHandle.error() != SF_ERR_NO_ERROR)
			writeSucceeded = false;

		bool leftEnabled = ui->leftRadioButton->isChecked() || ui->bothRadioButton->isChecked();
		bool rightEnabled = ui->rightRadioButton->isChecked() || ui->bothRadioButton->isChecked();

		int buf[1024];
		int buf2[2 * ARRAYSIZE(buf)];
		int samplesRead;
		while (writeSucceeded && (samplesRead = fileHandle.read(buf, ARRAYSIZE(buf))) > 0)
		{
			for (int i = 0; i < samplesRead; i++)
			{
				buf2[2 * i] = leftEnabled ? buf[i] : 0;
				buf2[2 * i + 1] = rightEnabled ? buf[i] : 0;
			}
			writeSucceeded = bufferHandle.write(buf2, 2 * samplesRead) == 2 * samplesRead;
		}
	}
	buffer.close();

	if (!writeSucceeded || buffer.size() == 0)
	{
		buffer.buffer().clear();
		return;
	}

	// Decoding above is synchronous, so the Qt timer cannot observe a default
	// endpoint switch while it runs. Recheck after the buffer is complete and
	// immediately before PlaySound chooses its destination.
	if (!isPlaybackEndpointStillValid())
	{
		buffer.buffer().clear();
		QMessageBox::warning(
			this,
			tr("Playback device mismatch"),
			tr("Make the selected playback device the Windows default playback device, "
				"then reopen calibration. No test noise was played."));
		return;
	}

	if (!PlaySoundA(buffer.data().data(), NULL, SND_MEMORY | SND_ASYNC | SND_LOOP))
		buffer.buffer().clear();
}

void LoudnessCorrectionFilterGUIDialog::on_stopButton_clicked()
{
	if (buffer.size() == 0)
		return;

	(void)PlaySoundA(NULL, NULL, 0);
	buffer.buffer().clear();
}

void LoudnessCorrectionFilterGUIDialog::on_leftRadioButton_toggled(bool checked)
{
	if (checked && buffer.size() > 0)
		on_playButton_clicked();
}

void LoudnessCorrectionFilterGUIDialog::on_rightRadioButton_toggled(bool checked)
{
	if (checked && buffer.size() > 0)
		on_playButton_clicked();
}

void LoudnessCorrectionFilterGUIDialog::on_bothRadioButton_toggled(bool checked)
{
	if (checked && buffer.size() > 0)
		on_playButton_clicked();
}
