#include "VerticalDragDial.h"

#include <algorithm>

#include <QMouseEvent>

VerticalDragDial::VerticalDragDial(QWidget* parent)
	: QDial(parent)
{
	setCursor(Qt::SizeVerCursor);
}

void VerticalDragDial::mousePressEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton)
	{
		QDial::mousePressEvent(event);
		return;
	}

	setFocus(Qt::MouseFocusReason);
	verticalDragging = true;
	lastGlobalY = event->globalPosition().y();
	dragValue = value();
	setSliderDown(true);
	event->accept();
}

void VerticalDragDial::mouseMoveEvent(QMouseEvent* event)
{
	if (!verticalDragging || !(event->buttons() & Qt::LeftButton))
	{
		QDial::mouseMoveEvent(event);
		return;
	}

	const qreal currentGlobalY = event->globalPosition().y();
	const qreal upwardPixels = lastGlobalY - currentGlobalY;
	lastGlobalY = currentGlobalY;

	const double range = static_cast<double>(maximum()) - minimum();
	const double pixelsForFullRange = (std::max)(160.0, height() * 3.0);
	const double precision = (event->modifiers() & Qt::ShiftModifier) ? 0.1 : 1.0;
	dragValue += upwardPixels * range / pixelsForFullRange * precision;
	dragValue = (std::max)(static_cast<double>(minimum()),
		(std::min)(static_cast<double>(maximum()), dragValue));
	setSliderPosition(qRound(dragValue));
	event->accept();
}

void VerticalDragDial::mouseReleaseEvent(QMouseEvent* event)
{
	if (verticalDragging && event->button() == Qt::LeftButton)
	{
		verticalDragging = false;
		if (!hasTracking())
			setValue(sliderPosition());
		setSliderDown(false);
		event->accept();
		return;
	}

	QDial::mouseReleaseEvent(event);
}
