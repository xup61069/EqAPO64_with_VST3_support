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

#include <QFileInfo>
#include <algorithm>
#include <cstdint>
#include <QFileDialog>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QProcess>
#include <QSettings>
#include <QSignalBlocker>
#include <QStyle>
#include <QStringList>
#include <QTextStream>
#include <QTimer>
#include <QUuid>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "outproc/OutProcAudioProtocol.h"
#include "outproc/OutProcVSTConfig.h"
#include "helpers/aeffectx.h"
#include "helpers/StringHelper.h"
#include "Editor/helpers/GUIHelper.h"
#include "helpers/UiSnapshot.h"
#include "Editor/MainWindow.h"
#include "VSTPluginFilterGUIDialog.h"
#include "VSTMidiMappingDialog.h"
#include "VSTPluginFilterGUI.h"
#include "ui_VSTPluginFilterGUI.h"

using namespace std;
using namespace std::placeholders;

static QString makeOutProcObjectName(const QString& hostId, const wchar_t* suffix)
{
	QString safeId = hostId;
	for (QChar& ch : safeId)
	{
		const bool ok = ch.isLetterOrNumber() || ch == '-' || ch == '_';
		if (!ok)
			ch = '_';
	}
	return "Global\\EqApoOutProcVST_" + safeId + "_" + QString::fromWCharArray(suffix);
}

static QString canonicalVSTPath(const QString& value)
{
	if (value.trimmed().isEmpty())
		return QString();

	QDir pluginsDir(QString::fromStdWString(
		VSTPluginLibrary::getDefaultPluginPath()));
	QFileInfo fileInfo(pluginsDir, value.trimmed());
	QString canonical = fileInfo.canonicalFilePath();
	if (canonical.isEmpty())
		canonical = fileInfo.absoluteFilePath();
	return QDir::toNativeSeparators(QDir::cleanPath(canonical));
}

static void appendOutProcDebugLog(const QString& message)
{
	QFile file(QDir::temp().absoluteFilePath("EqApoOutProcHost-debug.log"));
	if (file.open(QIODevice::Append | QIODevice::Text))
	{
		QTextStream stream(&file);
		stream << "[editor pid=" << GetCurrentProcessId() << "] " << message << "\n";
	}
}

static std::vector<VSTParameterDescriptor> convertOutProcParameterDescriptors(
	const std::vector<OutProcVSTParameterDescriptor>& source)
{
	std::vector<VSTParameterDescriptor> result;
	result.reserve(source.size());
	for (const OutProcVSTParameterDescriptor& serialized : source)
	{
		VSTParameterDescriptor descriptor;
		descriptor.api = serialized.api == OutProcVSTParameterApi::VST3
			? VSTParameterApi::VST3 : VSTParameterApi::VST2;
		descriptor.stableId = serialized.stableId;
		descriptor.name = serialized.name;
		descriptor.stepCount = serialized.stepCount;
		descriptor.normalizedValue = serialized.normalizedValue;
		descriptor.readOnly = serialized.readOnly;
		descriptor.hidden = serialized.hidden;
		result.push_back(std::move(descriptor));
	}
	return result;
}

static std::vector<OutProcVSTParameterDescriptor> convertOutProcParameterDescriptors(
	const std::vector<VSTParameterDescriptor>& source)
{
	std::vector<OutProcVSTParameterDescriptor> result;
	result.reserve(source.size());
	for (const VSTParameterDescriptor& parameter : source)
	{
		OutProcVSTParameterDescriptor descriptor;
		descriptor.api = parameter.api == VSTParameterApi::VST3
			? OutProcVSTParameterApi::VST3 : OutProcVSTParameterApi::VST2;
		descriptor.stableId = parameter.stableId;
		descriptor.name = parameter.name;
		descriptor.stepCount = parameter.stepCount;
		descriptor.normalizedValue = parameter.normalizedValue;
		descriptor.readOnly = parameter.readOnly;
		descriptor.hidden = parameter.hidden;
		result.push_back(std::move(descriptor));
	}
	return result;
}

