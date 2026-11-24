#include "obs/update-feed.hpp"

#include "langlive/obsplugin/core/update.hpp"
#include "obs/curl-http.hpp"
#include "obs/qa-override.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

#include <memory>

namespace langlive::obsplugin::obs {

namespace {

constexpr long kStallTimeoutSeconds = 30; // abort after 30 s without data
constexpr size_t kMaxManifestBytes = 1024 * 1024;

#if defined(_WIN32)
constexpr const char *kPlatformKey = "windows-x64";
constexpr const char *kInstallerSuffix = ".exe";
#else
constexpr const char *kPlatformKey = "macos";
constexpr const char *kInstallerSuffix = ".pkg";
#endif

HttpRequest MakeRequest(std::string url, const std::string &userAgent)
{
	HttpRequest request;
	request.url = std::move(url);
	request.headers = {"User-Agent: " + userAgent, "Cache-Control: no-cache"};
	request.stallTimeoutSeconds = kStallTimeoutSeconds;
	return request;
}

bool Succeeded(const HttpResponse &response)
{
	return response.ok() && response.status >= 200 && response.status < 300;
}

QString Describe(const HttpResponse &response)
{
	return response.ok() ? QStringLiteral("HTTP %1").arg(response.status) : QString::fromStdString(response.error);
}

std::optional<UpdateManifest> ParseManifest(const QByteArray &json)
{
	QJsonParseError error;
	const QJsonDocument document = QJsonDocument::fromJson(json, &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		obs_log(LOG_WARNING, "update manifest is not valid JSON: %s", qPrintable(error.errorString()));
		return std::nullopt;
	}

	const QJsonObject root = document.object();
	const QJsonObject platform = root.value("platforms").toObject().value(kPlatformKey).toObject();
	if (platform.isEmpty()) {
		obs_log(LOG_WARNING, "update manifest has no '%s' entry", kPlatformKey);
		return std::nullopt;
	}

	UpdateManifest manifest;
	manifest.version = root.value("version").toString().toStdString();
	manifest.minSupportedVersion = root.value("min_supported_version").toString().toStdString();
	manifest.installerUrl = platform.value("url").toString().toStdString();
	manifest.installerSha256 = platform.value("sha256").toString().toLower().toStdString();
	return manifest;
}

// Where an installer is downloaded: a folder of its own under a plugin
// folder in the temp directory, named after the URL's file name. Its own
// folder, because a canceled download can still be writing (and then
// removes its file) when the next one starts.
QString DownloadPath(const QUrl &url)
{
	static int downloads = 0;
	const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
			    QStringLiteral("/langlive-obs-plugin-update/%1-%2")
				    .arg(QCoreApplication::applicationPid())
				    .arg(++downloads);
	QDir().mkpath(dir);
	return dir + QLatin1Char('/') + url.fileName();
}

// A failed or canceled download: its file and its folder.
void RemoveDownload(const QString &path)
{
	QFile::remove(path);
	QDir().rmdir(QFileInfo(path).absolutePath());
}

} // namespace

QtUpdateFeed::QtUpdateFeed(std::string manifestUrl, std::string userAgent, std::string overridePath)
	: manifestUrl_(std::move(manifestUrl)),
	  userAgent_(std::move(userAgent)),
	  overridePath_(std::move(overridePath)),
	  http_(std::make_unique<CurlHttp>())
{
}

QtUpdateFeed::~QtUpdateFeed() = default;

std::string QtUpdateFeed::ManifestOverride() const
{
	if (overridePath_.empty())
		return {};
	const obs::ManifestOverride found = ReadManifestOverride(overridePath_);
	if (!found.error.empty())
		obs_log(LOG_WARNING, "QA override ignored: %s", found.error.c_str());
	return found.url;
}

void QtUpdateFeed::FetchManifest(std::function<void(std::optional<UpdateManifest>)> done)
{
	// Read on every check, so QA can switch manifests without restarting OBS.
	const std::string override = ManifestOverride();
	if (!override.empty())
		obs_log(LOG_INFO, "QA override: update manifest %s", override.c_str());
	const std::string &manifestUrl = override.empty() ? manifestUrl_ : override;

	if (manifestUrl.empty()) {
		obs_log(LOG_INFO, "update check skipped: no manifest URL in this build");
		done(std::nullopt);
		return;
	}
	if (!core::IsAllowedUpdateUrl(manifestUrl)) {
		obs_log(LOG_WARNING, "update manifest URL must use HTTPS");
		done(std::nullopt);
		return;
	}

	HttpRequest request = MakeRequest(manifestUrl, userAgent_);
	request.maxResponseBytes = kMaxManifestBytes;
	http_->Send(std::move(request), [done = std::move(done)](HttpResponse response) {
		if (!Succeeded(response)) {
			obs_log(LOG_WARNING, "update manifest download failed: %s", qPrintable(Describe(response)));
			done(std::nullopt);
			return;
		}
		done(ParseManifest(QByteArray::fromStdString(response.body)));
	});
}

void QtUpdateFeed::DownloadInstaller(const std::string &url, const std::string &sha256,
				     std::function<void(int percent)> progress,
				     std::function<void(InstallerDownload, std::string path)> done)
{
	const QUrl qurl(QString::fromStdString(url));
	// fileName() is decoded: "%5C" would be a Windows path separator.
	const QString name = qurl.fileName();
	if (!core::IsAllowedUpdateUrl(url) || !name.endsWith(QLatin1String(kInstallerSuffix)) ||
	    name.contains(QLatin1Char('\\')) || name.contains(QLatin1Char('/'))) {
		obs_log(LOG_WARNING, "refusing installer URL: %s", url.c_str());
		done(InstallerDownload::Failed, {});
		return;
	}

	CancelDownload(); // one download at a time
	downloadCancel_ = std::make_shared<std::atomic<bool>>(false);

	const QString path = DownloadPath(qurl);
	HttpRequest request = MakeRequest(url, userAgent_);
	request.outputPath = path.toStdString();
	request.cancel = downloadCancel_;
	http_->Send(
		std::move(request),
		[path, expected = QString::fromStdString(sha256), done = std::move(done)](HttpResponse response) {
			if (!Succeeded(response)) {
				obs_log(LOG_WARNING, "installer download failed: %s", qPrintable(Describe(response)));
				RemoveDownload(path);
				done(InstallerDownload::Failed, {});
				return;
			}
			QFile file(path);
			QCryptographicHash hash(QCryptographicHash::Sha256);
			if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file)) {
				obs_log(LOG_WARNING, "cannot read %s", qPrintable(path));
				RemoveDownload(path);
				done(InstallerDownload::Failed, {});
				return;
			}
			file.close();
			const QString actual = QString::fromLatin1(hash.result().toHex());
			if (actual != expected) {
				obs_log(LOG_WARNING, "installer SHA-256 mismatch: expected %s, got %s",
					qPrintable(expected), qPrintable(actual));
				RemoveDownload(path);
				done(InstallerDownload::ChecksumMismatch, {});
				return;
			}
			obs_log(LOG_INFO, "installer downloaded and verified: %s", qPrintable(path));
			done(InstallerDownload::Ok, path.toStdString());
		},
		std::move(progress));
}

