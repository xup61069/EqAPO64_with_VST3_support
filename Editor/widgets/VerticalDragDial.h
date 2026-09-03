#pragma once

#include <QDial>

class QMouseEvent;

// A dial with DAW-style relative vertical dragging. Moving upward increases
// the value; moving downward decreases it. Keyboard and wheel behavior remain
// the standard QDial behavior.
class VerticalDragDial : public QDial
{
public:
	explicit VerticalDragDial(QWidget* parent = nullptr);

protected:
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;

private:
	bool verticalDragging = false;
	qreal lastGlobalY = 0.0;
	double dragValue = 0.0;
};
