#include "obs/diagnostics.hpp"

#include "langlive/obsplugin/core/redaction.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/platform.h>

#include <plugin-support.h>

#include <QDate>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTextStream>

#include <string>

namespace langlive::obsplugin::obs {

namespace {

// The current session's log plus the previous ones, so a crash in the
// last session is covered.
constexpr int kLogCount = 3;
constexpr int kCrashReportCount = 3;
constexpr int kZipTimeoutMs = 60000;

QString ObsConfigDir(const char *subdir)
{
	char path[512];
	if (os_get_config_path(path, sizeof(path), subdir) < 0)
		return {};
	return QString::fromUtf8(path);
}

QFileInfoList NewestFiles(const QString &dir, const QStringList &patterns, int count)
{
	if (dir.isEmpty())
		return {};
	QFileInfoList files = QDir(dir).entryInfoList(patterns, QDir::Files, QDir::Time);
	return files.mid(0, count);
}

void CopyFiles(const QFileInfoList &files, const QString &targetDir)
{
	if (files.isEmpty())
		return;
	QDir().mkpath(targetDir);
	for (const QFileInfo &file : files)
		QFile::copy(file.absoluteFilePath(), QDir(targetDir).filePath(file.fileName()));
}

// Lines this plugin wrote to the current log, for quick reading.
void ExtractPluginLog(const QFileInfo &log, const QString &targetFile)
{
	QFile source(log.absoluteFilePath());
	if (!source.open(QIODevice::ReadOnly | QIODevice::Text))
		return;

	const QString tag = QStringLiteral("[%1]").arg(QString::fromUtf8(PLUGIN_NAME));
	QStringList lines;
	QTextStream in(&source);
	while (!in.atEnd()) {
		const QString line = in.readLine();
		if (line.contains(tag))
			lines << line;
	}

	QFile out(targetFile);
	if (out.open(QIODevice::WriteOnly | QIODevice::Text))
		out.write(lines.join(QLatin1Char('\n')).toUtf8());
}

void CopyProfileSettings(const QString &targetDir)
{
	char *profilePath = obs_frontend_get_current_profile_path();
	if (!profilePath)
		return;

	QDir().mkpath(targetDir);
	const QDir profile(QString::fromUtf8(profilePath));
	bfree(profilePath);

	// service.json holds the stream key; it is redacted below.
	for (const char *name : {"basic.ini", "streamEncoder.json", "recordEncoder.json", "service.json"}) {
		const QString source = profile.filePath(QString::fromUtf8(name));
		if (QFile::exists(source))
			QFile::copy(source, QDir(targetDir).filePath(QString::fromUtf8(name)));
	}
}

void RedactFile(const QString &path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return;
	const std::string content = file.readAll().toStdString();
	file.close();

	const std::string redacted = core::RedactSensitiveFields(content);
	if (redacted == content)
		return;

	if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
		file.write(redacted.data(), static_cast<qint64>(redacted.size()));
}

void RedactDirectory(const QString &dir)
{
	QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
	while (it.hasNext())
		RedactFile(it.next());
}

// Uses the OS's zip tool: Qt has no public zip API.
bool ZipDirectory(const QString &sourceDir, const QString &zipPath)
{
	QProcess process;
#if defined(Q_OS_WIN)
	auto quote = [](QString path) {
		return QStringLiteral("'%1'").arg(path.replace(QLatin1Char('\''), QStringLiteral("''")));
	};
	process.start(QStringLiteral("powershell.exe"),
		      {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"), QStringLiteral("-Command"),
		       QStringLiteral("Compress-Archive -Path %1 -DestinationPath %2 -Force")
			       .arg(quote(QDir(sourceDir).filePath(QStringLiteral("*"))), quote(zipPath))});
#else
	process.setWorkingDirectory(sourceDir);
	process.start(QStringLiteral("/usr/bin/zip"),
		      {QStringLiteral("-r"), QStringLiteral("-X"), QStringLiteral("-q"), zipPath, QStringLiteral(".")});
#endif
	if (!process.waitForFinished(kZipTimeoutMs))
		return false;
	return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

} // namespace

QString DefaultDiagnosticsDir()
{
	return QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
}

QString DiagnosticsFileName()
{
	const QString date = QDate::currentDate().toString(QStringLiteral("yyyyMMdd"));
	return QStringLiteral("langlive_diagnostics_%1.zip").arg(date);
}

ExportResult ExportDiagnostics(const QString &zipPath)
{
	const QString workDir = zipPath + QStringLiteral(".tmp");
	QDir(workDir).removeRecursively();
	if (!QDir().mkpath(workDir))
		return {false, QStringLiteral("Cannot create %1").arg(workDir)};

	const QFileInfoList logs = NewestFiles(ObsConfigDir("obs-studio/logs"), {QStringLiteral("*.txt")}, kLogCount);
	CopyFiles(logs, QDir(workDir).filePath(QStringLiteral("obs-logs")));
	if (!logs.isEmpty())
		ExtractPluginLog(logs.first(), QDir(workDir).filePath(QStringLiteral("plugin-log.txt")));

	CopyFiles(NewestFiles(ObsConfigDir("obs-studio/crashes"), {QStringLiteral("*.txt")}, kCrashReportCount),
		  QDir(workDir).filePath(QStringLiteral("crash-reports")));

	CopyProfileSettings(QDir(workDir).filePath(QStringLiteral("settings")));

	RedactDirectory(workDir);

	QFile::remove(zipPath);
	const bool zipped = ZipDirectory(workDir, zipPath);
	QDir(workDir).removeRecursively();

	if (!zipped)
		return {false, QStringLiteral("Failed to create %1").arg(zipPath)};

	obs_log(LOG_INFO, "exported diagnostics to %s", QFileInfo(zipPath).fileName().toUtf8().constData());
	return {true, {}};
}

} // namespace langlive::obsplugin::obs