void QtUpdateFeed::CancelDownload()
{
	if (downloadCancel_)
		downloadCancel_->store(true);
	downloadCancel_.reset();
}

bool QtUpdateFeed::LaunchInstaller(const std::string &path)
{
	const QString file = QString::fromStdString(path);
	if (!file.endsWith(QLatin1String(kInstallerSuffix)) || !QFile::exists(file))
		return false;

	// The installer replaces the loaded plugin and the macOS package refuses
	// to run while OBS is open, so a detached helper waits for this OBS
	// process to exit, then opens the installer.
	const QString pid = QString::number(QCoreApplication::applicationPid());
#if defined(_WIN32)
	QString quoted = file;
	quoted.replace(QLatin1Char('\''), QStringLiteral("''"));
	const QString command = QStringLiteral("Wait-Process -Id %1 -ErrorAction SilentlyContinue; "
					       "Start-Process -FilePath '%2'")
					.arg(pid, quoted);
	const bool started = QProcess::startDetached(QStringLiteral("powershell.exe"),
						     {QStringLiteral("-NoProfile"), QStringLiteral("-WindowStyle"),
						      QStringLiteral("Hidden"), QStringLiteral("-Command"), command});
#else
	const bool started = QProcess::startDetached(
		QStringLiteral("/bin/sh"),
		{QStringLiteral("-c"),
		 QStringLiteral("while kill -0 \"$1\" 2>/dev/null; do sleep 0.5; done; exec /usr/bin/open \"$2\""),
		 QStringLiteral("langlive-update"), pid, file});
#endif
	obs_log(started ? LOG_INFO : LOG_WARNING, "launch installer %s: %s", qPrintable(file),
		started ? "started" : "failed");
	return started;
}

} // namespace langlive::obsplugin::obs
