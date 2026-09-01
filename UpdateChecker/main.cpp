/*
	This file is part of EqualizerAPO, a system-wide equalizer.
	Copyright (C) 2024  Jonas Dahlinger

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
#include <QDir>
#include <QStyleHints>
#include <QCommandLineParser>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QVersionNumber>
#include <QtWidgets/QApplication>
#include <helpers/TaskSchedulerHelper.h>
#include "UpdateChecker.h"
#include "version.h"

using namespace std::chrono_literals;

void showFailureMessage(QString message, QString title);

namespace
{
	const QUrl releaseApiUrl("https://api.github.com/repos/xup61069/loudness-correction-apo/releases/latest");

	bool isTransientNetworkError(QNetworkReply::NetworkError error)
	{
		return error == QNetworkReply::HostNotFoundError
			|| error == QNetworkReply::TimeoutError
			|| error == QNetworkReply::TemporaryNetworkFailureError
			|| error == QNetworkReply::NetworkSessionFailedError;
	}

	bool createUpdateDocument(const QJsonDocument& githubDocument, QJsonDocument& updateDocument,
		QVersionNumber& releaseVersion, QString& releaseVersionText)
	{
		if (!githubDocument.isObject())
			return false;

		QJsonObject releaseObject = githubDocument.object();
		releaseVersionText = releaseObject.value("tag_name").toString().trimmed();
		if (releaseVersionText.startsWith('v', Qt::CaseInsensitive))
			releaseVersionText.remove(0, 1);

		qsizetype suffixIndex = 0;
		releaseVersion = QVersionNumber::fromString(releaseVersionText, &suffixIndex);
		if (releaseVersion.isNull() || suffixIndex != releaseVersionText.size())
			return false;

		QUrl downloadUrl(releaseObject.value("html_url").toString(), QUrl::StrictMode);
		if (!downloadUrl.isValid() || downloadUrl.scheme() != "https"
			|| downloadUrl.host().compare("github.com", Qt::CaseInsensitive) != 0)
			return false;

		QDateTime publishedAt = QDateTime::fromString(
			releaseObject.value("published_at").toString(), Qt::ISODate);
		if (!publishedAt.isValid())
			return false;

		QJsonArray releaseNotes;
		const QString body = releaseObject.value("body").toString();
		for (QString line : body.split(QRegularExpression("[\\r\\n]+"), Qt::SkipEmptyParts))
		{
			line.remove(QRegularExpression("^\\s*(?:#{1,6}\\s+|[-*+]\\s+|\\d+[.)]\\s+)"));
			line = line.trimmed();
			if (line.isEmpty() || line == "---")
				continue;
			if (line.size() > 300)
				line = line.left(300) + QString::fromUtf8("…");
			releaseNotes.append(line);
			if (releaseNotes.size() == 12)
				break;
		}

		QJsonObject versionObject;
		versionObject.insert("version", releaseVersionText);
		versionObject.insert("date", publishedAt.date().toString(Qt::ISODate));
		versionObject.insert("info", releaseNotes);

		QJsonObject updateObject;
		updateObject.insert("download-url", downloadUrl.toString(QUrl::FullyEncoded));
		updateObject.insert("versions", QJsonArray{ versionObject });
		updateDocument = QJsonDocument(updateObject);
		return true;
	}
}

int main(int argc, char* argv[])
{
	QCoreApplication::addLibraryPath("qt");

	QApplication app(argc, argv);
	if (QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark)
		app.setStyle("fusion");

	QLocale::setDefault(QLocale::system());

	QTranslator qtTranslator;
	if (qtTranslator.load(QLocale(), ":/translations/qtbase", "_"))
		app.installTranslator(&qtTranslator);

	QTranslator updateCheckerTranslator;
	if (updateCheckerTranslator.load(
		QLocale(), ":/translations/UpdateChecker", "_"))
		app.installTranslator(&updateCheckerTranslator);

	QCommandLineParser parser;
	QCommandLineOption autoOption("a", "Automatic mode (no dialog if no new version, respect skip version, only check every 24 hours)");
	QCommandLineOption installOption("i", "Install scheduled task");
	QCommandLineOption uninstallOption("u", "Uninstall scheduled task");
	QCommandLineOption silentOption("s", "Suppress dialogs and report failures through the exit code");
	parser.addOptions(QList<QCommandLineOption>() << autoOption << installOption << uninstallOption << silentOption);
	parser.process(app);
	bool silentMode = parser.isSet(silentOption);
	if (parser.isSet(installOption))
	{
		try
		{
			QString programPath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
			QString workingDir = QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
			TaskSchedulerHelper::scheduleAtLogon(L"EqualizerAPOUpdateChecker", programPath.toStdWString(), L"-a", workingDir.toStdWString());
		}
		catch (TaskSchedulerException e)
		{
			if (!silentMode)
				QMessageBox::critical(nullptr, UpdateChecker::tr("Error installing Update Checker"), QString::fromStdWString(e.getMessage()));
			return 2;
		}
		return 0;
	}
	if (parser.isSet(uninstallOption))
	{
		try
		{
			TaskSchedulerHelper::unschedule(L"EqualizerAPOUpdateChecker");
		}
		catch (TaskSchedulerException e)
		{
			if (!silentMode)
				QMessageBox::critical(nullptr, UpdateChecker::tr("Error uninstalling Update Checker"), QString::fromStdWString(e.getMessage()));
			return 2;
		}
		return 0;
	}
	bool autoMode = parser.isSet(autoOption);

	QString version = QString("%0.%1.%2").arg(MAJOR).arg(MINOR).arg(REVISION);

	QSettings settings(QString::fromWCharArray(UPDATE_CHECKER_REGPATH), QSettings::NativeFormat);
	QString skipVersion;
	if (autoMode)
	{
		QDateTime lastCheckDate = settings.value("lastCheckDate").toDateTime();

		if (lastCheckDate.isValid() && lastCheckDate.toUTC().daysTo(QDateTime::currentDateTimeUtc()) < 1)
			return 1;
		skipVersion = settings.value("skipVersion").toString();
	}

	QNetworkAccessManager manager;
	QNetworkReply* reply = nullptr;
	int tries = autoMode ? 10 : 1;
	while (tries-- > 0)
	{
		QNetworkRequest request(releaseApiUrl);
		request.setHeader(QNetworkRequest::UserAgentHeader, QString("loudness-correction-apo-UpdateChecker/%0").arg(version));
		request.setRawHeader("Accept", "application/vnd.github+json");
		request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
		request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
		request.setTransferTimeout(10s);
		reply = manager.get(request);
		QEventLoop loop;
		QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
		loop.exec();

		if (reply->error() == QNetworkReply::NoError || !isTransientNetworkError(reply->error()) || tries == 0)
			break;

		delete reply;
		reply = nullptr;
		QThread::sleep(5);
	}

	int result = 0;
	if (reply != nullptr && reply->isFinished())
	{
		if (reply->error() != QNetworkReply::NoError)
		{
			showFailureMessage(reply->errorString(), UpdateChecker::tr("Error while checking for update"));
		}
		else
		{
			QJsonParseError error;
			QJsonDocument githubDocument = QJsonDocument::fromJson(reply->readAll(), &error);
			if (error.error != QJsonParseError::NoError)
			{
				showFailureMessage(error.errorString(), UpdateChecker::tr("Error while reading response of update check"));
			}
			else
			{
				QJsonDocument updateDocument;
				QVersionNumber availableVersion;
				QString availableVersionText;
				if (!createUpdateDocument(githubDocument, updateDocument, availableVersion, availableVersionText))
				{
					showFailureMessage(UpdateChecker::tr("The update service returned invalid release data."),
						UpdateChecker::tr("Error while reading response of update check"));
				}
				else
				{
					if (autoMode)
						settings.setValue("lastCheckDate", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

					QVersionNumber installedVersion = QVersionNumber::fromString(version);
					bool updateAvailable = QVersionNumber::compare(availableVersion, installedVersion) > 0;
					bool skipped = autoMode && skipVersion == availableVersionText;
					if (!updateAvailable || skipped)
					{
						if (!autoMode)
							QMessageBox::information(nullptr, UpdateChecker::tr("No update available"),
								UpdateChecker::tr("The installed version %0 of Equalizer APO is up to date.").arg(version));
					}
					else
					{
						UpdateChecker dialog(nullptr, updateDocument);
						dialog.show();
						result = app.exec();
					}
				}
			}
		}
	}

	delete reply;
	return result;
}

void showFailureMessage(QString message, QString title)
{
	QSettings settings(QString::fromWCharArray(UPDATE_CHECKER_REGPATH), QSettings::NativeFormat);
	bool hideFailureMessage = settings.value("hideFailureMessage").toBool();
	if (!hideFailureMessage)
	{
		QMessageBox messageBox;
		messageBox.setText(message);
		messageBox.setWindowTitle(title);
		messageBox.setIcon(QMessageBox::Icon::Critical);
		QCheckBox* hideCheckBox = new QCheckBox(UpdateChecker::tr("Don't show message for failed update check again"));
		messageBox.setCheckBox(hideCheckBox);
		messageBox.exec();

		if (hideCheckBox->isChecked())
			settings.setValue("hideFailureMessage", true);
	}
}
