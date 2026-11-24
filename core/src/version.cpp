#include "langlive/obsplugin/core/version.hpp"

#include <cctype>
#include <vector>

namespace langlive::obsplugin::core {

namespace {

bool IsNumeric(std::string_view text)
{
	if (text.empty())
		return false;
	for (char c : text) {
		if (!std::isdigit(static_cast<unsigned char>(c)))
			return false;
	}
	return true;
}

// Parses a non-negative integer without a leading zero (SemVer rule).
std::optional<int> ParseNumber(std::string_view text)
{
	if (!IsNumeric(text) || (text.size() > 1 && text[0] == '0') || text.size() > 9)
		return std::nullopt;
	int value = 0;
	for (char c : text)
		value = value * 10 + (c - '0');
	return value;
}

std::vector<std::string_view> Split(std::string_view text, char separator)
{
	std::vector<std::string_view> parts;
	size_t start = 0;
	while (true) {
		const size_t end = text.find(separator, start);
		parts.push_back(
			text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
		if (end == std::string_view::npos)
			break;
		start = end + 1;
	}
	return parts;
}

int ComparePrerelease(std::string_view a, std::string_view b)
{
	// A release has higher precedence than any of its pre-releases.
	if (a.empty() || b.empty())
		return a.empty() == b.empty() ? 0 : (a.empty() ? 1 : -1);

	const std::vector<std::string_view> left = Split(a, '.');
	const std::vector<std::string_view> right = Split(b, '.');

	for (size_t i = 0; i < left.size() && i < right.size(); ++i) {
		const bool leftNumeric = IsNumeric(left[i]);
		const bool rightNumeric = IsNumeric(right[i]);

		if (leftNumeric && rightNumeric) {
			// Compare by length first to avoid overflow on long identifiers.
			if (left[i].size() != right[i].size())
				return left[i].size() < right[i].size() ? -1 : 1;
			if (left[i] != right[i])
				return left[i] < right[i] ? -1 : 1;
		} else if (leftNumeric != rightNumeric) {
			// Numeric identifiers have lower precedence than alphanumeric ones.
			return leftNumeric ? -1 : 1;
		} else if (left[i] != right[i]) {
			return left[i] < right[i] ? -1 : 1;
		}
	}

	if (left.size() == right.size())
		return 0;
	return left.size() < right.size() ? -1 : 1;
}

} // namespace

std::optional<Version> ParseVersion(std::string_view text)
{
	if (!text.empty() && (text[0] == 'v' || text[0] == 'V'))
		text.remove_prefix(1);

	if (const size_t plus = text.find('+'); plus != std::string_view::npos)
		text = text.substr(0, plus);

	std::string_view prerelease;
	if (const size_t dash = text.find('-'); dash != std::string_view::npos) {
		prerelease = text.substr(dash + 1);
		text = text.substr(0, dash);
		if (prerelease.empty())
			return std::nullopt;
		for (std::string_view identifier : Split(prerelease, '.')) {
			if (identifier.empty())
				return std::nullopt;
		}
	}

	const std::vector<std::string_view> parts = Split(text, '.');
	if (parts.size() != 3)
		return std::nullopt;

	const std::optional<int> major = ParseNumber(parts[0]);
	const std::optional<int> minor = ParseNumber(parts[1]);
	const std::optional<int> patch = ParseNumber(parts[2]);
	if (!major || !minor || !patch)
		return std::nullopt;

	return Version{*major, *minor, *patch, std::string(prerelease)};
}

int CompareVersions(const Version &a, const Version &b)
{
	if (a.major != b.major)
		return a.major < b.major ? -1 : 1;
	if (a.minor != b.minor)
		return a.minor < b.minor ? -1 : 1;
	if (a.patch != b.patch)
		return a.patch < b.patch ? -1 : 1;
	return ComparePrerelease(a.prerelease, b.prerelease);
}

bool IsUpdateRequired(const Version &current, const Version &minSupported)
{
	return CompareVersions(current, minSupported) < 0;
}

} // namespace langlive::obsplugin::core
