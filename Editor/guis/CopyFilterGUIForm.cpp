/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2015  Jonas Thedering

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

#include <algorithm>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QToolButton>

#include "Editor/helpers/GUIHelper.h"
#include "CopyFilterGUIRow.h"
#include "CopyFilterGUIForm.h"

using namespace std;

CopyFilterGUIForm::CopyFilterGUIForm(QWidget* parent)
	: QWidget(parent)
{
	setMinimumWidth(0);
	setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
}

void CopyFilterGUIForm::load(vector<Assignment> assignments)
{
	formRows.clear();
	if (gridLayout != NULL)
	{
		while (QLayoutItem* item = gridLayout->takeAt(0))
		{
			if (QWidget* widget = item->widget())
			{
				widget->hide();
				widget->deleteLater();
			}
			delete item;
		}
		delete gridLayout;
		gridLayout = NULL;
	}

	gridLayout = new QGridLayout(this);
	gridLayout->setContentsMargins(2, 2, 2, 2);
	gridLayout->setHorizontalSpacing(4);
	gridLayout->setVerticalSpacing(4);
	gridLayout->setColumnStretch(1, 1);
	const QSize actionIconSize = GUIHelper::scale(QSize(16, 16));
	const int actionButtonSize = (std::max)(
		GUIHelper::scale(24), fontMetrics().height() + 4);

	int row = 0;
	for (Assignment assignment : assignments)
	{
		QString oc = QString::fromStdWString(assignment.targetChannel);

		bool firstSummand = true;
		for (Assignment::Summand summand : assignment.sourceSum)
		{
			QComboBox* targetComboBox = NULL;
			if (firstSummand)
			{
				QLabel* channelLabel = new QLabel(tr("Channel"), this);
				targetComboBox = new QComboBox(this);
				targetComboBox->setEditable(true);
				targetComboBox->setMinimumContentsLength(10);
				targetComboBox->setSizeAdjustPolicy(
					QComboBox::AdjustToMinimumContentsLengthWithIcon);
				targetComboBox->setSizePolicy(
					QSizePolicy::Expanding, QSizePolicy::Fixed);
				for (wstring channelName : inputChannelNames)
					targetComboBox->addItem(QString::fromStdWString(channelName));
				targetComboBox->setEditText(oc);
				targetComboBox->setToolTip(oc);
				channelLabel->setBuddy(targetComboBox);
				connect(targetComboBox, SIGNAL(editTextChanged(QString)), this, SIGNAL(updateModel()));
				connect(targetComboBox, SIGNAL(editTextChanged(QString)), this, SIGNAL(updateChannels()));
				connect(targetComboBox, &QComboBox::editTextChanged,
					targetComboBox, [targetComboBox](const QString& text) {
						targetComboBox->setToolTip(text);
					});
				gridLayout->addWidget(channelLabel, row, 0);
				gridLayout->addWidget(targetComboBox, row, 1, 1, 3);
				row++;

				firstSummand = false;
			}

			QLabel* operatorLabel = new QLabel(
				targetComboBox == NULL ? "+" : "=", this);
			operatorLabel->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
			gridLayout->addWidget(operatorLabel, row, 0);

			CopyFilterGUIRow* rowWidget = new CopyFilterGUIRow(summand, inputChannelNames, this);
			connect(rowWidget, SIGNAL(updateModel()), this, SIGNAL(updateModel()));
			connect(rowWidget, SIGNAL(updateChannels()), this, SIGNAL(updateChannels()));
			gridLayout->addWidget(rowWidget, row, 1);

			const QString addSummandText = tr("Add summand");
			QToolButton* addSummandButton = new QToolButton(this);
			addSummandButton->setIcon(GUIHelper::createAccentAddIcon());
			addSummandButton->setAutoRaise(true);
			addSummandButton->setIconSize(actionIconSize);
			addSummandButton->setMinimumSize(actionButtonSize, actionButtonSize);
			addSummandButton->setToolTip(addSummandText);
			addSummandButton->setAccessibleName(addSummandText);
			gridLayout->addWidget(addSummandButton, row, 2);
			connect(addSummandButton, SIGNAL(pressed()), this, SLOT(addSummand()));

			const QString removeText = assignment.sourceSum.size() == 1
				? tr("Remove assignment") : tr("Remove summand");
			QToolButton* removeButton = new QToolButton(this);
			removeButton->setIcon(GUIHelper::createThemeIcon(GUIHelper::ThemeIcon::Remove));
			removeButton->setAutoRaise(true);
			removeButton->setIconSize(actionIconSize);
			removeButton->setMinimumSize(actionButtonSize, actionButtonSize);
			removeButton->setToolTip(removeText);
			removeButton->setAccessibleName(removeText);
			gridLayout->addWidget(removeButton, row, 3);
			connect(removeButton, SIGNAL(pressed()), this, SLOT(remove()));

			formRows.append({
				targetComboBox, rowWidget, addSummandButton, removeButton});

			row++;
		}
	}

	QPushButton* addAssignmentButton = new QPushButton(this);
	addAssignmentButton->setText(tr("Add assignment"));
	addAssignmentButton->setIcon(GUIHelper::createAccentAddIcon());
	gridLayout->addWidget(addAssignmentButton, row++, 0, 1, 4);
	connect(addAssignmentButton, SIGNAL(pressed()), this, SLOT(addAssignment()));
	gridLayout->setRowStretch(row, 1);
}

