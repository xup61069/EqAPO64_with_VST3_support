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

#include <QToolBar>
#include <QApplication>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QStyle>
#include <QStyleOption>

#include <algorithm>

#include "Editor/helpers/GUIHelper.h"
#include "FilterTableRow.h"
#include "ui_FilterTableRow.h"

namespace
{
	class ElidingCommandLabel final : public QLabel
	{
	public:
		explicit ElidingCommandLabel(const QString& command, QWidget* parent = nullptr)
			: QLabel(command, parent)
		{
			setObjectName(QStringLiteral("elidingCommandLabel"));
			setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
			setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
			setMinimumWidth(0);
			setToolTip(command);
			setAccessibleName(command);
		}

		QSize sizeHint() const override
		{
			QSize size = QLabel::sizeHint();
			const int preferredWidth = fontMetrics().horizontalAdvance(QLatin1Char('M')) * 48;
			size.setWidth((std::min)(size.width(), preferredWidth));
			return size;
		}

		QSize minimumSizeHint() const override
		{
			QSize size = QLabel::minimumSizeHint();
			size.setWidth(0);
			return size;
		}

	protected:
		void paintEvent(QPaintEvent*) override
		{
			QPainter painter(this);
			QStyleOption option;
			option.initFrom(this);
			style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);

			QRect textRect = rect().marginsRemoved(contentsMargins());
			const int labelMargin = margin();
			textRect.adjust(labelMargin, labelMargin, -labelMargin, -labelMargin);
			const QString displayText = fontMetrics().elidedText(
				text(), Qt::ElideMiddle, (std::max)(0, textRect.width()));
			style()->drawItemText(
				&painter,
				textRect,
				alignment(),
				palette(),
				isEnabled(),
				displayText,
				foregroundRole());
		}
	};
}

FilterTableRow::FilterTableRow(FilterTable* table, int number, FilterTable::Item* item, IFilterGUI* gui)
	: QWidget(table),
	ui(new Ui::FilterTableRow)
{
	ui->setupUi(this);
	ui->actionAdd->setIcon(GUIHelper::createAccentAddIcon());
	ui->actionRemove->setIcon(GUIHelper::createThemeIcon(GUIHelper::ThemeIcon::Remove));
	ui->actionEditText->setIcon(GUIHelper::createThemeIcon(GUIHelper::ThemeIcon::Edit));
	setAttribute(Qt::WA_StyledBackground, false);
	ui->labelNumber->setMinimumWidth(GUIHelper::scale(38));
	ui->horizontalLayout->setContentsMargins(
		0,
		GUIHelper::scale(1),
		GUIHelper::scale(6),
		0);
	QFont numberFont = font();
	numberFont.setWeight(QFont::DemiBold);
	ui->labelNumber->setFont(numberFont);

	this->table = table;
	this->item = item;
	this->gui = gui;

	ui->labelNumber->setText(QString("%0").arg(number));

	ui->toolBar->addAction(ui->actionAdd);
	ui->toolBar->addAction(ui->actionRemove);
	ui->toolBar->addAction(ui->actionEditText);
	ui->toolBar->setOrientation(Qt::Horizontal);
	ui->toolBar->updateMaximumHeight();
	ui->horizontalLayout->setAlignment(ui->toolBar, Qt::AlignTop);

	ui->stackedWidget->setContentsMargins(
		GUIHelper::scale(4),
		GUIHelper::scale(4),
		GUIHelper::scale(2),
		GUIHelper::scale(4));

	if (gui != NULL)
	{
		connect(gui, SIGNAL(updateModel()), this, SLOT(updateModel()));
		ui->stackedWidget->addWidget(gui);
	}
	else
	{
		ui->stackedWidget->addWidget(new ElidingCommandLabel(item->text, ui->stackedWidget));
	}
	ui->stackedWidget->setCurrentIndex(1);
}

FilterTableRow::~FilterTableRow()
{
	delete ui;
}

QRect FilterTableRow::getHeaderRect()
{
	QRect rect(0, 0, ui->labelNumber->geometry().right() + 1, height());

	return rect;
}

void FilterTableRow::editText()
{
	if (!ui->actionEditText->isChecked())
		ui->actionEditText->trigger();
}

void FilterTableRow::mouseDoubleClickEvent(QMouseEvent*)
{
	if (gui == NULL && ui->stackedWidget->currentIndex() == 1)
		ui->actionEditText->trigger();
}

