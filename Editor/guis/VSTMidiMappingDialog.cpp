/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2026 Equalizer APO contributors
*/

#include "VSTMidiMappingDialog.h"

#include "Editor/helpers/GUIHelper.h"

#include <algorithm>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

namespace
{
	bool sameDevice(
		const VSTMidiDeviceIdentity& left,
		const VSTMidiDeviceIdentity& right)
	{
		return left.manufacturerId == right.manufacturerId &&
			left.productId == right.productId &&
			left.driverVersion == right.driverVersion &&
			left.nameOrdinal == right.nameOrdinal &&
			_wcsicmp(left.name.c_str(), right.name.c_str()) == 0;
	}

	bool sameStableDevice(
		const VSTMidiDeviceIdentity& left,
		const VSTMidiDeviceIdentity& right)
	{
		return left.manufacturerId == right.manufacturerId &&
			left.productId == right.productId &&
			left.nameOrdinal == right.nameOrdinal &&
			_wcsicmp(left.name.c_str(), right.name.c_str()) == 0;
	}

	QString messageTypeName(VSTMidiMessageType type)
	{
		switch (type)
		{
		case VSTMidiMessageType::ControlChange:
			return VSTMidiMappingDialog::tr("Control change");
		case VSTMidiMessageType::Note:
			return VSTMidiMappingDialog::tr("Note");
		case VSTMidiMessageType::PitchBend:
			return VSTMidiMappingDialog::tr("Pitch bend");
		}
		return VSTMidiMappingDialog::tr("Unknown");
	}

	QString valueModeName(VSTMidiValueMode mode)
	{
		return mode == VSTMidiValueMode::Toggle
			? VSTMidiMappingDialog::tr("Toggle")
			: VSTMidiMappingDialog::tr("Absolute");
	}
}

