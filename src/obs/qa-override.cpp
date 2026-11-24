#include "obs/qa-override.hpp"

#include "langlive/obsplugin/core/update.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

namespace langlive::obsplugin::obs {

ManifestOverride ReadManifestOverride(const std::string &path)
{
	ManifestOverride result;
	QFile file(QString::fromStdString(path));
	if (path.empty() || !file.exists())
		return result;
	if (!file.open(QIODevice::ReadOnly)) {
		result.error = "cannot read " + path;
		return result;
	}

	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(file.read(64 * 1024), &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
		result.error = path + " is not a JSON object";
		return result;
	}
	const std::string url = document.object().value(QStringLiteral("update_manifest_url")).toString().toStdString();
	if (url.empty())
		return result;
	if (!core::IsAllowedUpdateUrl(url)) {
		result.error = "update_manifest_url must use HTTPS: " + url;
		return result;
	}
	result.url = url;
	return result;
}

} // namespace langlive::obsplugin::obs
