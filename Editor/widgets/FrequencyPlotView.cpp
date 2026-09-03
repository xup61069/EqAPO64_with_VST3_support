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
#include <QApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QWheelEvent>
#include <QScrollBar>

#include "Editor/helpers/GUIHelper.h"
#include "FrequencyPlotItem.h"
#include "FrequencyPlotView.h"

using namespace std;

FrequencyPlotView::FrequencyPlotView(QWidget* parent)
	: QGraphicsView(parent)
{
	hRuler = new FrequencyPlotHRuler(this);
	vRuler = new FrequencyPlotVRuler(this);
	updateRulerMargins();
	hRuler->setMouseTracking(true);
	vRuler->setMouseTracking(true);
}

void FrequencyPlotView::updateRulerMargins()
{
	const QFontMetrics metrics(font());
	const int verticalRulerWidth = qMax(
		GUIHelper::scale(32),
		metrics.boundingRect(QStringLiteral("-100.0")).width() + GUIHelper::scale(10));
	const int horizontalRulerHeight = qMax(
		GUIHelper::scale(20),
		metrics.height() + GUIHelper::scale(8));
	setViewportMargins(verticalRulerWidth, 0, 0, horizontalRulerHeight);
	updateRulerGeometry();
}

void FrequencyPlotView::updateRulerGeometry()
{
	const QRect rect = viewport()->geometry();
	const QMargins margins = viewportMargins();
	hRuler->setGeometry(rect.x() - margins.left(), rect.y() + rect.height(), rect.width() + margins.left(), margins.bottom());
	vRuler->setGeometry(rect.x() - margins.left(), rect.y(), margins.left(), rect.height() + margins.bottom());
}

FrequencyPlotScene* FrequencyPlotView::scene() const
{
	return qobject_cast<FrequencyPlotScene*>(QGraphicsView::scene());
}

void FrequencyPlotView::setScene(FrequencyPlotScene* scene)
{
	QGraphicsView::setScene(scene);
}

void FrequencyPlotView::drawBackground(QPainter* painter, const QRectF& drawRect)
{
	painter->setRenderHint(QPainter::Antialiasing, false);

	QRectF rect = drawRect;
	if (!sceneRect().contains(rect))
	{
		rect = rect.intersected(sceneRect());
		painter->setClipRect(rect);
	}

	FrequencyPlotScene* s = scene();
	QPointF topLeft = mapToScene(0, 0);
	QPointF bottomRight = mapToScene(viewport()->width(), viewport()->height());
	double dbStep = abs(s->yToDb(0) - s->yToDb(GUIHelper::scale(30)));

	double dbBase = pow(10, floor(log10(dbStep)));
	if (dbStep >= 5 * dbBase)
		dbStep = 5 * dbBase;
	else if (dbStep >= 2 * dbBase)
		dbStep = 2 * dbBase;
	else
		dbStep = dbBase;

	double fromDb = floor(s->yToDb(rect.top() + rect.height()) / dbStep) * dbStep;
	double toDb = ceil(s->yToDb(rect.top()) / dbStep) * dbStep;

	QColor gridColor = palette().color(QPalette::Mid);
	const bool highContrast = qApp
		&& qApp->property("eqapoModernThemeHighContrast").toBool();
	if (!highContrast)
		gridColor.setAlpha(
			palette().color(QPalette::Window).lightnessF() < 0.5 ? 150 : 180);
	painter->setPen(gridColor);
	for (double db = fromDb; db <= toDb; db += dbStep)
	{
		double y = floor(s->dbToY(db)) + 0.5;
		if (y != -1)
			painter->drawLine(topLeft.x(), y, bottomRight.x(), y);
	}

	double fromHz = s->xToHz(rect.left());
	double toHz = s->xToHz(rect.left() + rect.width());

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
			double x = floor(s->hzToX(hz)) + 0.5;
			if (x >= 0)
				painter->drawLine(x, topLeft.y(), x, bottomRight.y());

			hz += hzBase;
			if (round(hz / hzBase) >= 10)
				hzBase *= 10;
		}
	}
	else
	{
		vector<double>::const_iterator it = lower_bound(bands.cbegin(), bands.cend(), fromHz);
		for (; it != bands.cend() && *it < toHz; it++)
		{
			double hz = *it;
			double x = floor(s->hzToX(hz)) + 0.5;
			if (x >= 0)
				painter->drawLine(x, topLeft.y(), x, bottomRight.y());
		}
	}
}

void FrequencyPlotView::updateHRuler()
{
	hRuler->update();
}

