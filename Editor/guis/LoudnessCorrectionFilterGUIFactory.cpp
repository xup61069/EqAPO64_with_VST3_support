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

#include "filters/loudnessCorrection/LoudnessCorrectionFilter.h"
#include "LegacyLoudnessCorrectionFilterGUI.h"
#include "LoudnessCorrectionFilterGUI.h"
#include "LoudnessCorrectionFilterGUIFactory.h"
#include "AbstractAPOInfo.h"
#include <cmath>
#include <limits>
#include <QRegularExpression>

namespace
{
	struct GenericV2Parameters
	{
		bool state = false;
		double neutralVolumeDb = -20.0;
		double strength = 1.0;
		bool useManualVolume = false;
		double manualVolumeDb = 0.0;
	};

	bool captureNumber(
		const QString& parameters,
		const QString& key,
		double minimum,
		double maximum,
		double& output)
	{
		QRegularExpression expression(
			QString("(?:^|\\s)%1\\s+([-+]?(?:[0-9]+(?:[\\.,][0-9]*)?|[\\.,][0-9]+))(?=\\s|$)")
				.arg(QRegularExpression::escape(key)),
			QRegularExpression::CaseInsensitiveOption);
		QRegularExpressionMatch match = expression.match(parameters);
		if (!match.hasMatch())
			return false;
		bool ok = false;
		QString number = match.captured(1);
		number.replace(',', '.');
		double value = number.toDouble(&ok);
		if (!ok || !std::isfinite(value) || value < minimum || value > maximum)
			return false;
		output = value;
		return true;
	}

	bool parseGenericV2Parameters(
		const QString& parameters,
		GenericV2Parameters& output)
	{
		QRegularExpression versionExpression(
			"(?:^|\\s)Version\\s+3(?=\\s|$)",
			QRegularExpression::CaseInsensitiveOption);
		QRegularExpression modelExpression(
			"(?:^|\\s)Model\\s+GenericLoudnessV1(?=\\s|$)",
			QRegularExpression::CaseInsensitiveOption);
		if (!versionExpression.match(parameters).hasMatch() ||
			!modelExpression.match(parameters).hasMatch())
			return false;

		QRegularExpression stateExpression(
			"(?:^|\\s)State\\s+(0|1)(?=\\s|$)",
			QRegularExpression::CaseInsensitiveOption);
		QRegularExpressionMatch stateMatch = stateExpression.match(parameters);
		if (!stateMatch.hasMatch())
			return false;
		output.state = stateMatch.captured(1) == "1";

		if (!captureNumber(parameters, "NeutralVolumeDb", -160.0, 0.0,
			output.neutralVolumeDb) ||
			!captureNumber(parameters, "Strength", 0.0, 1.0, output.strength))
		{
			return false;
		}

		QRegularExpression volumeModeExpression(
			"(?:^|\\s)VolumeMode\\s+(Auto|Manual)(?=\\s|$)",
			QRegularExpression::CaseInsensitiveOption);
		QRegularExpressionMatch volumeModeMatch = volumeModeExpression.match(parameters);
		if (!volumeModeMatch.hasMatch())
			return false;
		output.useManualVolume = volumeModeMatch.captured(1).compare(
			"Manual", Qt::CaseInsensitive) == 0;
		if (output.useManualVolume &&
			!captureNumber(parameters, "ManualVolumeDb", -160.0, 0.0,
				output.manualVolumeDb))
		{
			return false;
		}
		return true;
	}
}

LoudnessCorrectionFilterGUIFactory::LoudnessCorrectionFilterGUIFactory()
{
}

LoudnessCorrectionFilterGUIFactory::~LoudnessCorrectionFilterGUIFactory()
{
	if (timer != NULL)
		timer->stop();

	if (volumeController != NULL)
	{
		delete volumeController;
		volumeController = NULL;
	}
}

void LoudnessCorrectionFilterGUIFactory::initialize(FilterTable* filterTable)
{
	this->filterTable = filterTable;
}

QList<FilterTemplate> LoudnessCorrectionFilterGUIFactory::createFilterTemplates()
{
	QList<FilterTemplate> list;
	list.append(FilterTemplate(tr("Loudness correction"), "LoudnessCorrection: State 1 ReferenceLevel 80 ReferenceOffset 0 Attenuation 1.0", QStringList(tr("Advanced filters"))));
	return list;
}

IFilterGUI* LoudnessCorrectionFilterGUIFactory::createFilterGUI(QString& command, QString& parameters)
{
	IFilterGUI* result = NULL;

	if (command == "LoudnessCorrection")
	{
		GenericV2Parameters genericV2;
		if (parseGenericV2Parameters(parameters, genericV2))
		{
			result = new LegacyLoudnessCorrectionFilterGUI(
				command,
				parameters,
				genericV2.state,
				genericV2.neutralVolumeDb,
				genericV2.strength,
				genericV2.useManualVolume,
				genericV2.manualVolumeDb);
		}
		else
		{
			LoudnessCorrectionFilter::FilterParameters params;
			if (!params.deSerialize(parameters.toStdWString()))
			{
				std::wstring endpointId = getSelectedRenderEndpointId();
				result = new LoudnessCorrectionFilterGUI(
					params.state,
					params.referenceLevel,
					params.referenceOffset,
					params.attenuation,
					params.useManualVolume,
					params.manualVolume,
					endpointId,
					!endpointId.empty());

				if (timer == NULL)
				{
					timer = new QTimer(this);
					connect(timer, SIGNAL(timeout()), this, SLOT(checkVolume()));
					timer->start(250);
				}
			}
		}
	}

	return result;
}

void LoudnessCorrectionFilterGUIFactory::checkVolume()
{
	if (filterTable == NULL)
		return;

	std::wstring endpointId = getSelectedRenderEndpointId();
	if (endpointId != volumeControllerEndpointId)
	{
		delete volumeController;
		volumeController = NULL;
		volumeControllerEndpointId = endpointId;
		lastVolume = std::numeric_limits<double>::quiet_NaN();
	}

	if (endpointId.empty())
		return;

	if (volumeController == NULL)
	{
		volumeController = new VolumeController(endpointId);
		volumeController->getVolume(lastVolume);
	}
	else
	{
		double volume;
		HRESULT res = volumeController->getVolume(volume);

		if (SUCCEEDED(res) && std::abs(volume - lastVolume) > 0.05)
		{
			filterTable->updateAnalysis();
			lastVolume = volume;
		}
	}
}

std::wstring LoudnessCorrectionFilterGUIFactory::getSelectedRenderEndpointId() const
{
	if (filterTable == NULL)
		return L"";

	std::shared_ptr<AbstractAPOInfo> selectedDevice = filterTable->getSelectedDevice();
	if (selectedDevice == NULL || selectedDevice->isInput())
		return L"";

	std::wstring deviceGuid = selectedDevice->getDeviceGuid();
	if (deviceGuid.empty())
		return L"";
	return deviceGuid;
}
