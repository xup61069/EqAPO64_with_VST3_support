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
#include <QPainter>
#include <QMouseEvent>
#include <QVector>

#include "Editor/helpers/GUIHelper.h"
#include "FrequencyPlotView.h"
#include "FrequencyPlotHRuler.h"

using namespace std;

FrequencyPlotHRuler::FrequencyPlotHRuler(QWidget* parent)
	: QWidget(parent)
{
}

void FrequencyPlotHRuler::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	FrequencyPlotView* view = qobject_cast<FrequencyPlotView*>(parentWidget());
	FrequencyPlotScene* s = view->scene();
	QFontMetrics metrics = painter.fontMetrics();
	const auto clampTextCenter = [this, &metrics](qreal center, const QString& text, qreal extraMargin = 0.0)
	{
		const qreal halfTextWidth = metrics.boundingRect(text).width() / 2.0 + extraMargin;
		const qreal maxTextCenter = qMax(halfTextWidth, static_cast<qreal>(width()) - halfTextWidth);
		return qBound(halfTextWidth, center, maxTextCenter);
	};
	const QPalette rulerPalette = palette();
	painter.setPen(rulerPalette.color(QPalette::WindowText));

	QPointF topLeft = view->mapToScene(0, 0);
	QPointF bottomRight = view->mapToScene(view->viewport()->width(), view->viewport()->height());
	int offsetLeft = view->viewportMargins().left();
	double fromHz = s->xToHz(topLeft.x());
	double toHz = s->xToHz(bottomRight.x());
	struct TickLabel
	{
		QString text;
		qreal center;
		QRectF occupiedRect;
		bool draw = false;
	};
	QVector<TickLabel> labels;
	const qreal labelGap = GUIHelper::scale(6);
	const auto appendLabel = [&](double hz)
	{
		const double x = s->hzToX(hz);
		if (x == -1)
			return;

		const QString text = hz < 1000
			? QString("%0").arg(hz)
			: QString("%0k").arg(hz / 1000);
		const qreal center = clampTextCenter(
			x - topLeft.x() + offsetLeft + 1,
			text);
		const qreal halfWidth = metrics.horizontalAdvance(text) / 2.0;
		labels.append({
			text,
			center,
			QRectF(
				center - halfWidth - labelGap / 2.0,
				0,
				halfWidth * 2.0 + labelGap,
				height())
		});
	};

	const vector<double>& bands = s->getBands();
	if (bands.empty())
	{
		double hzBase = pow(10, floor(log10(fromHz)));
		fromHz = floor(fromHz / hzBase) * hzBase;
		fromHz += hzBase;
		if (round(fromHz / hzBase) >= 10)
			hzBase *= 10;
		for (double hz = fromHz; hz <= toHz;)
		{
			appendLabel(hz);

			hz += hzBase;
			if (round(hz / hzBase) >= 10)
				hzBase *= 10;
		}
	}
	else
	{
		vector<double>::const_iterator it = lower_bound(bands.cbegin(), bands.cend(), fromHz);
		for (; it != bands.cend() && *it < toHz; it++)
			appendLabel(*it);
	}

	if (!labels.isEmpty())
	{
		// Keep the first visible tick and reserve the last visible tick when
		// both fit. Fill the space between them without allowing adjacent
		// label bounds (including a scaled readability gap) to overlap.
		labels.first().draw = true;
		qreal occupiedRight = labels.first().occupiedRect.right();
		const qsizetype lastIndex = labels.size() - 1;
		const bool reserveRightBoundary = lastIndex > 0
			&& labels.last().occupiedRect.left() >= occupiedRight;
		if (reserveRightBoundary)
			labels.last().draw = true;
		const qreal rightBoundary = reserveRightBoundary
			? labels.last().occupiedRect.left()
			: static_cast<qreal>(width()) + labelGap;

		for (qsizetype index = 1; index < lastIndex; ++index)
		{
			TickLabel& candidate = labels[index];
			if (candidate.occupiedRect.left() >= occupiedRight
				&& candidate.occupiedRect.right() <= rightBoundary)
			{
				candidate.draw = true;
				occupiedRight = candidate.occupiedRect.right();
			}
		}

		for (const TickLabel& label : labels)
		{
			if (label.draw)
			{
				painter.drawText(
					qRound(label.center),
					0,
					0,
					height(),
					Qt::TextDontClip | Qt::AlignCenter,
					label.text);
			}
		}
	}

	QPoint mousePos = view->getLastMousePos();
	if (!mousePos.isNull())
	{
		QPointF mouseScenePos = view->mapToScene(mousePos);

		double hz = s->xToHz(mouseScenePos.x());
		double x = s->hzToX(hz);
		if (x != -1)
		{
			QString text = QString("%0").arg(hz, 0, 'f', 1);
			QRectF rect = metrics.boundingRect(text);
			const qreal center = clampTextCenter(x - topLeft.x() + offsetLeft + 1, text, 4.0);
			rect = QRectF(center - ceil(rect.width() / 2) - 3 + 0.5, ceil(height() / 2) - ceil(rect.height() / 2) + 1.5, rect.width() + 5, rect.height() - 1);
			QPainterPath path;
			path.addRect(rect);
			QPainterPath topTriangle;
			topTriangle.moveTo(QPoint(center - 3, rect.top() + 1));
			topTriangle.lineTo(QPoint(center + 3, rect.top() + 1));
			topTriangle.lineTo(QPoint(center, rect.top() - 3));
			path = path.united(topTriangle);

			painter.setPen(rulerPalette.color(QPalette::Mid));
			painter.setBrush(rulerPalette.color(QPalette::ToolTipBase));
			painter.drawPath(path);
			painter.setPen(rulerPalette.color(QPalette::Highlight));
			painter.drawText(center, 0, 0, height(), Qt::TextDontClip | Qt::AlignCenter, text);
		}
	}
}

void FrequencyPlotHRuler::wheelEvent(QWheelEvent* event)
{
	FrequencyPlotView* view = qobject_cast<FrequencyPlotView*>(parentWidget());
	view->zoom(event->angleDelta().y(), 0, event->position().x() - view->viewportMargins().left(), 0);
}

void FrequencyPlotHRuler::mouseMoveEvent(QMouseEvent* event)
{
	FrequencyPlotView* view = qobject_cast<FrequencyPlotView*>(parentWidget());
	view->setLastMousePos(QPoint(qRound(event->position().x()) - view->viewportMargins().left(), view->viewport()->height() - 1));
}
