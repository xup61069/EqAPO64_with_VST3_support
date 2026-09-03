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
#include "helpers/UiSnapshot.h"
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

	struct UnmarkedParameters
	{
		bool state = false;
		double referenceLevel = 0.0;
		double referenceOffset = 0.0;
		double neutralVolumeDb = 0.0;
		double strength = 1.0;
		bool useManualVolume = false;
		double manualVolumeDb = 0.0;
		bool canConvertShelf = false;
		bool canKeepFormula = false;
	};

	int keyCount(const QString& parameters, const QString& key)
	{
		QRegularExpression expression(
			QString("(?:^|\\s)%1(?=\\s|$)")
				.arg(QRegularExpression::escape(key)),
			QRegularExpression::CaseInsensitiveOption);
		int result = 0;
		QRegularExpressionMatchIterator matches = expression.globalMatch(parameters);
		while (matches.hasNext())
		{
			matches.next();
			++result;
		}
		return result;
	}

	bool containsKey(const QString& parameters, const QString& key)
	{
		return keyCount(parameters, key) > 0;
	}

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
		if (keyCount(parameters, "Version") != 1 ||
			keyCount(parameters, "Model") != 1 ||
			keyCount(parameters, "State") != 1 ||
			keyCount(parameters, "NeutralVolumeDb") != 1 ||
			keyCount(parameters, "Strength") != 1 ||
			keyCount(parameters, "VolumeMode") != 1 ||
			keyCount(parameters, "ManualVolumeDb") > 1)
		{
			return false;
		}

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
		if (keyCount(parameters, "ManualVolumeDb") !=
			(output.useManualVolume ? 1 : 0))
		{
			return false;
		}
		if (output.useManualVolume &&
			!captureNumber(parameters, "ManualVolumeDb", -160.0, 0.0,
				output.manualVolumeDb))
		{
			return false;
		}
		return true;
	}

	bool parseUnmarkedParameters(
		const QString& parameters,
		UnmarkedParameters& output)
	{
		// Marked data belongs to a versioned model and must never fall through
		// to the original shelf-profile adapter.
		if (containsKey(parameters, "Schema") ||
			containsKey(parameters, "Model") ||
			containsKey(parameters, "Version"))
		{
			return false;
		}
		if (keyCount(parameters, "State") != 1 ||
			keyCount(parameters, "ReferenceLevel") != 1 ||
			keyCount(parameters, "ReferenceOffset") != 1 ||
			keyCount(parameters, "Attenuation") > 1 ||
			keyCount(parameters, "Volume") > 1)
		{
			return false;
		}

		QRegularExpression stateExpression(
			"(?:^|\\s)State\\s+(0|1)(?=\\s|$)",
			QRegularExpression::CaseInsensitiveOption);
		QRegularExpressionMatch stateMatch = stateExpression.match(parameters);
		if (!stateMatch.hasMatch())
			return false;
		output.state = stateMatch.captured(1) == "1";

		if (!captureNumber(parameters, "ReferenceLevel", -999.0, 999.0,
			output.referenceLevel) ||
			!captureNumber(parameters, "ReferenceOffset", -999.0, 999.0,
				output.referenceOffset))
		{
			return false;
		}

		output.neutralVolumeDb = output.referenceLevel - output.referenceOffset;

		if (containsKey(parameters, "Attenuation") &&
			!captureNumber(parameters, "Attenuation", 0.0, 1.0,
				output.strength))
		{
			return false;
		}

		output.useManualVolume = containsKey(parameters, "Volume");
		if (output.useManualVolume &&
			!captureNumber(parameters, "Volume", -160.0, 0.0,
				output.manualVolumeDb))
		{
			return false;
		}

		output.canConvertShelf = std::isfinite(output.neutralVolumeDb) &&
			output.neutralVolumeDb <= 0.0;
		output.canKeepFormula = output.referenceLevel >= 1.0 &&
			output.referenceLevel <= 100.0 &&
			output.referenceOffset >= -100.0 &&
			output.referenceOffset <= 100.0 &&
			(!output.useManualVolume || output.manualVolumeDb >= -100.0);
		return output.canConvertShelf || output.canKeepFormula;
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

	if (defaultVolumeController != NULL)
	{
		delete defaultVolumeController;
		defaultVolumeController = NULL;
	}
}

void LoudnessCorrectionFilterGUIFactory::initialize(FilterTable* filterTable)
{
	this->filterTable = filterTable;
}