void FilterTableRow::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);

	const qreal inset = GUIHelper::scale(1.0);
	const QRectF card = QRectF(rect()).adjusted(
		inset,
		inset,
		-inset,
		-inset);
	const bool selected = table->getSelectedItems().contains(item);
	const bool focused = table->getFocusedItem() == item;
	const QPalette rowPalette = palette();
	const QColor surface = rowPalette.color(QPalette::Base);
	QColor accent = rowPalette.color(QPalette::Highlight);
	QColor border = rowPalette.color(QPalette::Mid);
	const bool highContrast = qApp
		&& qApp->property("eqapoModernThemeHighContrast").toBool();

	painter.setPen(Qt::NoPen);
	painter.setBrush(surface);
	painter.drawRect(card);

	if ((selected || focused) && !highContrast)
	{
		QColor overlay = selected ? accent : rowPalette.color(QPalette::AlternateBase);
		overlay.setAlpha(selected ? 38 : 90);
		painter.setBrush(overlay);
		painter.drawRect(card);
	}

	QPainterPath clip;
	clip.addRect(card);
	painter.save();
	painter.setClipPath(clip);
	QRectF rail = card;
	rail.setRight(ui->labelNumber->geometry().right() + GUIHelper::scale(3));
	if (!highContrast)
	{
		QColor railColor = accent;
		railColor.setAlpha(selected ? 72 : 27);
		painter.fillRect(rail, railColor);
	}
	QColor railEdge = accent;
	if (!highContrast)
		railEdge.setAlpha(selected ? 220 : 90);
	painter.setPen(QPen(railEdge, GUIHelper::scale(selected ? 2.0 : 1.0)));
	painter.drawLine(
		QPointF(rail.right(), rail.top()),
		QPointF(rail.right(), rail.bottom()));
	painter.restore();

	if (selected)
		border = accent;
	else if (focused)
	{
		border = rowPalette.color(QPalette::WindowText);
		if (!highContrast)
			border.setAlpha(105);
	}
	painter.setBrush(Qt::NoBrush);
	painter.setPen(QPen(
		border,
		GUIHelper::scale(selected && focused ? 2.0 : 1.0)));
	painter.drawRect(card);
}

void FilterTableRow::updateModel()
{
	IFilterGUI* sender = qobject_cast<IFilterGUI*>(QObject::sender());
	QString command;
	QString parameters;
	sender->store(command, parameters);

	item->text = command + ": " + parameters;

	qDebug("Updated item text to %s", item->text.toStdString().c_str());
}

void FilterTableRow::on_actionAdd_triggered()
{
	QMenu* menu = table->createAddPopupMenu();
	QRect rect = ui->toolBar->actionGeometry(ui->actionAdd);
	QPoint p = ui->toolBar->mapToGlobal(QPoint(rect.x(), rect.y() + rect.height()));
	QAction* action = menu->exec(p);
	ui->actionAdd->setChecked(false);
	if (action != NULL)
	{
		FilterTemplate t = action->data().value<FilterTemplate>();
		QString line = t.getLine();
		table->addLine(line, item);
		table->updateGuis();
	}
}

void FilterTableRow::on_actionRemove_triggered()
{
	table->removeItem(item);
	table->updateGuis();
}

void FilterTableRow::on_actionEditText_triggered(bool checked)
{
	if (checked)
	{
		if (!lastEditTime.isValid() || lastEditTime.msecsTo(QDateTime::currentDateTimeUtc()) > 100)
		{
			ui->lineEdit->setText(item->text);
			ui->stackedWidget->setCurrentIndex(0);
			ui->lineEdit->setFocus();
		}
		else
		{
			ui->actionEditText->setChecked(false);
		}
	}
}

void FilterTableRow::on_lineEdit_editingFinished()
{
	if (ui->stackedWidget->currentIndex() == 0 && !editingDone)
	{
		if (ui->lineEdit->text() != item->text)
		{
			editingDone = true;
			item->text = ui->lineEdit->text();
			table->updateModel();
			// set focus to table so that enter key does not cause scrolling down
			table->setFocus();
			table->updateGuis();
		}
		else
		{
			ui->stackedWidget->setCurrentIndex(1);
			lastEditTime = QDateTime::currentDateTimeUtc();
			ui->actionEditText->setChecked(false);
		}
	}
}

void FilterTableRow::on_lineEdit_editingCanceled()
{
	if (ui->stackedWidget->currentIndex() == 0)
	{
		// will cause editingFinished to be called, so prevent committing
		editingDone = true;
		table->setFocus();
		ui->stackedWidget->setCurrentIndex(1);
		editingDone = false;
		ui->actionEditText->setChecked(false);
	}
}