void CopyFilterGUIForm::setChannelNames(const vector<wstring>& channelNames)
{
	inputChannelNames = channelNames;

	for (const FormRow& formRow : formRows)
	{
		if (formRow.targetComboBox != NULL)
		{
			QComboBox* targetComboBox = formRow.targetComboBox;
			targetComboBox->blockSignals(true);
			QString text = targetComboBox->currentText();
			targetComboBox->clear();
			for (wstring channelName : channelNames)
				targetComboBox->addItem(QString::fromStdWString(channelName));
			targetComboBox->setEditText(text);
			targetComboBox->setToolTip(text);
			targetComboBox->blockSignals(false);
		}

		formRow.summandWidget->setChannelNames(channelNames);
	}
}

vector<Assignment> CopyFilterGUIForm::buildAssignments(QWidget* pressedButton)
{
	vector<Assignment> assignments;

	Assignment* currentAssignment = NULL;
	for (const FormRow& formRow : formRows)
	{
		if (formRow.targetComboBox != NULL)
		{
			QString oc = formRow.targetComboBox->currentText().trimmed();
			assignments.push_back(Assignment());
			currentAssignment = &assignments.back();
			currentAssignment->targetChannel = oc.toStdWString();
		}

		if (currentAssignment != NULL)
		{
			if (formRow.removeButton == pressedButton)
				continue;

			currentAssignment->sourceSum.push_back(
				formRow.summandWidget->buildSummand());

			if (formRow.addButton == pressedButton)
			{
				Assignment::Summand newSummand;
				newSummand.channel = L" ";
				newSummand.factor = 1.0;
				newSummand.isDecibel = false;
				currentAssignment->sourceSum.push_back(newSummand);
			}
		}
	}

	// remove empty assignments (can be caused by remove button)
	assignments.erase(remove_if(assignments.begin(), assignments.end(), [](Assignment assignment) {
		return assignment.sourceSum.empty();
	}), assignments.end());

	return assignments;
}

void CopyFilterGUIForm::addSummand()
{
	QWidget* addSummandButton = qobject_cast<QWidget*>(sender());

	vector<Assignment> assignments = buildAssignments(addSummandButton);

	load(assignments);
}

void CopyFilterGUIForm::remove()
{
	QWidget* removeButton = qobject_cast<QWidget*>(sender());

	vector<Assignment> assignments = buildAssignments(removeButton);

	load(assignments);

	emit updateModel();
	emit updateChannels();
}

void CopyFilterGUIForm::addAssignment()
{
	vector<Assignment> assignments = buildAssignments();

	Assignment newAssignment;
	Assignment::Summand summand;
	summand.channel = L" ";
	summand.factor = 1.0;
	summand.isDecibel = false;
	newAssignment.sourceSum.push_back(summand);

	assignments.push_back(newAssignment);

	load(assignments);
}