VSTMidiMappingDialog::VSTMidiMappingDialog(
	const std::vector<VSTParameterDescriptor>& parameters,
	const std::wstring& encodedConfiguration,
	QWidget* parent)
	: QDialog(parent),
	  parameters(parameters),
	  encodedConfiguration(encodedConfiguration)
{
	setObjectName(QStringLiteral("vstMidiMappingDialog"));
	setWindowTitle(tr("VST MIDI control"));
	setModal(true);
	QSize availableSize(1280, 720);
	QScreen* targetScreen = parent != nullptr ? parent->screen() : screen();
	if (targetScreen != nullptr)
		availableSize = targetScreen->availableGeometry().size() - QSize(32, 32);
	const QSize desiredSize = GUIHelper::scale(
		QSize(820, 540)).boundedTo(availableSize);
	setMinimumSize(GUIHelper::scale(
		QSize(520, 360)).boundedTo(desiredSize));
	resize(desiredSize);

	if (!encodedConfiguration.empty() &&
		!VSTMidiBindingCodec::deserialize(encodedConfiguration, configuration))
	{
		configuration = VSTMidiConfiguration();
		QMessageBox::warning(
			this,
			tr("MIDI configuration ignored"),
			tr("The saved MIDI mapping is invalid. The VST remains active, but MIDI control is disabled until you save a new mapping."));
	}

	QVBoxLayout* root = new QVBoxLayout(this);
	QLabel* introduction = new QLabel(tr(
		"Bind a hardware MIDI knob, fader, or button to an automatable VST parameter. "
		"Mappings run in the audio host even after Configuration Editor is closed."), this);
	introduction->setWordWrap(true);
	introduction->setAccessibleName(tr("MIDI mapping help"));
	root->addWidget(introduction);

	QFormLayout* form = new QFormLayout;
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	QWidget* deviceRow = new QWidget(this);
	QHBoxLayout* deviceLayout = new QHBoxLayout(deviceRow);
	deviceLayout->setContentsMargins(0, 0, 0, 0);
	deviceComboBox = new QComboBox(deviceRow);
	deviceComboBox->setObjectName(QStringLiteral("midiDeviceComboBox"));
	deviceComboBox->setAccessibleName(tr("MIDI input device"));
	deviceComboBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	refreshButton = new QPushButton(tr("Refresh"), deviceRow);
	refreshButton->setObjectName(QStringLiteral("refreshMidiDevicesButton"));
	refreshButton->setAccessibleName(tr("Refresh MIDI devices"));
	deviceLayout->addWidget(deviceComboBox, 1);
	deviceLayout->addWidget(refreshButton);
	QLabel* deviceLabel = new QLabel(tr("MIDI input device:"), this);
	deviceLabel->setBuddy(deviceComboBox);
	form->addRow(deviceLabel, deviceRow);

	parameterComboBox = new QComboBox(this);
	parameterComboBox->setObjectName(QStringLiteral("vstParameterComboBox"));
	parameterComboBox->setAccessibleName(tr("VST target parameter"));
	parameterComboBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	for (std::size_t index = 0; index < this->parameters.size(); ++index)
	{
		const VSTParameterDescriptor& parameter = this->parameters[index];
		if (parameter.readOnly || parameter.hidden)
			continue;
		QString name = QString::fromStdWString(parameter.name).trimmed();
		if (name.isEmpty())
			name = tr("Parameter %1").arg(parameter.stableId);
		const QString api = parameter.api == VSTParameterApi::VST3
			? QStringLiteral("VST3") : QStringLiteral("VST2");
		parameterComboBox->addItem(
			tr("%1 · %2 #%3").arg(name, api).arg(parameter.stableId),
			static_cast<qulonglong>(index));
	}
	QLabel* parameterLabel = new QLabel(tr("Target parameter:"), this);
	parameterLabel->setBuddy(parameterComboBox);
	form->addRow(parameterLabel, parameterComboBox);

	QWidget* learnOptions = new QWidget(this);
	QHBoxLayout* learnOptionsLayout = new QHBoxLayout(learnOptions);
	learnOptionsLayout->setContentsMargins(0, 0, 0, 0);
	modeComboBox = new QComboBox(learnOptions);
	modeComboBox->setObjectName(QStringLiteral("midiValueModeComboBox"));
	modeComboBox->setAccessibleName(tr("MIDI value mode"));
	modeComboBox->addItem(tr("Automatic (recommended)"), 0);
	modeComboBox->addItem(tr("Absolute knob or fader"),
		static_cast<int>(VSTMidiValueMode::Absolute));
	modeComboBox->addItem(tr("Toggle button"),
		static_cast<int>(VSTMidiValueMode::Toggle));
	anyChannelCheckBox = new QCheckBox(tr("Any channel"), learnOptions);
	anyChannelCheckBox->setToolTip(tr(
		"Accept the learned controller on every MIDI channel instead of only the channel used while learning."));
	learnOptionsLayout->addWidget(modeComboBox, 1);
	learnOptionsLayout->addWidget(anyChannelCheckBox);
	QLabel* modeLabel = new QLabel(tr("Control behavior:"), this);
	modeLabel->setBuddy(modeComboBox);
	form->addRow(modeLabel, learnOptions);
	root->addLayout(form);

	QHBoxLayout* learnLayout = new QHBoxLayout;
	learnButton = new QPushButton(tr("Learn MIDI control"), this);
	learnButton->setObjectName(QStringLiteral("learnMidiButton"));
	learnButton->setAccessibleName(tr("Learn MIDI control"));
	learnStatusLabel = new QLabel(tr("Choose a target, then start learning."), this);
	learnStatusLabel->setObjectName(QStringLiteral("midiLearnStatusLabel"));
	learnStatusLabel->setWordWrap(true);
	learnStatusLabel->setAccessibleName(tr("MIDI learn status"));
	learnLayout->addWidget(learnButton);
	learnLayout->addWidget(learnStatusLabel, 1);
	root->addLayout(learnLayout);

	mappingTable = new QTableWidget(this);
	mappingTable->setObjectName(QStringLiteral("midiMappingTable"));
	mappingTable->setAccessibleName(tr("MIDI mappings"));
	mappingTable->setColumnCount(5);
	mappingTable->setHorizontalHeaderLabels(QStringList()
		<< tr("Message") << tr("Channel") << tr("Control")
		<< tr("Behavior") << tr("VST parameter"));
	mappingTable->setSelectionBehavior(QAbstractItemView::SelectRows);
	mappingTable->setSelectionMode(QAbstractItemView::SingleSelection);
	mappingTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
	mappingTable->verticalHeader()->setVisible(false);
	mappingTable->horizontalHeader()->setStretchLastSection(true);
	mappingTable->horizontalHeader()->setSectionResizeMode(
		QHeaderView::ResizeToContents);
	mappingTable->horizontalHeader()->setSectionResizeMode(
		4, QHeaderView::Stretch);
	root->addWidget(mappingTable, 1);

	QHBoxLayout* actions = new QHBoxLayout;
	removeButton = new QPushButton(tr("Remove selected"), this);
	clearButton = new QPushButton(tr("Clear all"), this);
	actions->addWidget(removeButton);
	actions->addWidget(clearButton);
	actions->addStretch(1);
	QDialogButtonBox* buttonBox = new QDialogButtonBox(
		QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
	if (QPushButton* saveButton = buttonBox->button(QDialogButtonBox::Save))
		saveButton->setText(tr("Save mappings"));
	actions->addWidget(buttonBox);
	root->addLayout(actions);

	connect(refreshButton, &QPushButton::clicked,
		this, &VSTMidiMappingDialog::refreshDevices);
	connect(learnButton, &QPushButton::clicked,
		this, &VSTMidiMappingDialog::toggleLearn);
	connect(&midiPollTimer, &QTimer::timeout,
		this, &VSTMidiMappingDialog::pollMidi);
	connect(mappingTable, &QTableWidget::itemSelectionChanged,
		this, &VSTMidiMappingDialog::updateActionState);
	connect(removeButton, &QPushButton::clicked,
		this, &VSTMidiMappingDialog::removeSelectedMapping);
	connect(clearButton, &QPushButton::clicked,
		this, &VSTMidiMappingDialog::clearMappings);
	connect(buttonBox, &QDialogButtonBox::accepted,
		this, &VSTMidiMappingDialog::accept);
	connect(buttonBox, &QDialogButtonBox::rejected,
		this, &VSTMidiMappingDialog::reject);
	midiPollTimer.setTimerType(Qt::CoarseTimer);
	midiPollTimer.setInterval(20);

	refreshDevices();
	rebuildMappingTable();
	updateActionState();
}

VSTMidiMappingDialog::~VSTMidiMappingDialog()
{
	stopLearning();
}

std::wstring VSTMidiMappingDialog::getEncodedConfiguration() const
{
	return encodedConfiguration;
}

void VSTMidiMappingDialog::refreshDevices()
{
	const VSTMidiDeviceIdentity selected = deviceComboBox->currentIndex() >= 0 &&
		static_cast<std::size_t>(deviceComboBox->currentIndex()) < devices.size()
		? devices[deviceComboBox->currentIndex()].identity : configuration.device;
	stopLearning();
	devices = WinMidiInput::enumerateDevices();
	QSignalBlocker blocker(deviceComboBox);
	deviceComboBox->clear();
	for (const WinMidiDeviceInfo& device : devices)
	{
		QString name = QString::fromStdWString(device.identity.name);
		if (device.identity.nameOrdinal > 0)
			name += tr(" · device %1").arg(device.identity.nameOrdinal + 1);
		deviceComboBox->addItem(name);
	}

	configuration.device = selected;
	if (!selectConfiguredDevice() && !configuration.device.empty())
	{
		WinMidiDeviceInfo unavailable;
		unavailable.identity = configuration.device;
		devices.push_back(unavailable);
		deviceComboBox->addItem(tr("%1 (disconnected)")
			.arg(QString::fromStdWString(configuration.device.name)));
		deviceComboBox->setCurrentIndex(deviceComboBox->count() - 1);
		setLearnStatus(tr(
			"The saved MIDI device is unavailable. Existing mappings are kept; "
			"reconnect it and refresh, choose another device, or cancel without saving."),
			"warning");
	}
	else if (deviceComboBox->currentIndex() < 0 && deviceComboBox->count() > 0)
	{
		deviceComboBox->setCurrentIndex(0);
	}
	else if (deviceComboBox->count() == 0)
	{
		setLearnStatus(tr(
			"No MIDI input devices were found. Existing mappings are kept; "
			"connect a device and refresh, or cancel without saving."),
			"warning");
	}
	updateActionState();
}

bool VSTMidiMappingDialog::selectConfiguredDevice()
{
	// Prefer the complete identity, then tolerate a driver update for the same
	// physical input. Persist the current enumerated identity so the device is
	// not shown twice as both current and disconnected on later refreshes.
	for (std::size_t index = 0; index < devices.size(); ++index)
	{
		if (sameDevice(devices[index].identity, configuration.device))
		{
			deviceComboBox->setCurrentIndex(static_cast<int>(index));
			return true;
		}
	}
	for (std::size_t index = 0; index < devices.size(); ++index)
	{
		if (sameStableDevice(devices[index].identity, configuration.device))
		{
			configuration.device = devices[index].identity;
			deviceComboBox->setCurrentIndex(static_cast<int>(index));
			return true;
		}
	}
	return false;
}

const VSTParameterDescriptor* VSTMidiMappingDialog::selectedParameter() const
{
	if (parameterComboBox->currentIndex() < 0)
		return nullptr;
	const std::size_t index = static_cast<std::size_t>(
		parameterComboBox->currentData().toULongLong());
	return index < parameters.size() ? &parameters[index] : nullptr;
}

void VSTMidiMappingDialog::toggleLearn()
{
	if (learning)
	{
		stopLearning();
		setLearnStatus(tr("MIDI learn cancelled."), "normal");
		return;
	}
	if (selectedParameter() == nullptr || deviceComboBox->currentIndex() < 0 ||
		static_cast<std::size_t>(deviceComboBox->currentIndex()) >= devices.size())
	{
		setLearnStatus(tr("Select a MIDI device and a VST parameter first."), "warning");
		return;
	}

	configuration.device = devices[deviceComboBox->currentIndex()].identity;
	learning = true;
	learnButton->setText(tr("Cancel learning"));
	displayedConnectionState = VSTMidiConnectionState::Stopped;
	setLearnStatus(tr("Connecting to the MIDI input… You can cancel learning at any time."), "warning");
	if (!midiInput.start(configuration.device))
	{
		learning = false;
		learnButton->setText(tr("Learn MIDI control"));
		updateConnectionStatus(true);
		return;
	}
	midiPollTimer.start();
	updateConnectionStatus(true);
}

void VSTMidiMappingDialog::stopLearning()
{
	midiPollTimer.stop();
	midiInput.stop();
	learning = false;
	displayedConnectionState = VSTMidiConnectionState::Stopped;
	if (learnButton != nullptr)
		learnButton->setText(tr("Learn MIDI control"));
}

void VSTMidiMappingDialog::pollMidi()
{
	updateConnectionStatus();
	WinMidiShortMessage message;
	while (midiInput.tryPop(message))
	{
		const std::uint8_t status = static_cast<std::uint8_t>(
			message.packedMessage & 0xff);
		const std::uint8_t statusType = status & 0xf0;
		const std::uint8_t channel = status & 0x0f;
		const std::uint8_t data1 = static_cast<std::uint8_t>(
			(message.packedMessage >> 8) & 0x7f);
		const std::uint8_t data2 = static_cast<std::uint8_t>(
			(message.packedMessage >> 16) & 0x7f);

		VSTMidiMessageType messageType;
		if (statusType == 0xb0)
			messageType = VSTMidiMessageType::ControlChange;
		else if (statusType == 0x90 && data2 > 0)
			messageType = VSTMidiMessageType::Note;
		else if (statusType == 0xe0)
			messageType = VSTMidiMessageType::PitchBend;
		else
			continue;

		const VSTParameterDescriptor* parameter = selectedParameter();
		if (parameter == nullptr)
			return;
		VSTMidiBinding binding;
		binding.messageType = messageType;
		binding.channel = anyChannelCheckBox->isChecked() ? 0xff : channel;
		binding.controlNumber = messageType == VSTMidiMessageType::PitchBend
			? 0 : data1;
		const int selectedMode = modeComboBox->currentData().toInt();
		binding.valueMode = selectedMode == 0
			? (messageType == VSTMidiMessageType::Note
				? VSTMidiValueMode::Toggle : VSTMidiValueMode::Absolute)
			: static_cast<VSTMidiValueMode>(selectedMode);
		binding.parameterType = parameter->api == VSTParameterApi::VST3
			? VSTMidiParameterType::VST3ParamID
			: VSTMidiParameterType::VST2ParameterIndex;
		binding.parameterId = parameter->stableId;
		binding.parameterName = parameter->name;
		binding.stepCount = parameter->stepCount;

		// A physical source controls only one target within a row. Learning the
		// same source again replaces its previous target instead of creating an
		// ambiguous fan-out.
		configuration.bindings.erase(
			std::remove_if(
				configuration.bindings.begin(),
				configuration.bindings.end(),
				[&binding](const VSTMidiBinding& current) {
					return VSTMidiBindingCodec::sourcesConflict(current, binding);
				}),
			configuration.bindings.end());
		configuration.bindings.push_back(binding);

		stopLearning();
		rebuildMappingTable();
		setLearnStatus(
			tr("Mapped %1 on channel %2 to %3.")
				.arg(messageTypeName(messageType))
				.arg(channel + 1)
				.arg(QString::fromStdWString(parameter->name)),
			"normal");
		return;
	}

}

void VSTMidiMappingDialog::updateConnectionStatus(bool force)
{
	const VSTMidiConnectionState state = midiInput.connectionState();
	if (!force && state == displayedConnectionState)
		return;
	displayedConnectionState = state;

	switch (state)
	{
	case VSTMidiConnectionState::Connecting:
		setLearnStatus(tr(
			"Connecting to the MIDI input… You can cancel learning at any time."),
			"warning");
		break;
	case VSTMidiConnectionState::Connected:
		setLearnStatus(tr(
			"Connected and listening. Move a knob or fader, or press a MIDI button…"),
			"normal");
		break;
	case VSTMidiConnectionState::Busy:
		setLearnStatus(tr(
			"This MIDI input is busy in another application or process. Close the other MIDI user and wait for automatic retry, or cancel learning."),
			"danger");
		break;
	case VSTMidiConnectionState::DeviceUnavailable:
		setLearnStatus(tr(
			"The MIDI input is unavailable. Reconnect it and wait for automatic retry, refresh to choose another device, or cancel learning. Existing mappings are unchanged."),
			"warning");
		break;
	case VSTMidiConnectionState::BrokerCapacityExceeded:
		setLearnStatus(tr(
			"Too many MIDI inputs or listeners are active in this process. Close another MIDI mapping window or disable an unused mapping, then try again."),
			"danger");
		break;
	case VSTMidiConnectionState::Stopped:
		setLearnStatus(tr(
			"The MIDI input could not be started. Refresh the device list or cancel without saving; existing mappings are unchanged."),
			"danger");
		break;
	}
}

QString VSTMidiMappingDialog::parameterDisplayName(
	const VSTMidiBinding& binding) const
{
	for (const VSTParameterDescriptor& parameter : parameters)
	{
		const bool apiMatches =
			(binding.parameterType == VSTMidiParameterType::VST3ParamID) ==
			(parameter.api == VSTParameterApi::VST3);
		if (!apiMatches || parameter.stableId != binding.parameterId)
			continue;
		if (parameter.api == VSTParameterApi::VST2 &&
			parameter.name != binding.parameterName)
			continue;
		return QString::fromStdWString(parameter.name);
	}
	return tr("%1 (parameter unavailable)")
		.arg(QString::fromStdWString(binding.parameterName));
}

void VSTMidiMappingDialog::rebuildMappingTable()
{
	mappingTable->setRowCount(static_cast<int>(configuration.bindings.size()));
	for (std::size_t row = 0; row < configuration.bindings.size(); ++row)
	{
		const VSTMidiBinding& binding = configuration.bindings[row];
		const QString control = binding.messageType == VSTMidiMessageType::PitchBend
			? tr("Bend") : QString::number(binding.controlNumber);
		const QString channel = binding.channel == 0xff
			? tr("Any") : QString::number(binding.channel + 1);
		const QStringList values = {
			messageTypeName(binding.messageType), channel, control,
			valueModeName(binding.valueMode), parameterDisplayName(binding)
		};
		for (int column = 0; column < values.size(); ++column)
			mappingTable->setItem(static_cast<int>(row), column,
				new QTableWidgetItem(values[column]));
	}
	updateActionState();
}

void VSTMidiMappingDialog::removeSelectedMapping()
{
	const int row = mappingTable->currentRow();
	if (row < 0 || static_cast<std::size_t>(row) >= configuration.bindings.size())
		return;
	configuration.bindings.erase(configuration.bindings.begin() + row);
	rebuildMappingTable();
}

void VSTMidiMappingDialog::clearMappings()
{
	if (configuration.bindings.empty())
		return;
	if (QMessageBox::question(
		this, tr("Clear MIDI mappings?"),
		tr("Remove every MIDI mapping from this VST row?"),
		QMessageBox::Yes | QMessageBox::Cancel,
		QMessageBox::Cancel) != QMessageBox::Yes)
		return;
	configuration.bindings.clear();
	rebuildMappingTable();
}

void VSTMidiMappingDialog::updateActionState()
{
	const bool canLearn = deviceComboBox->count() > 0 &&
		parameterComboBox->count() > 0;
	learnButton->setEnabled(canLearn);
	removeButton->setEnabled(mappingTable->currentRow() >= 0);
	clearButton->setEnabled(!configuration.bindings.empty());
}

void VSTMidiMappingDialog::setLearnStatus(
	const QString& text, const char* level)
{
	learnStatusLabel->setText(text);
	learnStatusLabel->setAccessibleDescription(text);
	learnStatusLabel->setProperty("statusLevel", QString::fromLatin1(level));
	learnStatusLabel->style()->unpolish(learnStatusLabel);
	learnStatusLabel->style()->polish(learnStatusLabel);
}

void VSTMidiMappingDialog::accept()
{
	stopLearning();
	if (configuration.bindings.empty())
	{
		encodedConfiguration.clear();
		QDialog::accept();
		return;
	}
	if (deviceComboBox->currentIndex() < 0 ||
		static_cast<std::size_t>(deviceComboBox->currentIndex()) >= devices.size())
	{
		QMessageBox::warning(this, tr("MIDI device required"),
			tr("Choose the MIDI input device used by these mappings."));
		return;
	}
	configuration.device = devices[deviceComboBox->currentIndex()].identity;
	if (!VSTMidiBindingCodec::serialize(configuration, encodedConfiguration))
	{
		QMessageBox::critical(this, tr("MIDI mappings not saved"),
			tr("The MIDI mapping data exceeded its safety limits."));
		return;
	}
	QDialog::accept();
}

void VSTMidiMappingDialog::reject()
{
	stopLearning();
	QDialog::reject();
}