VSTPluginFilterGUI::VSTPluginFilterGUI(std::shared_ptr<VSTPluginLibrary> library, const std::wstring& chunkData, const std::unordered_map<std::wstring, float>& paramMap, bool outProcMode, const QString& hostId, int vst3ClassIndex, const std::wstring& midiConfig)
	: ui(new Ui::VSTPluginFilterGUI), library(library), chunkData(chunkData), paramMap(paramMap), midiConfig(midiConfig), outProcMode(outProcMode), hostId(hostId), vst3ClassIndex(vst3ClassIndex)
{
	ui->setupUi(this);
	ui->selectButton->setIcon(GUIHelper::createThemeIcon(GUIHelper::ThemeIcon::OpenFolder));
	ui->label->setBuddy(ui->pathLineEdit);
	ui->pathLineEdit->setAccessibleName(ui->label->text());
	ui->selectButton->setToolTip(tr("Select VST plugin"));
	ui->selectButton->setAccessibleName(tr("Select VST plugin"));
	ui->vst3ClassComboBox->setAccessibleName(tr("VST3 plug-in class"));
	ui->classLabel->setBuddy(ui->vst3ClassComboBox);
	ui->midiButton->setAccessibleName(tr("MIDI control"));
	ui->statusLabel->setWordWrap(true);
	ui->statusLabel->setAccessibleName(tr("VST status"));
	ui->warningTextEdit->setProperty("statusLevel", "warning");
	ui->warningTextEdit->setReadOnly(true);
	ui->warningTextEdit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
	ui->warningTextEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	ui->warningTextEdit->setMinimumWidth(0);
	if (outProcMode && this->hostId.isEmpty())
		this->hostId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	ui->frame->setVisible(false);
	updatePermissionWarning();

	QString absolutePath = QString::fromStdWString(library->getLibPath());
	QDir pluginsDir(QString::fromStdWString(VSTPluginLibrary::getDefaultPluginPath()));
	QString relativePath = QDir::toNativeSeparators(pluginsDir.relativeFilePath(absolutePath));
	if (relativePath.startsWith(QDir::toNativeSeparators("../../")))
		relativePath = absolutePath;
	ui->pathLineEdit->setText(relativePath);
	refreshVST3ClassComboBox();
	updateMidiButton();

	connect(&idleTimer, &QTimer::timeout, this, &VSTPluginFilterGUI::on_idle);
	// This timer only coalesces state persistence and polls out-of-process
	// status. The plug-in editor owns its own high-frequency idle timer.
	idleTimer.setTimerType(Qt::CoarseTimer);
	idleTimer.setInterval(100);
}

VSTPluginFilterGUI::~VSTPluginFilterGUI()
{
	closeOutProcPanel();
	releasePluginInstance();

	delete ui;
}

void VSTPluginFilterGUI::store(QString& command, QString& parameters)
{
	storeWithMidiConfig(command, parameters, midiConfig);
}

void VSTPluginFilterGUI::storeWithMidiConfig(
	QString& command,
	QString& parameters,
	const std::wstring& serializedMidiConfig) const
{
	command = outProcMode ? "OutProcVSTPlugin" : "VSTPlugin";

	QString absolutePath = QString::fromStdWString(library->getLibPath());
	QDir pluginsDir(QString::fromStdWString(VSTPluginLibrary::getDefaultPluginPath()));
	QString relativePath = QDir::toNativeSeparators(pluginsDir.relativeFilePath(absolutePath));
	if (relativePath.startsWith(QDir::toNativeSeparators("../../")))
		relativePath = absolutePath;

	if (relativePath.contains(" "))
		relativePath = "\"" + relativePath + "\"";
	parameters = "Library " + relativePath;
	if (library->isVST3() && vst3ClassIndex != 0)
		parameters += " ClassIndex " + QString::number(vst3ClassIndex);
	if (outProcMode)
		parameters += " HostId " + hostId;
	if (!serializedMidiConfig.empty())
		parameters += " MidiConfig \"" + QString::fromStdWString(serializedMidiConfig) + "\"";
	if (chunkData != L"")
	{
		parameters += " ChunkData \"" + QString::fromStdWString(chunkData) + "\"";
	}
	else
	{
		for (auto it : paramMap)
		{
			QString name = QString::fromStdWString(it.first);
			if (name.contains(" ") || name.contains("\""))
				name = "\"" + name.replace("\"", "\"\"") + "\"";
			parameters += " " + name + " " + QString("%1").arg(it.second);
		}
	}
}

void VSTPluginFilterGUI::loadPreferences(const QVariantMap& prefs)
{
	autoApplyDialog = prefs.value("autoApplyDialog").toBool();
#ifdef EQAPO_ENABLE_UI_SNAPSHOTS
	// Snapshot fixtures are deliberately synthetic. Never let a coincidental
	// file in the working directory turn a visual regression capture into
	// plug-in execution.
	if (UiSnapshot::requested())
		return;
#endif
	if (outProcMode)
	{
		ui->statusLabel->setText(tr("Out-of-process VST host"));
		ui->statusLabel->setProperty("statusLevel", "info");
		ui->statusLabel->style()->unpolish(ui->statusLabel);
		ui->statusLabel->style()->polish(ui->statusLabel);
	}
	else
		initPlugin();
}

void VSTPluginFilterGUI::storePreferences(QVariantMap& prefs)
{
	prefs.insert("autoApplyDialog", autoApplyDialog);
}

void VSTPluginFilterGUI::prepareDelete()
{
	appendOutProcDebugLog("prepareDelete outProc=" + QString(outProcMode ? "true" : "false") + " hostId=" + hostId);
	if (outProcMode)
		terminateOutProcPanel();
}

