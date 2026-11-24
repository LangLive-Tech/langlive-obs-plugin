#pragma once

#include "langlive/obsplugin/core/version.hpp"

#include <string_view>

namespace langlive::obsplugin::core {

enum class UpdateStatus {
	UpToDate,  // current >= latest
	Available, // a newer version exists; updating is optional
	Required,  // current < min_supported_version; must update before logging in
};

// Compares the running version with the update manifest's latest and minimum
// supported versions.
UpdateStatus EvaluateUpdate(const Version &current, const Version &latest, const Version &minSupported);

// Manifest and installer downloads must use HTTPS. Plain HTTP is accepted
// only for localhost (127.0.0.1, ::1, localhost) so a local test server works.
bool IsAllowedUpdateUrl(std::string_view url);

} // namespace langlive::obsplugin::core