QPoint FrequencyPlotView::getLastMousePos() const
{
	return lastMousePos;
}

void FrequencyPlotView::setLastMousePos(QPoint pos)
{
	lastMousePos = pos;
	hRuler->update();
	vRuler->update();
}

void FrequencyPlotView::zoom(int deltaX, int deltaY, int mouseX, int mouseY)
{
	FrequencyPlotScene* s = scene();
	QPointF scenePos = mapToScene(mouseX, mouseY);
	double hz = s->xToHz(scenePos.x());
	double db = s->yToDb(scenePos.y());

	qreal zoomFactorX = pow(1.001, deltaX);
	qreal zoomFactorY = pow(1.001, deltaY);
	double zoomX = max(0.5, min(30.0, s->getZoomX() * zoomFactorX));
	double zoomY = max(0.5, min(30.0, s->getZoomY() * zoomFactorY));
	s->setZoom(zoomX, zoomY);

	if (deltaX != 0)
	{
		double x = s->hzToX(hz);
		if (x != -1)
			horizontalScrollBar()->setValue(horizontalScrollBar()->value() + round(x - scenePos.x()));
	}

	if (deltaY != 0)
	{
		double y = s->dbToY(db);
		if (y != -1)
			verticalScrollBar()->setValue(verticalScrollBar()->value() + round(y - scenePos.y()));
	}

	resetCachedContent();
	viewport()->update();
	if (deltaX != 0)
		hRuler->update();
	if (deltaY != 0)
		vRuler->update();
}

void FrequencyPlotView::setScrollOffsets(int x, int y)
{
	presetScrollX = x;
	presetScrollY = y;
}

void FrequencyPlotView::resetView()
{
	FrequencyPlotScene* s = scene();
	if (s == NULL)
		return;

	const double defaultZoom = GUIHelper::scaleZoom(1.0);
	s->setZoom(defaultZoom, defaultZoom);
	presetScrollX = -1;
	presetScrollY = -1;
	horizontalScrollBar()->setValue(qRound(s->hzToX(20.0)));
	verticalScrollBar()->setValue(qRound(s->dbToY(22.0)));

	resetCachedContent();
	viewport()->update();
	hRuler->update();
	vRuler->update();
}

void FrequencyPlotView::changeEvent(QEvent* event)
{
	QGraphicsView::changeEvent(event);
	if (event->type() == QEvent::FontChange || event->type() == QEvent::ApplicationFontChange)
	{
		updateRulerMargins();
		hRuler->update();
		vRuler->update();
	}
}

void FrequencyPlotView::wheelEvent(QWheelEvent* event)
{
	event->accept();
	if (event->modifiers() & Qt::ShiftModifier)
	{
		const QPoint angle = event->angleDelta();
		const int delta = angle.y() != 0 ? angle.y() : angle.x();
		horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta);
		hRuler->update();
		return;
	}
	int delta = event->angleDelta().y();
	zoom(delta, delta, event->position().x(), event->position().y());
}

void FrequencyPlotView::scrollContentsBy(int dx, int dy)
{
	QGraphicsView::scrollContentsBy(dx, dy);

	if (dx != 0)
		hRuler->update();
	if (dy != 0)
		vRuler->update();
}

void FrequencyPlotView::resizeEvent(QResizeEvent* event)
{
	QGraphicsView::resizeEvent(event);
	updateRulerGeometry();
}

void FrequencyPlotView::mousePressEvent(QMouseEvent* event)
{
	QGraphicsView::mousePressEvent(event);

	setLastMousePos(event->position().toPoint());
}

void FrequencyPlotView::mouseMoveEvent(QMouseEvent* event)
{
	if (event->buttons() & Qt::RightButton)
	{
		QPoint position = event->position().toPoint();
		horizontalScrollBar()->setValue(horizontalScrollBar()->value() - (position.x() - lastMousePos.x()));
		verticalScrollBar()->setValue(verticalScrollBar()->value() - (position.y() - lastMousePos.y()));
	}
	else
	{
		QGraphicsView::mouseMoveEvent(event);
	}
	setLastMousePos(event->position().toPoint());
}

void FrequencyPlotView::leaveEvent(QEvent*)
{
	setLastMousePos(QPoint());
}

void FrequencyPlotView::showEvent(QShowEvent* event)
{
	QGraphicsView::showEvent(event);

	if (presetScrollX != -1)
	{
		horizontalScrollBar()->setValue(presetScrollX);
		presetScrollX = -1;
	}

	if (presetScrollY != -1)
	{
		verticalScrollBar()->setValue(presetScrollY);
		presetScrollY = -1;
	}
}