void VSTPluginFilterGUI::on_openPanelButton_clicked()
{
	if (outProcMode)
	{
		openOutProcPanel();
		return;
	}

	initPlugin();

	if (effect != NULL)
	{
		effect->writeToEffect(chunkData, paramMap);

		VSTPluginFilterGUIDialog dialog(this, effect, autoApplyDialog);
		connect(dialog.getApplyButton(), SIGNAL(pressed()), SLOT(applyDialog()));
		connect(dialog.getAutoApplyCheckBox(), SIGNAL(toggled(bool)), SLOT(autoApplyToggled(bool)));
		lastReadTimer.invalidate();
		idleTimer.start();

		if (dialog.exec() == QDialog::Accepted)
		{
			effect->readFromEffect(chunkData, paramMap);
			updateModel();
			updatePermissionWarning();
		}
		idleTimer.stop();
	}
}

void VSTPluginFilterGUI::on_reloadButton_clicked()
{
	if (library->getLibPath().empty())
		return;

	if (outProcMode)
	{
		appendOutProcDebugLog("reload requested hostId=" + hostId);
		terminateOutProcPanel();
		hostId = QUuid::createUuid().toString(QUuid::WithoutBraces);
		ui->openPanelButton->setText(tr("Open panel"));
		ui->statusLabel->setText(tr("Out-of-process VST reload requested"));
	}
	else
	{
		ui->statusLabel->setText(tr("VST plugin reload requested"));
	}

	// Rewriting the row makes the APO audio engine reconstruct its VST instance.
	emit updateModel();
}

void VSTPluginFilterGUI::on_midiButton_clicked()
{
	if (!outProcMode)
		initPlugin();
	if (outProcMode && outProcGuiRunning && !midiConfig.empty())
		terminateOutProcPanel();
	const std::vector<VSTParameterDescriptor> parameters =
		availableMidiParameters();
	if (parameters.empty())
	{
		QMessageBox::information(
			this,
			tr("VST MIDI control"),
			outProcMode
				? tr("Open the out-of-process panel once and close it after the plug-in state has been captured. MIDI mapping then has stable parameter IDs to target.")
				: tr("This plug-in did not expose any writable parameters for MIDI control."));
		return;
	}

	MainWindow* mainWindow = NULL;
	const bool releaseRuntimeMidi = !midiConfig.empty();
	if (releaseRuntimeMidi)
	{
		mainWindow = qobject_cast<MainWindow*>(window());
		QString temporaryCommand;
		QString temporaryParameters;
		storeWithMidiConfig(
			temporaryCommand, temporaryParameters, std::wstring());
		if (mainWindow == NULL ||
			!mainWindow->beginTemporaryFilterConfiguration(
				this,
				temporaryCommand,
				temporaryParameters,
				QStringLiteral("vst-midi-learn")))
		{
			QMessageBox::warning(
				this,
				tr("VST MIDI control"),
				tr("Save the current profile and resolve any temporary audio state before configuring MIDI."));
			return;
		}
	}

	int dialogResult = QDialog::Rejected;
	std::wstring updatedConfiguration = midiConfig;
	{
		// Destroy the learner (and close its WinMM handle) before restoring the
		// runtime row, otherwise single-client MIDI drivers can remain busy.
		VSTMidiMappingDialog dialog(parameters, midiConfig, this);
		dialogResult = dialog.exec();
		if (dialogResult == QDialog::Accepted)
			updatedConfiguration = dialog.getEncodedConfiguration();
	}

	if (releaseRuntimeMidi)
	{
		bool keptExternal = false;
		if (!mainWindow->restoreTemporaryFilterConfiguration(&keptExternal))
		{
			QMessageBox::critical(
				this,
				tr("Audio processing was not restored"),
				tr("The temporary MIDI-learning state could not be restored automatically. Close the editor and use temporary audio recovery before continuing."));
			return;
		}
		if (keptExternal)
			return;
	}

	if (dialogResult != QDialog::Accepted)
		return;
	if (updatedConfiguration == midiConfig)
		return;
	midiConfig = updatedConfiguration;
	updateMidiButton();
	emit updateModel();
}

void VSTPluginFilterGUI::on_vst3ClassComboBox_currentIndexChanged(int index)
{
	if (index < 0 || index == vst3ClassIndex)
		return;

	if (!chunkData.empty() || !paramMap.empty() || !midiConfig.empty())
	{
		const QMessageBox::StandardButton answer = QMessageBox::question(
			this,
			tr("Change VST3 plug-in class?"),
			tr("Changing the class clears the saved plug-in state and any parameter-specific controls for this row."),
			QMessageBox::Yes | QMessageBox::Cancel,
			QMessageBox::Cancel);
		if (answer != QMessageBox::Yes)
		{
			QSignalBlocker blocker(ui->vst3ClassComboBox);
			ui->vst3ClassComboBox->setCurrentIndex(vst3ClassIndex);
			return;
		}
	}

	if (outProcMode)
	{
		terminateOutProcPanel();
		hostId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	}
	else
		releasePluginInstance();

	vst3ClassIndex = index;
	chunkData = L"";
	paramMap.clear();
	midiConfig.clear();
	outProcParameterDescriptors.clear();
	updateMidiButton();
	if (!outProcMode)
		initPlugin();
	emit updateModel();
	updatePermissionWarning();
}

