/*
	This file is part of EqualizerAPO, a system-wide equalizer.
	Copyright (C) 2016  Jonas Thedering

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

#include "GUIHelper.h"

#include <QApplication>
#include <QFont>
#include <QFontMetrics>
#include <QIcon>
#include <QIconEngine>
#include <QPainter>
#include <QPalette>
#include <QScreen>
#include <QStyleHints>

#include <algorithm>

namespace
{
	class AccentAddIconEngine final : public QIconEngine
	{
	public:
		QIconEngine* clone() const override
		{
			return new AccentAddIconEngine(*this);
		}

		QString key() const override
		{
			return QStringLiteral("EqApoAccentAddIcon");
		}

		void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State) override
		{
			const QPalette palette = QApplication::palette();
			const QColor color = mode == QIcon::Disabled
				? palette.color(QPalette::Disabled, QPalette::ButtonText)
				: palette.color(QPalette::Highlight);
			const qreal side = (std::min)(rect.width(), rect.height());
			const qreal arm = side * 0.29;
			const qreal stroke = (std::max)(1.0, side * 0.12);
			const QPointF center = QRectF(rect).center();

			painter->save();
			painter->setRenderHint(QPainter::Antialiasing, false);
			painter->setPen(QPen(color, stroke, Qt::SolidLine, Qt::SquareCap));
			painter->drawLine(
				QPointF(center.x() - arm, center.y()),
				QPointF(center.x() + arm, center.y()));
			painter->drawLine(
				QPointF(center.x(), center.y() - arm),
				QPointF(center.x(), center.y() + arm));
			painter->restore();
		}
	};
}

QSize GUIHelper::scale(QSize size)
{
	if (qApp->testAttribute(Qt::AA_Use96Dpi))
		return size;

	qreal dpi = QGuiApplication::primaryScreen()->logicalDotsPerInchX();
	return QSize(qRound(size.width() * dpi / 96), qRound(size.height() * dpi / 96));
}

int GUIHelper::scale(double pixel)
{
	if (qApp->testAttribute(Qt::AA_Use96Dpi))
		return qRound(pixel);

	qreal dpi = QGuiApplication::primaryScreen()->logicalDotsPerInchX();
	return qRound(pixel * dpi / 96);
}

double GUIHelper::scaleZoom(double zoom)
{
	if (qApp->testAttribute(Qt::AA_Use96Dpi))
		return zoom;

	qreal dpi = QGuiApplication::primaryScreen()->logicalDotsPerInchX();
	return zoom * dpi / 96;
}

double GUIHelper::invScale(int pixel)
{
	if (qApp->testAttribute(Qt::AA_Use96Dpi))
		return pixel;

	qreal dpi = QGuiApplication::primaryScreen()->logicalDotsPerInchX();
	return pixel * 96 / dpi;
}

double GUIHelper::invScaleZoom(double zoom)
{
	if (qApp->testAttribute(Qt::AA_Use96Dpi))
		return zoom;

	qreal dpi = QGuiApplication::primaryScreen()->logicalDotsPerInchX();
	return zoom * 96 / dpi;
}

bool GUIHelper::isDarkMode()
{
	return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

QIcon GUIHelper::createAccentAddIcon()
{
	return QIcon(new AccentAddIconEngine);
}
