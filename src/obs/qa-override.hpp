#pragma once

#include <string>

namespace langlive::obsplugin::obs {

// QA builds only (LANGLIVE_QA_BUILD): a file in the plugin's config folder,
// qa-override.json, lets QA point the update check at another manifest, such
// as the test scenarios, without a release:
//
//   { "update_manifest_url": "https://.../test/scenarios/forced.json" }
//
// Production builds never read it.
constexpr const char *kQaOverrideFileName = "qa-override.json";

struct ManifestOverride {
	std::string url;   // empty: no override (no file, or no URL in it)
	std::string error; // why the file was ignored; empty if it was not
};

// Reads the override file at `path`. A missing file is no override; a file
// that is not a JSON object, or whose URL is not HTTPS, is ignored with an
// error.
ManifestOverride ReadManifestOverride(const std::string &path);

} // namespace langlive::obsplugin::obs