void VSTPluginFilterGUI::openOutProcPanel()
{
	if (outProcGuiRunning)
	{
		if (outProcGuiHidden)
		{
			if (!signalOutProcPanel(L"GuiShow"))
			{
				terminateOutProcPanel();
			}
			else
			{
				outProcGuiHidden = false;
				ui->openPanelButton->setText(tr("Hide panel"));
				return;
			}
		}
		else
		{
			if (!signalOutProcPanel(L"GuiHide"))
			{
				terminateOutProcPanel();
			}
			else
			{
				outProcGuiHidden = true;
				ui->openPanelButton->setText(tr("Show panel"));
				return;
			}
		}
	}

	if (signalOutProcPanel(L"GuiShow"))
	{
		outProcGuiRunning = true;
		outProcGuiHidden = false;
		ui->openPanelButton->setText(tr("Hide panel"));
		ui->statusLabel->setText(tr("Out-of-process VST panel is open"));
		idleTimer.start();
		return;
	}

	if (library->getLibPath() == L"")
		return;

	const QString hostExe = "EqApoOutProcHost.exe";
	QString hostPath = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(hostExe);
	if (!QFile::exists(hostPath))
	{
		QMessageBox::warning(this, tr("VST plugin"), tr("%1 was not found next to Editor.exe.").arg(hostExe));
		return;
	}

	outProcGuiConfigPath = QDir::temp().absoluteFilePath("EqApoVSTGui-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".opvs");
	OutProcVSTConfig config;
	config.libraryPath = library->getLibPath();
	config.vst3ClassIndex = vst3ClassIndex;
	config.chunkData = chunkData;
	config.paramMap = paramMap;
	config.midiConfig = midiConfig;
	config.parameterDescriptors =
		convertOutProcParameterDescriptors(outProcParameterDescriptors);
	if (!OutProcWriteVSTConfig(outProcGuiConfigPath.toStdWString(), config))
	{
		QMessageBox::warning(this, tr("VST plugin"), tr("Could not create temporary VST host configuration."));
		outProcGuiConfigPath.clear();
		return;
	}

	QStringList arguments;
	arguments << "--gui" << "--session" << hostId << "--vst-config" << outProcGuiConfigPath;
	// The APO starts a headless host during cold configuration load. Ask that
	// host to release the per-session lease before the interactive host starts.
	signalOutProcPanel(L"HostHandoff");
	appendOutProcDebugLog("open panel hostId=" + hostId + " config=" + outProcGuiConfigPath);

	qint64 pid = 0;
	if (!QProcess::startDetached(hostPath, arguments, QCoreApplication::applicationDirPath(), &pid))
	{
		QMessageBox::warning(this, tr("VST plugin"), tr("Could not start the out-of-process VST host."));
		QFile::remove(outProcGuiConfigPath);
		outProcGuiConfigPath.clear();
		return;
	}

	outProcGuiRunning = true;
	outProcGuiPid = pid;
	outProcGuiHidden = false;
	ui->openPanelButton->setText(tr("Hide panel"));
	ui->statusLabel->setText(tr("Out-of-process VST panel is open"));
	idleTimer.start();
}

bool VSTPluginFilterGUI::signalOutProcPanel(const wchar_t* suffix)
{
	QString objectName = makeOutProcObjectName(hostId, suffix);
	HANDLE eventHandle = OpenEventW(EVENT_MODIFY_STATE, FALSE, reinterpret_cast<LPCWSTR>(objectName.utf16()));
	if (eventHandle == NULL)
	{
		appendOutProcDebugLog("signal " + QString::fromWCharArray(suffix) + " hostId=" + hostId + " open failed gle=" + QString::number(GetLastError()));
		return false;
	}
	const BOOL ok = SetEvent(eventHandle);
	CloseHandle(eventHandle);
	appendOutProcDebugLog("signal " + QString::fromWCharArray(suffix) + " hostId=" + hostId + " ok=" + QString(ok ? "true" : "false") + " gle=" + QString::number(GetLastError()));
	return ok == TRUE;
}

bool VSTPluginFilterGUI::consumeOutProcPanelSignal(const wchar_t* suffix)
{
	QString objectName = makeOutProcObjectName(hostId, suffix);
	HANDLE eventHandle = OpenEventW(SYNCHRONIZE, FALSE, reinterpret_cast<LPCWSTR>(objectName.utf16()));
	if (eventHandle == NULL)
		return false;
	const DWORD waitResult = WaitForSingleObject(eventHandle, 0);
	CloseHandle(eventHandle);
	return waitResult == WAIT_OBJECT_0;
}

static bool terminateOutProcPidForHostId(const QString& hostId)
{
	QString objectName = makeOutProcObjectName(hostId, L"GuiInfo");
	HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, reinterpret_cast<LPCWSTR>(objectName.utf16()));
	if (mapping == NULL)
	{
		appendOutProcDebugLog("pid mapping open failed hostId=" + hostId + " gle=" + QString::number(GetLastError()));
		return false;
	}

	OutProcGuiInfo* info = static_cast<OutProcGuiInfo*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(OutProcGuiInfo)));
	if (info == nullptr)
	{
		CloseHandle(mapping);
		appendOutProcDebugLog("pid mapping view failed hostId=" + hostId + " gle=" + QString::number(GetLastError()));
		return false;
	}

	const DWORD pid = (info->magic == OUTPROC_GUI_INFO_MAGIC && info->version == OUTPROC_GUI_INFO_VERSION) ? info->processId : 0;
	UnmapViewOfFile(info);
	CloseHandle(mapping);

	if (pid == 0 || pid == GetCurrentProcessId())
	{
		appendOutProcDebugLog("pid mapping invalid hostId=" + hostId + " pid=" + QString::number(pid));
		return false;
	}

	HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
	if (process == NULL)
	{
		appendOutProcDebugLog("pid mapping open process failed hostId=" + hostId + " pid=" + QString::number(pid) + " gle=" + QString::number(GetLastError()));
		return false;
	}

	TerminateProcess(process, 0);
	WaitForSingleObject(process, 1000);
	CloseHandle(process);
	appendOutProcDebugLog("pid mapping terminated hostId=" + hostId + " pid=" + QString::number(pid));
	return true;
}

