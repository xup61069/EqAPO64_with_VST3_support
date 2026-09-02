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

#include <algorithm>
#include <QLabel>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QPushButton>
#include <QScreen>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>
#include "Editor/helpers/GUIHelper.h"
#include "VSTPluginFilterGUIDialog.h"
#include "ui_VSTPluginFilterGUIDialog.h"

using namespace std;
using namespace std::placeholders;

namespace
{
	constexpr int DEFAULT_EDITOR_WIDTH = 400;
	constexpr int DEFAULT_EDITOR_HEIGHT = 300;
	// QWidget permits much larger values, but corrupt plug-in dimensions must
	// not create an unbounded native child window. This still leaves ample room
	// for legitimate oversized editors, which remain reachable by scrolling.
	constexpr int MAX_EDITOR_DIMENSION = 16384;
}

VSTPluginFilterGUIDialog::VSTPluginFilterGUIDialog(QWidget* parent, VSTPluginInstance* effect, bool autoApply)
	: QDialog(parent, Qt::WindowCloseButtonHint),
	ui(new Ui::VSTPluginFilterGUIDialog),
	effect(effect)
{
	ui->setupUi(this);
	ui->autoApplyCheckBox->setChecked(autoApply);

	QString name = QString::fromStdWString(effect->getName());
	setWindowTitle(name);
	ui->editorScrollArea->setWidgetResizable(false);
	ui->frame->setFixedSize(DEFAULT_EDITOR_WIDTH, DEFAULT_EDITOR_HEIGHT);

	connect(&idleTimer, &QTimer::timeout, this, [this]() {
		if (this->editorStarted)
		{
			this->effect->doIdle();
			int width = 0;
			int height = 0;
			if (this->effect->getEditorSize(&width, &height))
				this->setEditorSize(width, height);
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
	setEditorSize(w, h);
}

void VSTPluginFilterGUIDialog::showEvent(QShowEvent* event)
{
	QDialog::showEvent(event);
	if (!screenChangeConnected && windowHandle() != nullptr)
	{
		connect(windowHandle(), &QWindow::screenChanged, this, [this](QScreen*) {
			constrainToAvailableScreen();
		});
		screenChangeConnected = true;
	}
	constrainToAvailableScreen();
	QTimer::singleShot(0, this, [this]() { startEditor(); });
}

QRect VSTPluginFilterGUIDialog::availableScreenGeometry() const
{
	QScreen* targetScreen = nullptr;
	if (windowHandle() != nullptr)
		targetScreen = windowHandle()->screen();
	if (targetScreen == nullptr && parentWidget() != nullptr)
		targetScreen = parentWidget()->screen();
	if (targetScreen == nullptr)
		targetScreen = QGuiApplication::screenAt(frameGeometry().center());
	if (targetScreen == nullptr)
		targetScreen = QGuiApplication::primaryScreen();
	return targetScreen != nullptr ? targetScreen->availableGeometry() : QRect();
}

void VSTPluginFilterGUIDialog::setEditorSize(int width, int height)
{
	if (width <= 0 || height <= 0)
		return;

	const QSize boundedSize(
		qBound(1, width, MAX_EDITOR_DIMENSION),
		qBound(1, height, MAX_EDITOR_DIMENSION));
	if (ui->frame->size() == boundedSize)
		return;

	ui->frame->setFixedSize(boundedSize);
	ui->frame->updateGeometry();
	constrainToAvailableScreen();
}

void VSTPluginFilterGUIDialog::constrainToAvailableScreen()
{
	const QRect available = availableScreenGeometry();
	if (!available.isValid())
		return;

	const QSize screenMargin = GUIHelper::scale(QSize(32, 48));
	const QSize maximumDialogSize(
		(std::max)(1, available.width() - screenMargin.width()),
		(std::max)(1, available.height() - screenMargin.height()));
	setMaximumSize(maximumDialogSize);

	const QMargins margins = ui->gridLayout->contentsMargins();
	const int horizontalSpacing = (std::max)(0, ui->gridLayout->horizontalSpacing());
	const int verticalSpacing = (std::max)(0, ui->gridLayout->verticalSpacing());
	const int scrollBarExtent = style()->pixelMetric(QStyle::PM_ScrollBarExtent);
	const QSize controlsSize(
		ui->buttonBox->sizeHint().width() + horizontalSpacing +
			ui->autoApplyCheckBox->sizeHint().width(),
		(std::max)(ui->buttonBox->sizeHint().height(),
			ui->autoApplyCheckBox->sizeHint().height()));

	QSize desiredSize(
		(std::max)(ui->frame->width() + scrollBarExtent,
			controlsSize.width()) + margins.left() + margins.right(),
		ui->frame->height() + scrollBarExtent + controlsSize.height() +
			verticalSpacing + margins.top() + margins.bottom());
	desiredSize = desiredSize.expandedTo(minimumSizeHint()).boundedTo(maximumDialogSize);
	resize(desiredSize);

	const QRect dialogFrame = frameGeometry();
	const int maximumX = (std::max)(available.left(), available.right() - dialogFrame.width() + 1);
	const int maximumY = (std::max)(available.top(), available.bottom() - dialogFrame.height() + 1);
	move(
		qBound(available.left(), dialogFrame.left(), maximumX),
		qBound(available.top(), dialogFrame.top(), maximumY));
}

void VSTPluginFilterGUIDialog::startEditor()
{
	if (editorStarted)
		return;
	editorStarted = true;

	// The frame is already owned by the scroll area before this native handle
	// is created, so the plug-in receives one stable parent HWND and is never
	// reparented after startEditing().
	HWND hwnd = (HWND)ui->frame->winId();
	short width = DEFAULT_EDITOR_WIDTH;
	short height = DEFAULT_EDITOR_HEIGHT;

	if (effect->startEditing(hwnd, &width, &height))
	{
		setEditorSize(width, height);
		effect->setSizeWindowFunc(bind(&VSTPluginFilterGUIDialog::onSizeWindow, this, _1, _2));
		idleTimer.start();
		return;
	}

	QVBoxLayout* layout = new QVBoxLayout(ui->frame);
	QLabel* label = new QLabel(tr("This VST3 plug-in did not expose an embeddable editor."), ui->frame);
	label->setWordWrap(true);
	label->setAlignment(Qt::AlignCenter);
	layout->addWidget(label);
	setEditorSize(420, 120);
}

void VSTPluginFilterGUIDialog::on_autoApplyCheckBox_clicked(bool checked)
{
	if (checked)
		getApplyButton()->click();
}
