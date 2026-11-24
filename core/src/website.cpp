#include "langlive/obsplugin/core/website.hpp"

namespace langlive::obsplugin::core {

namespace {

bool StartsWith(const std::string &text, const char *prefix)
{
	return text.rfind(prefix, 0) == 0;
}

} // namespace

std::string WebsitePageUrl(std::string websiteUrl, WebsitePage page, const std::string &locale)
{
	if (websiteUrl.empty())
		websiteUrl = kDefaultWebsiteUrl;
	while (!websiteUrl.empty() && websiteUrl.back() == '/')
		websiteUrl.pop_back();

	std::string language;
	if (StartsWith(locale, "ja"))
		language = "/ja-JP";
	else if (!StartsWith(locale, "zh"))
		language = "/en-US";
	const char *path = "/legal/terms";
	if (page == WebsitePage::Privacy)
		path = "/legal/privacy";
	else if (page == WebsitePage::HelpCenter)
		path = "/obsFaq?anchor=obsplugin";
	return websiteUrl + language + path;
}

} // namespace langlive::obsplugin::core