static QString makeOutProcPidPath(const QString& hostId)
{
	QString safeId = hostId;
	for (QChar& ch : safeId)
	{
		const bool ok = ch.isLetterOrNumber() || ch == '-' || ch == '_';
		if (!ok)
			ch = '_';
	}
	return QDir::temp().absoluteFilePath("EqApoOutProcHost-" + safeId + ".pid");
}

static bool terminateOutProcPidFileForHostId(const QString& hostId)
{
	const QString path = makeOutProcPidPath(hostId);
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
	{
		appendOutProcDebugLog("pid file open failed hostId=" + hostId + " path=" + path);
		return false;
	}

	bool ok = false;
	const DWORD pid = file.readAll().trimmed().toULong(&ok);
	file.close();
	if (!ok || pid == 0 || pid == GetCurrentProcessId())
	{
		appendOutProcDebugLog("pid file invalid hostId=" + hostId + " path=" + path + " pid=" + QString::number(pid));
		return false;
	}

	HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
	if (process == NULL)
	{
		appendOutProcDebugLog("pid file open process failed hostId=" + hostId + " pid=" + QString::number(pid) + " gle=" + QString::number(GetLastError()));
		return false;
	}

	TerminateProcess(process, 0);
	WaitForSingleObject(process, 1000);
	CloseHandle(process);
	QFile::remove(path);
	appendOutProcDebugLog("pid file terminated hostId=" + hostId + " pid=" + QString::number(pid) + " path=" + path);
	return true;
}

static bool terminateOutProcPid(qint64 pid, const QString& hostId)
{
	if (pid <= 0 || static_cast<DWORD>(pid) == GetCurrentProcessId())
		return false;

	HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
	if (process == NULL)
	{
		appendOutProcDebugLog("detached pid open process failed hostId=" + hostId + " pid=" + QString::number(pid) + " gle=" + QString::number(GetLastError()));
		return false;
	}

	TerminateProcess(process, 0);
	WaitForSingleObject(process, 1000);
	CloseHandle(process);
	appendOutProcDebugLog("detached pid terminated hostId=" + hostId + " pid=" + QString::number(pid));
	return true;
}

void VSTPluginFilterGUI::closeOutProcPanel()
{
	if (!outProcGuiRunning)
		return;

	signalOutProcPanel(L"GuiHide");
	outProcGuiHidden = true;

	if (ui != nullptr)
	{
		ui->openPanelButton->setText(tr("Show panel"));
		ui->statusLabel->setText(tr("Out-of-process VST host"));
	}
}

void VSTPluginFilterGUI::terminateOutProcPanel()
{
	idleTimer.stop();
	appendOutProcDebugLog("terminate panel hostId=" + hostId + " running=" + QString(outProcGuiRunning ? "true" : "false") + " pid=" + QString::number(outProcGuiPid));
	signalOutProcPanel(L"GuiExit");
	terminateOutProcPidForHostId(hostId);
	terminateOutProcPidFileForHostId(hostId);
	terminateOutProcPid(outProcGuiPid, hostId);

	if (!outProcGuiConfigPath.isEmpty())
	{
		OutProcVSTConfig updatedConfig;
		if (OutProcReadVSTConfig(outProcGuiConfigPath.toStdWString(), updatedConfig))
		{
			chunkData = updatedConfig.chunkData;
			paramMap = updatedConfig.paramMap;
			outProcParameterDescriptors = convertOutProcParameterDescriptors(
				updatedConfig.parameterDescriptors);
		}
		QFile::remove(outProcGuiConfigPath);
	}
	outProcGuiConfigPath.clear();
	outProcGuiRunning = false;
	outProcGuiPid = 0;
	outProcGuiHidden = false;
}

