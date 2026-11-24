#pragma once

#include <QString>

namespace langlive::obsplugin::obs {

struct ExportResult {
	bool success = false;
	QString error;
};

// The desktop: where the spec puts the file by default.
QString DefaultDiagnosticsDir();

// "langlive_diagnostics_yyyymmdd.zip" for today.
QString DiagnosticsFileName();

// Bundles the recent OBS logs, the plugin's own log lines, recent crash
// reports, and the current profile settings into a zip file. Secrets are
// redacted from every file. Nothing is uploaded.
ExportResult ExportDiagnostics(const QString &zipPath);

} // namespace langlive::obsplugin::obs
