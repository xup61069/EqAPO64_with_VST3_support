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

#include <QLabel>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include "VSTPluginFilterGUIDialog.h"
#include "ui_VSTPluginFilterGUIDialog.h"

using namespace std;
using namespace std::placeholders;

VSTPluginFilterGUIDialog::VSTPluginFilterGUIDialog(QWidget* parent, VSTPluginInstance* effect, bool autoApply)
	: QDialog(parent, Qt::WindowCloseButtonHint),
	ui(new Ui::VSTPluginFilterGUIDialog),
	effect(effect)
{
	ui->setupUi(this);
	ui->autoApplyCheckBox->setChecked(autoApply);

	QString name = QString::fromStdWString(effect->getName());
	setWindowTitle(name);
	ui->frame->setFixedSize(400, 300);

	connect(&idleTimer, &QTimer::timeout, this, [this]() {
		if (this->editorStarted)
		{
			this->effect->doIdle();
			int width = 0;
			int height = 0;
			if (this->effect->getEditorSize(&width, &height) && width > 0 && height > 0
				&& (this->ui->frame->width() != width || this->ui->frame->height() != height))
				this->ui->frame->setFixedSize(width, height);
		}
	});
	idleTimer.setInterval(16);
}

VSTPluginFilterGUIDialog::~VSTPluginFilterGUIDialog()
{
	idleTimer.stop();
	effect->stopEditing();
	effect->setSizeWindowFunc(nullptr);

	delete ui;
}

QPushButton* VSTPluginFilterGUIDialog::getApplyButton()
{
	return ui->buttonBox->button(QDialogButtonBox::Apply);
}

QCheckBox* VSTPluginFilterGUIDialog::getAutoApplyCheckBox()
{
	return ui->autoApplyCheckBox;
}

void VSTPluginFilterGUIDialog::onSizeWindow(int w, int h)
{
	ui->frame->setFixedSize(w, h);
}

void VSTPluginFilterGUIDialog::showEvent(QShowEvent* event)
{
	QDialog::showEvent(event);
	QTimer::singleShot(0, this, [this]() { startEditor(); });
}

void VSTPluginFilterGUIDialog::startEditor()
{
	if (editorStarted)
		return;
	editorStarted = true;

	HWND hwnd = (HWND)ui->frame->winId();
	short width = 400;
	short height = 300;

	if (effect->startEditing(hwnd, &width, &height))
	{
		ui->frame->setFixedSize(width, height);
		effect->setSizeWindowFunc(bind(&VSTPluginFilterGUIDialog::onSizeWindow, this, _1, _2));
		idleTimer.start();
		return;
	}

	QVBoxLayout* layout = new QVBoxLayout(ui->frame);
	QLabel* label = new QLabel(tr("This VST3 plug-in did not expose an embeddable editor."), ui->frame);
	label->setWordWrap(true);
	label->setAlignment(Qt::AlignCenter);
	layout->addWidget(label);
	ui->frame->setFixedSize(420, 120);
}

void VSTPluginFilterGUIDialog::on_autoApplyCheckBox_clicked(bool checked)
{
	if (checked)
		getApplyButton()->click();
}