void VSTPluginFilterGUI::applyDialog()
{
	if (capturePluginStateIfChanged())
		updateModel();
	updatePermissionWarning();
}

void VSTPluginFilterGUI::autoApplyToggled(bool checked)
{
	autoApplyDialog = checked;
}

void VSTPluginFilterGUI::initPlugin()
{
	if (effect != NULL)
		return;

	QString text;
	const char* statusLevel = "danger";
	if (library->getLibPath() == L"")
	{
		text = tr("No file selected.");
	}
	else
	{
		int result = library->initialize();
		if (result < 0)
		{
			switch (result)
			{
			case AbstractLibrary::FILE_NOT_FOUND:
				text = tr("File not found.");
				break;
			case AbstractLibrary::LOADING_FAILED:
				text = tr("Library could not be loaded.");
				break;
			case AbstractLibrary::FUNCTIONS_MISSING:
				text = tr("Library does not contain needed functions.");
				break;
			case AbstractLibrary::WRONG_ARCHITECTURE:
#ifdef _WIN64
				int bitDepth = 64;
#else
				int bitDepth = 32;
#endif
				text = tr("Library has the wrong architecture. Only %1-bit libraries are supported.").arg(bitDepth);
				break;
			}
		}
		else
		{
			effect = new VSTPluginInstance(library, 1, vst3ClassIndex);
			if (effect->initialize())
			{
				effect->setLanguage(QLocale().language() == QLocale::German ? 2 : 1);
				effect->setAutomateFunc(bind(&VSTPluginFilterGUI::onAutomate, this));

				statusLevel = "normal";
				text = QString::fromStdWString(effect->getName());
			}
			else
			{
				delete effect;
				effect = NULL;

				text = tr("Plugin crashed during initialization.");
			}
		}
	}

	ui->statusLabel->setProperty("statusLevel", statusLevel);
	ui->statusLabel->style()->unpolish(ui->statusLabel);
	ui->statusLabel->style()->polish(ui->statusLabel);
	ui->statusLabel->setText(text);
}

void VSTPluginFilterGUI::releasePluginInstance()
{
	if (effect == NULL)
		return;

	idleTimer.stop();
	effect->setAutomateFunc(nullptr);
	effect->setParameterAutomateFunc(nullptr);
	effect->setSizeWindowFunc(nullptr);
	effect->stopEditing();

	VSTPluginInstance* instance = effect;
	effect = NULL;
	delete instance;
}

void VSTPluginFilterGUI::on_pathLineEdit_editingFinished()
{
	const QString requestedPath = canonicalVSTPath(ui->pathLineEdit->text());
	const QString currentPath = canonicalVSTPath(
		QString::fromStdWString(library->getLibPath()));
	if (QString::compare(requestedPath, currentPath, Qt::CaseInsensitive) != 0)
	{
		if (!currentPath.isEmpty() &&
			(!chunkData.empty() || !paramMap.empty() || !midiConfig.empty()) &&
			QMessageBox::question(
				this,
				tr("Change VST plug-in?"),
				tr("Changing the plug-in may clear its saved state and MIDI mappings. Continue?"),
				QMessageBox::Yes | QMessageBox::Cancel,
				QMessageBox::Cancel) != QMessageBox::Yes)
		{
			QDir pluginsDir(QString::fromStdWString(
				VSTPluginLibrary::getDefaultPluginPath()));
			QString displayPath = QDir::toNativeSeparators(
				pluginsDir.relativeFilePath(currentPath));
			if (displayPath.startsWith(QDir::toNativeSeparators("../../")))
				displayPath = currentPath;
			ui->pathLineEdit->setText(displayPath);
			return;
		}
		int oldId = 0;
		if (outProcMode)
		{
			terminateOutProcPanel();
			hostId = QUuid::createUuid().toString(QUuid::WithoutBraces);
		}
		if (effect != NULL)
		{
			oldId = effect->uniqueID();
			releasePluginInstance();
		}

		library = VSTPluginLibrary::getInstance(requestedPath.toStdWString());
		vst3ClassIndex = 0;
		refreshVST3ClassComboBox();
		if (!outProcMode)
			initPlugin();

		if (outProcMode || effect == NULL || oldId == 0 || effect->uniqueID() != oldId)
		{
			chunkData = L"";
			paramMap.clear();
			midiConfig.clear();
			outProcParameterDescriptors.clear();
			updateMidiButton();
		}

		updateModel();
		updatePermissionWarning();
	}
}

bool VSTPluginFilterGUI::capturePluginStateIfChanged()
{
	if (effect == NULL)
		return false;

	std::wstring updatedChunkData;
	std::unordered_map<std::wstring, float> updatedParamMap;
	effect->readFromEffect(updatedChunkData, updatedParamMap);
	if (updatedChunkData == chunkData && updatedParamMap == paramMap)
		return false;

	chunkData = std::move(updatedChunkData);
	paramMap = std::move(updatedParamMap);
	return true;
}

