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

#include <QMouseEvent>
#include <QPainter>

#include <algorithm>

#include "Editor/helpers/GUIHelper.h"
#include "ResizeCorner.h"

ResizeCorner::ResizeCorner(FilterTable* filterTable, QSize minimumSize, QSize maximumSize, std::function<QSize()> getFunc, std::function<void(QSize)> setFunc, QWidget* parent)
	: QLabel(parent), minimumSize(minimumSize), maximumSize(maximumSize), getFunc(getFunc), setFunc(setFunc), filterTable(filterTable)
{
	setCursor(Qt::SizeFDiagCursor);
}

QSize ResizeCorner::sizeHint() const
{
	return GUIHelper::scale(QSize(16, 16));
}

void ResizeCorner::paintEvent(QPaintEvent* event)
{
	QLabel::paintEvent(event);

	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);
	const QPalette::ColorGroup group = isEnabled() ? QPalette::Active : QPalette::Disabled;
	const QColor color = palette().color(group, QPalette::ButtonText);
	painter.setPen(QPen(color, (std::max)(1, GUIHelper::scale(1.0)),
		Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));

	const int margin = (std::max)(1, GUIHelper::scale(2.0));
	const int step = (std::max)(2, GUIHelper::scale(4.0));
	const QRect gripRect = rect().adjusted(margin, margin, -margin, -margin);
	for (int line = 1; line <= 3; ++line)
	{
		const int span = line * step;
		painter.drawLine(
			QPoint(gripRect.right() - span, gripRect.bottom()),
			QPoint(gripRect.right(), gripRect.bottom() - span));
	}
}

void ResizeCorner::mousePressEvent(QMouseEvent* event)
{
	filterTable->setMinimumHeightHint(filterTable->height());
	QSize size = getFunc();
	QPoint globalPosition = event->globalPosition().toPoint();
	offsetX = size.width() - globalPosition.x();
	offsetY = size.height() - globalPosition.y();
}

void ResizeCorner::mouseMoveEvent(QMouseEvent* event)
{
	QSize size;
	QPoint globalPosition = event->globalPosition().toPoint();
	size.setWidth(offsetX + globalPosition.x());
	size.setHeight(offsetY + globalPosition.y());
	size = size.expandedTo(minimumSize);
	size = size.boundedTo(maximumSize);
	setFunc(size);
}

void ResizeCorner::mouseReleaseEvent(QMouseEvent*)
{
	filterTable->setMinimumHeightHint(0);
}
