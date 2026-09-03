/*
	This file is part of EqualizerAPO, a system-wide equalizer.
	Copyright (C) 2024  Jonas Thedering

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

#include "stdafx.h"
#include "OpacityIconEngine.h"

namespace
{
	QColor iconColor(QIcon::Mode mode, PaletteIconGlyph glyph)
	{
		const QPalette palette = QApplication::palette();
		if (mode == QIcon::Disabled || glyph == PaletteIconGlyph::unavailable)
			return palette.color(QPalette::Disabled, QPalette::Text);

		if (glyph == PaletteIconGlyph::error)
			return palette.color(QPalette::Active, QPalette::WindowText);

		return palette.color(QPalette::Active, QPalette::Highlight);
	}

	QColor glyphColor(QIcon::Mode mode, PaletteIconGlyph glyph)
	{
		const QPalette palette = QApplication::palette();
		if (mode == QIcon::Disabled)
			return palette.color(QPalette::Disabled, QPalette::Base);
		if (glyph == PaletteIconGlyph::error)
			return palette.color(QPalette::Active, QPalette::Base);
		return palette.color(QPalette::Active, QPalette::HighlightedText);
	}
}

OpacityIconEngine::OpacityIconEngine(PaletteIconGlyph glyph)
	:glyph(glyph)
{
}

void OpacityIconEngine::paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state)
{
	Q_UNUSED(state);

	painter->save();
	painter->setRenderHint(QPainter::Antialiasing, true);
	painter->setOpacity(opacity);

	const qreal side = qMin(rect.width(), rect.height());
	if (side <= 0)
	{
		painter->restore();
		return;
	}

	painter->translate(rect.center());
	painter->scale(side / 24.0, side / 24.0);
	painter->translate(-12.0, -12.0);

	const QColor primary = iconColor(mode, glyph);
	const QColor foreground = glyphColor(mode, glyph);
	QPen pen(primary, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
	painter->setPen(pen);
	painter->setBrush(Qt::NoBrush);

	switch (glyph)
	{
	case PaletteIconGlyph::device:
	{
		QPolygonF speaker;
		speaker << QPointF(4.0, 9.0) << QPointF(8.0, 9.0) << QPointF(13.0, 5.0)
			<< QPointF(13.0, 19.0) << QPointF(8.0, 15.0) << QPointF(4.0, 15.0);
		painter->setBrush(primary);
		painter->drawPolygon(speaker);
		painter->setBrush(Qt::NoBrush);
		painter->drawArc(QRectF(11.0, 7.0, 7.0, 10.0), -62 * 16, 124 * 16);
		painter->drawArc(QRectF(11.0, 4.5, 11.0, 15.0), -58 * 16, 116 * 16);
		break;
	}
	case PaletteIconGlyph::waiting:
		painter->drawEllipse(QRectF(3.5, 3.5, 17.0, 17.0));
		painter->setBrush(primary);
		painter->drawEllipse(QRectF(10.0, 2.2, 4.0, 4.0));
		break;
	case PaletteIconGlyph::success:
		painter->setPen(Qt::NoPen);
		painter->setBrush(primary);
		painter->drawEllipse(QRectF(2.5, 2.5, 19.0, 19.0));
		painter->setPen(QPen(foreground, 2.1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		painter->drawPolyline(QPolygonF() << QPointF(7.0, 12.2) << QPointF(10.4, 15.5) << QPointF(17.2, 8.2));
		break;
	case PaletteIconGlyph::warning:
	{
		QPolygonF triangle;
		triangle << QPointF(12.0, 2.5) << QPointF(22.0, 20.5) << QPointF(2.0, 20.5);
		painter->setPen(Qt::NoPen);
		painter->setBrush(primary);
		painter->drawPolygon(triangle);
		painter->setPen(QPen(foreground, 2.0, Qt::SolidLine, Qt::RoundCap));
		painter->drawLine(QPointF(12.0, 8.0), QPointF(12.0, 14.0));
		painter->drawPoint(QPointF(12.0, 17.2));
		break;
	}
	case PaletteIconGlyph::error:
		painter->setPen(Qt::NoPen);
		painter->setBrush(primary);
		painter->drawEllipse(QRectF(2.5, 2.5, 19.0, 19.0));
		painter->setPen(QPen(foreground, 2.0, Qt::SolidLine, Qt::RoundCap));
		painter->drawLine(QPointF(8.0, 8.0), QPointF(16.0, 16.0));
		painter->drawLine(QPointF(16.0, 8.0), QPointF(8.0, 16.0));
		break;
	case PaletteIconGlyph::unavailable:
		painter->drawEllipse(QRectF(3.5, 3.5, 17.0, 17.0));
		painter->drawLine(QPointF(7.5, 12.0), QPointF(16.5, 12.0));
		break;
	}

	painter->restore();
}

QPixmap OpacityIconEngine::pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state)
{
	QPixmap result(size);
	result.fill(Qt::transparent);
	QPainter painter(&result);
	paint(&painter, result.rect(), mode, state);
	return result;
}

OpacityIconEngine* OpacityIconEngine::clone() const
{
	OpacityIconEngine* engine = new OpacityIconEngine(glyph);
	engine->setOpacity(opacity);
	return engine;
}

void OpacityIconEngine::setOpacity(qreal opacity)
{
	this->opacity = opacity;
}

QIcon createPaletteIcon(PaletteIconGlyph glyph)
{
	return QIcon(new OpacityIconEngine(glyph));
}
