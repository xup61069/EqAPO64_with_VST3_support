/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2026 Equalizer APO contributors
*/

#pragma once

#include <QDialog>
#include <QTimer>
#include <string>
#include <vector>

#include "helpers/VSTMidiBindingCodec.h"
#include "helpers/WinMidiInput.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;

class VSTMidiMappingDialog : public QDialog
{
	Q_OBJECT

public:
	explicit VSTMidiMappingDialog(
		const std::vector<VSTParameterDescriptor>& parameters,
		const std::wstring& encodedConfiguration,
		QWidget* parent = nullptr);
	~VSTMidiMappingDialog() override;

	std::wstring getEncodedConfiguration() const;

protected:
	void accept() override;
	void reject() override;

private slots:
	void refreshDevices();
	void toggleLearn();
	void pollMidi();
	void removeSelectedMapping();
	void clearMappings();

private:
	void stopLearning();
	void rebuildMappingTable();
	void updateActionState();
	void updateConnectionStatus(bool force = false);
	void setLearnStatus(const QString& text, const char* level);
	bool selectConfiguredDevice();
	QString parameterDisplayName(const VSTMidiBinding& binding) const;
	const VSTParameterDescriptor* selectedParameter() const;

	std::vector<VSTParameterDescriptor> parameters;
	std::vector<WinMidiDeviceInfo> devices;
	VSTMidiConfiguration configuration;
	std::wstring encodedConfiguration;
	WinMidiInput midiInput;
	QTimer midiPollTimer;
	QComboBox* deviceComboBox = nullptr;
	QPushButton* refreshButton = nullptr;
	QComboBox* parameterComboBox = nullptr;
	QComboBox* modeComboBox = nullptr;
	QCheckBox* anyChannelCheckBox = nullptr;
	QPushButton* learnButton = nullptr;
	QLabel* learnStatusLabel = nullptr;
	QTableWidget* mappingTable = nullptr;
	QPushButton* removeButton = nullptr;
	QPushButton* clearButton = nullptr;
	bool learning = false;
	VSTMidiConnectionState displayedConnectionState = VSTMidiConnectionState::Stopped;
};
