#include "langlive/obsplugin/core/update.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace langlive::obsplugin::core {

namespace {

std::string Lowercase(std::string_view text)
{
	std::string result(text);
	std::transform(result.begin(), result.end(), result.begin(),
		       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return result;
}

// Host of "scheme://host[:port]/path", including brackets for IPv6.
std::string_view Host(std::string_view afterScheme)
{
	if (!afterScheme.empty() && afterScheme.front() == '[') {
		const size_t close = afterScheme.find(']');
		return close == std::string_view::npos ? std::string_view() : afterScheme.substr(0, close + 1);
	}
	return afterScheme.substr(0, afterScheme.find_first_of(":/?#"));
}

} // namespace

UpdateStatus EvaluateUpdate(const Version &current, const Version &latest, const Version &minSupported)
{
	if (IsUpdateRequired(current, minSupported))
		return UpdateStatus::Required;
	if (CompareVersions(current, latest) < 0)
		return UpdateStatus::Available;
	return UpdateStatus::UpToDate;
}

bool IsAllowedUpdateUrl(std::string_view url)
{
	const std::string lower = Lowercase(url);
	const std::string_view view(lower);

	constexpr std::string_view kHttps = "https://";
	constexpr std::string_view kHttp = "http://";
	if (view.substr(0, kHttps.size()) == kHttps)
		return !Host(view.substr(kHttps.size())).empty();
	if (view.substr(0, kHttp.size()) == kHttp) {
		const std::string_view host = Host(view.substr(kHttp.size()));
		return host == "127.0.0.1" || host == "localhost" || host == "[::1]";
	}
	return false;
}

} // namespace langlive::obsplugin::core
