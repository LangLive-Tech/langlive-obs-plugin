#pragma once

#include "app/ports.hpp"

#include <atomic>
#include <memory>
#include <string>

namespace langlive::obsplugin::obs {

class CurlHttp;

// Reads the update manifest and downloads installers over HTTPS (libcurl,
// see CurlHttp). Must be used on the main thread.
//
// Manifest format (latest.json):
//   {
//     "version": "1.2.0",
//     "min_supported_version": "1.0.0",
//     "platforms": {
//       "macos":       { "url": "https://.../x.pkg", "sha256": "<64 hex>" },
//       "windows-x64": { "url": "https://.../x.exe", "sha256": "<64 hex>" }
//     }
//   }
class QtUpdateFeed final : public UpdateFeed {
public:
	// `overridePath`: the QA override file (qa-override.json) in QA builds,
	// empty in production builds.
	QtUpdateFeed(std::string manifestUrl, std::string userAgent, std::string overridePath = {});
	~QtUpdateFeed() override;

	void FetchManifest(std::function<void(std::optional<UpdateManifest>)> done) override;
	void DownloadInstaller(const std::string &url, const std::string &sha256,
			       std::function<void(int percent)> progress,
			       std::function<void(InstallerDownload, std::string path)> done) override;
	bool LaunchInstaller(const std::string &path) override;
	void CancelDownload() override;

private:
	// QA builds: the manifest URL in qa-override.json; empty if none.
	std::string ManifestOverride() const;

	std::string manifestUrl_;
	std::string userAgent_;
	std::string overridePath_;
	std::shared_ptr<std::atomic<bool>> downloadCancel_; // the download in progress
	// Last: destroyed first, so no callback runs into a destroyed member.
	std::unique_ptr<CurlHttp> http_;
};

} // namespace langlive::obsplugin::obs
