/*
	This file is part of Equalizer APO, a system-wide equalizer.
	Copyright (C) 2026  Equalizer APO contributors

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
*/

#pragma once

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QPixmap>
#include <QTimer>
#include <QWidget>

namespace UiSnapshot
{
#ifdef EQAPO_ENABLE_UI_SNAPSHOTS
	inline QString outputPath()
	{
		return qEnvironmentVariable("EQAPO_UI_SNAPSHOT").trimmed();
	}

	inline bool requested()
	{
		return !outputPath().isEmpty();
	}

	inline void schedule(QWidget& widget, QApplication& application)
	{
		const QString path = outputPath();
		if (path.isEmpty())
			return;

		bool validDelay = false;
		const int requestedDelay = qEnvironmentVariableIntValue(
			"EQAPO_UI_SNAPSHOT_DELAY_MS", &validDelay);
		const int delay = validDelay ? qBound(0, requestedDelay, 10000) : 650;

		QTimer::singleShot(delay, &widget, [&widget, &application, path]
		{
			const QFileInfo target(path);
			const bool directoryReady = QDir().mkpath(target.absolutePath());
			const bool saved = directoryReady && widget.grab().save(path, "PNG");
			application.exit(saved ? 0 : 86);
		});
	}
#else
	inline QString outputPath()
	{
		return QString();
	}

	inline bool requested()
	{
		return false;
	}

	inline void schedule(QWidget& widget, QApplication& application)
	{
		Q_UNUSED(widget);
		Q_UNUSED(application);
	}
#endif
}
