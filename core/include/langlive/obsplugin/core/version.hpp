#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace langlive::obsplugin::core {

// A SemVer version. Build metadata ("+qa") is accepted when parsing but
// ignored, as SemVer requires.
struct Version {
	int major = 0;
	int minor = 0;
	int patch = 0;
	std::string prerelease; // e.g. "rc.1", empty for a release
};

// Parses "1.2.3", "v1.2.3", "1.2.3-rc.1", "1.2.3-rc.1+qa".
std::optional<Version> ParseVersion(std::string_view text);

// SemVer precedence: negative if a < b, zero if equal, positive if a > b.
int CompareVersions(const Version &a, const Version &b);

// True if `current` is older than the server's minimum supported version,
// which means the plugin must be updated before logging in.
bool IsUpdateRequired(const Version &current, const Version &minSupported);

} // namespace langlive::obsplugin::core
