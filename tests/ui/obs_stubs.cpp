// Stand-ins for the few libobs functions the UI calls, so the dialogs run
// without OBS. Strings come from the plugin's en-US locale file, or the one
// LANGLIVE_TEST_LOCALE names (for screenshots).

#include "obs/diagnostics.hpp"

#include <QByteArray>
#include <QFile>
#include <QHash>
#include <QtGlobal>

#include <cstdlib>
#include <cstring>

namespace {

QHash<QByteArray, QByteArray> &Strings()
{
	static QHash<QByteArray, QByteArray> strings;
	return strings;
}

} // namespace

void LoadTestLocale()
{
	const QString locale = qEnvironmentVariable("LANGLIVE_TEST_LOCALE", QStringLiteral("en-US"));
	QFile file(QStringLiteral(LANGLIVE_TEST_DATA_DIR "/locale/") + locale + QStringLiteral(".ini"));
	if (!file.open(QIODevice::ReadOnly))
		return;
	for (const QByteArray &line : file.readAll().split('\n')) {
		const qsizetype eq = line.indexOf('=');
		if (eq <= 0)
			continue;
		QByteArray value = line.mid(eq + 1).trimmed();
		if (value.startsWith('"') && value.endsWith('"'))
			value = value.mid(1, value.size() - 2);
		Strings().insert(line.left(eq), value);
	}
}

extern "C" {

const char *PLUGIN_NAME = "langlive-obs-plugin";
const char *PLUGIN_VERSION = "0.1.0";

void obs_log(int, const char *, ...) {}

const char *obs_module_text(const char *key)
{
	const auto it = Strings().constFind(QByteArray(key));
	return it == Strings().constEnd() ? key : it->constData();
}

struct obs_module;
obs_module *obs_current_module(void)
{
	return nullptr;
}

char *obs_find_module_file(obs_module *, const char *file)
{
	const QByteArray path = QByteArray(LANGLIVE_TEST_DATA_DIR "/") + file;
	return strdup(path.constData());
}

void bfree(void *ptr)
{
	free(ptr);
}

} // extern "C"

namespace langlive::obsplugin::obs {

QString DefaultDiagnosticsDir()
{
	return QStringLiteral("/tmp");
}

QString DiagnosticsFileName()
{
	return QStringLiteral("langlive_diagnostics_test.zip");
}

ExportResult ExportDiagnostics(const QString &)
{
	return {true, {}};
}

} // namespace langlive::obsplugin::obs