std::vector<VSTParameterDescriptor> VSTPluginFilterGUI::availableMidiParameters() const
{
	auto eligibleParameters = [](const std::vector<VSTParameterDescriptor>& source) {
		std::vector<VSTParameterDescriptor> result;
		result.reserve(source.size());
		for (const VSTParameterDescriptor& parameter : source)
		{
			if (!parameter.readOnly && !parameter.hidden)
				result.push_back(parameter);
		}
		return result;
	};

	if (effect != NULL)
		return eligibleParameters(effect->getParameterDescriptors());
	if (!outProcParameterDescriptors.empty())
		return eligibleParameters(outProcParameterDescriptors);
	// A VST3 parameter title cannot be recovered from its stable ParamID.
	// Require one successful host snapshot instead of presenting invented names.
	if (outProcMode && library->isVST3())
		return {};

	std::vector<VSTParameterDescriptor> result;
	for (const auto& entry : paramMap)
	{
		const std::wstring& key = entry.first;
		if (key.size() < 2 || key[0] != L'#')
			continue;
		wchar_t* end = NULL;
		const unsigned long id = wcstoul(key.c_str() + 1, &end, 10);
		if (end == key.c_str() + 1 || id > UINT32_MAX)
			continue;

		VSTParameterDescriptor descriptor;
		descriptor.api = library->isVST3()
			? VSTParameterApi::VST3 : VSTParameterApi::VST2;
		descriptor.stableId = static_cast<std::uint32_t>(id);
		descriptor.normalizedValue = entry.second;
		if (descriptor.api == VSTParameterApi::VST2)
		{
			if (*end != L':' || *(end + 1) == L'\0')
				continue;
			descriptor.name = end + 1;
		}
		else
		{
			if (*end != L'\0')
				continue;
			descriptor.name.clear();
		}
		result.push_back(std::move(descriptor));
	}
	std::sort(result.begin(), result.end(),
		[](const VSTParameterDescriptor& left,
			const VSTParameterDescriptor& right) {
			return left.stableId < right.stableId;
		});
	return result;
}

void VSTPluginFilterGUI::updateMidiButton()
{
	VSTMidiConfiguration configuration;
	const bool valid = !midiConfig.empty() &&
		VSTMidiBindingCodec::deserialize(midiConfig, configuration);
	if (valid && !configuration.bindings.empty())
	{
		ui->midiButton->setText(tr("MIDI control… (%1)")
			.arg(configuration.bindings.size()));
		ui->midiButton->setToolTip(tr("%1 MIDI mapping(s) active")
			.arg(configuration.bindings.size()));
		ui->midiButton->setProperty("statusLevel", "normal");
	}
	else
	{
		ui->midiButton->setText(tr("MIDI control…"));
		ui->midiButton->setToolTip(tr(
			"Bind MIDI knobs, faders, and buttons to VST parameters"));
		ui->midiButton->setProperty("statusLevel",
			midiConfig.empty() ? QVariant() : QVariant("warning"));
	}
	ui->midiButton->style()->unpolish(ui->midiButton);
	ui->midiButton->style()->polish(ui->midiButton);
}

void VSTPluginFilterGUI::refreshVST3ClassComboBox()
{
	ui->vst3ClassComboBox->blockSignals(true);
	ui->vst3ClassComboBox->clear();
	ui->vst3ClassComboBox->setVisible(false);
	ui->classLabel->setVisible(false);

	if (library != NULL && library->isVST3() && library->initialize() >= 0 && library->getVST3ClassCount() > 1)
	{
		for (int i = 0; i < library->getVST3ClassCount(); ++i)
			ui->vst3ClassComboBox->addItem(QString::fromUtf8(library->getVST3ClassInfo(i).name));
		if (vst3ClassIndex < 0 || vst3ClassIndex >= library->getVST3ClassCount())
			vst3ClassIndex = 0;
		ui->vst3ClassComboBox->setCurrentIndex(vst3ClassIndex);
		ui->vst3ClassComboBox->setVisible(true);
		ui->classLabel->setVisible(true);
	}

	ui->vst3ClassComboBox->blockSignals(false);
}

void VSTPluginFilterGUI::on_selectButton_clicked()
{
	QDir pluginsDir(QString::fromStdWString(VSTPluginLibrary::getDefaultPluginPath()));

	QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
	QString lastDir = settings.value("vst/lastDir", "").toString();
	if (lastDir == "")
		lastDir = pluginsDir.absolutePath();

	QFileInfo fileInfo(lastDir);
	QString path = ui->pathLineEdit->text();
	if (path.length() > 0)
		fileInfo.setFile(pluginsDir, path);

	QFileDialog dialog(this, tr("Select VST plugin"), fileInfo.absoluteFilePath(), "*.dll *.vst3");
	dialog.setFileMode(QFileDialog::ExistingFile);
	dialog.setNameFilter(tr("VST plugins (*.dll *.vst3)"));
	if (path.length() > 0)
		dialog.selectFile(fileInfo.fileName());
	if (dialog.exec() == QDialog::Accepted)
	{
		QString absolutePath = dialog.selectedFiles().first();
		settings.setValue("vst/lastDir", QDir::toNativeSeparators(QFileInfo(absolutePath).absolutePath()));
		QString relativePath = pluginsDir.relativeFilePath(absolutePath);
		if (relativePath.startsWith("../../"))
			relativePath = absolutePath;
		ui->pathLineEdit->setText(QDir::toNativeSeparators(relativePath));
		on_pathLineEdit_editingFinished();
	}
}