QList<FilterTemplate> LoudnessCorrectionFilterGUIFactory::createFilterTemplates()
{
	QList<FilterTemplate> list;
	list.append(FilterTemplate(tr("Loudness correction"), "LoudnessCorrection: Schema 1 Model FormulaLoudnessV1 Binding Single State 1 ReferenceLevel 80 ReferenceOffset 0 Attenuation 1.0", QStringList(tr("Advanced filters"))));
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
				80.0,
				genericV2.neutralVolumeDb,
				genericV2.neutralVolumeDb,
				genericV2.strength,
				genericV2.useManualVolume,
				genericV2.manualVolumeDb,
				false,
				true,
				false);
		}
		else
		{
			LoudnessCorrectionFilter::FilterParameters params;
			if (!params.deSerialize(parameters.toStdWString()))
			{
				std::wstring endpointId;
				bool selectedEndpointIsRender =
					getSelectedRenderEndpoint(endpointId);
				result = new LoudnessCorrectionFilterGUI(
					params.state,
					params.referenceLevel,
					params.referenceOffset,
					params.attenuation,
					params.binding,
					params.useManualVolume,
					params.manualVolume,
					endpointId,
					selectedEndpointIsRender);

				if (timer == NULL && !UiSnapshot::requested())
				{
					timer = new QTimer(this);
					connect(timer, SIGNAL(timeout()), this, SLOT(checkVolume()));
					timer->start(250);
				}
			}
			else
			{
				UnmarkedParameters unmarked;
				if (parseUnmarkedParameters(parameters, unmarked))
				{
					result = new LegacyLoudnessCorrectionFilterGUI(
						command,
						parameters,
						unmarked.state,
						unmarked.referenceLevel,
						unmarked.referenceOffset,
						unmarked.neutralVolumeDb,
						unmarked.strength,
						unmarked.useManualVolume,
						unmarked.manualVolumeDb,
						true,
						unmarked.canConvertShelf,
						unmarked.canKeepFormula);
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

	std::wstring endpointId;
	bool selectedEndpointIsRender = getSelectedRenderEndpoint(endpointId);
	if (endpointId != volumeControllerEndpointId)
	{
		delete volumeController;
		volumeController = NULL;
		volumeControllerEndpointId = endpointId;
		lastVolume = std::numeric_limits<double>::quiet_NaN();
	}

	if (selectedEndpointIsRender && !endpointId.empty() &&
		volumeController == NULL)
	{
		volumeController = new VolumeController(endpointId);
		if (FAILED(volumeController->getVolume(lastVolume)))
			lastVolume = std::numeric_limits<double>::quiet_NaN();
	}
	else if (volumeController != NULL)
	{
		double volume;
		HRESULT res = volumeController->getVolume(volume);

		if (SUCCEEDED(res) &&
			(!std::isfinite(lastVolume) || std::abs(volume - lastVolume) > 0.05))
		{
			filterTable->updateAnalysis();
			lastVolume = volume;
		}
	}

	// A Global-bound row follows the shared Windows eMultimedia render
	// endpoint, independently of which endpoint is selected in the editor.
	if (defaultVolumeController == NULL)
	{
		defaultVolumeController = new VolumeController();
		if (FAILED(defaultVolumeController->getVolume(lastDefaultVolume)))
			lastDefaultVolume = std::numeric_limits<double>::quiet_NaN();
	}
	else
	{
		double volume;
		HRESULT res = defaultVolumeController->getVolume(volume);
		if (SUCCEEDED(res) &&
			(!std::isfinite(lastDefaultVolume) ||
				std::abs(volume - lastDefaultVolume) > 0.05))
		{
			filterTable->updateAnalysis();
			lastDefaultVolume = volume;
		}
	}
}

bool LoudnessCorrectionFilterGUIFactory::getSelectedRenderEndpoint(
	std::wstring& endpointId) const
{
	endpointId.clear();
	if (filterTable == NULL)
		return false;

	std::shared_ptr<AbstractAPOInfo> selectedDevice = filterTable->getSelectedDevice();
	if (selectedDevice == NULL || selectedDevice->isInput())
		return false;

	// Some valid render endpoints (notably Voicemeeter/Matrix virtual devices)
	// have no endpoint GUID. The flow is still known: Global binding can use
	// the Windows default render endpoint, while Single remains unavailable.
	endpointId = selectedDevice->getDeviceGuid();
	return true;
}