void VSTPluginFilterGUI::on_idle()
{
	if (outProcMode && outProcGuiRunning && consumeOutProcPanelSignal(L"GuiHidden"))
	{
		outProcGuiHidden = true;
		ui->openPanelButton->setText(tr("Show panel"));
	}

	if (outProcMode && outProcGuiRunning && !outProcGuiConfigPath.isEmpty())
	{
		if (!lastReadTimer.isValid() || lastReadTimer.elapsed() > 500)
		{
			OutProcVSTConfig updatedConfig;
			if (OutProcReadVSTConfig(outProcGuiConfigPath.toStdWString(), updatedConfig))
			{
				std::vector<VSTParameterDescriptor> updatedDescriptors =
					convertOutProcParameterDescriptors(updatedConfig.parameterDescriptors);
				const bool stateChanged = chunkData != updatedConfig.chunkData ||
					paramMap != updatedConfig.paramMap;
				chunkData = updatedConfig.chunkData;
				paramMap = updatedConfig.paramMap;
				outProcParameterDescriptors = std::move(updatedDescriptors);
				if (stateChanged)
				{
					updateModel();
				}
			}
			lastReadTimer.restart();
		}
	}

	if (effect != NULL && autoApplyDialog)
	{
		const qint64 elapsed = lastReadTimer.isValid()
			? lastReadTimer.elapsed() : 1000;
		// A plug-in may omit automation notifications, so retain a slow
		// fallback poll. Continuous controls are coalesced to one save rather
		// than serializing and rebuilding the APO for every MIDI/drag event.
		if ((automationDirty && elapsed >= 75) || elapsed >= 1000)
		{
			if (capturePluginStateIfChanged())
				updateModel();
			automationDirty = false;
			lastReadTimer.restart();
		}
	}
}

void VSTPluginFilterGUI::onAutomate()
{
	if (autoApplyDialog)
		automationDirty = true;
}

void VSTPluginFilterGUI::onSizeWindow(int w, int h)
{
	Q_UNUSED(w);
	Q_UNUSED(h);
}

void VSTPluginFilterGUI::updatePermissionWarning()
{
	if (effect == NULL)
	{
		ui->warningTextEdit->setVisible(false);
		return;
	}

	ACCESS_MASK mask = GENERIC_READ;
	try
	{
		mask = RegistryHelper::getFileAccessForUser(library->getLibPath(), SECURITY_LOCAL_SERVICE_RID);
	}
	catch (RegistryException e)
	{
		// ignore
	}

	if ((mask & GENERIC_READ) != GENERIC_READ && (mask & FILE_GENERIC_READ) != FILE_GENERIC_READ)
	{
		QString text = tr("The library is not readable by the audio service.\nChange the file permissions or copy the file to the VSTPlugins directory.");

		ui->warningTextEdit->setPlainText(text);
		ui->warningTextEdit->setVisible(true);
		return;
	}

	QStringList files;
	if (chunkData != L"" && chunkData.length() < 100000)
	{
		QByteArray bytes = QByteArray::fromBase64(QString::fromStdWString(chunkData).toUtf8());
		QString string = QString::fromUtf8(bytes.data(), bytes.length());
		QRegularExpression regexp("[A-Za-z]:(?:\\\\[\\w \\(\\)-]+)+\\.[A-Za-z]{3}");
		QRegularExpressionMatchIterator it = regexp.globalMatch(string);
		while (it.hasNext())
		{
			QRegularExpressionMatch m = it.next();
			QString path = m.captured();
			QFile file(path);
			if (file.exists())
			{
				ACCESS_MASK mask = GENERIC_READ;
				try
				{
					mask = RegistryHelper::getFileAccessForUser(path.toStdWString(), SECURITY_LOCAL_SERVICE_RID);
				}
				catch (RegistryException e)
				{
					// ignore
				}

				if ((mask & GENERIC_READ) != GENERIC_READ && (mask & FILE_GENERIC_READ) != FILE_GENERIC_READ)
					files.append(path);
			}
		}
	}

	if (files.isEmpty())
	{
		ui->warningTextEdit->setVisible(false);
		ui->warningTextEdit->setPlainText("");
	}
	else
	{
		files.removeDuplicates();
		QString text = tr("The plugin seemingly accesses these files not readable by the audio service:\n"
				"%1\n"
				"Change the file permissions or copy the files to the config directory.").arg(files.join("\n"));
		ui->warningTextEdit->setPlainText(text);
		ui->warningTextEdit->setVisible(true);
	}
}
