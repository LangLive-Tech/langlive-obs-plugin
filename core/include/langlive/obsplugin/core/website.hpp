#pragma once

#include <string>

namespace langlive::obsplugin::core {

// The production LangLive website, used when a build sets none.
inline constexpr const char *kDefaultWebsiteUrl = "https://www.lang.live";

// Pages of the LangLive website the plugin links to.
enum class WebsitePage {
	Terms,      // /legal/terms
	Privacy,    // /legal/privacy
	HelpCenter, // /obsFaq?anchor=obsplugin, the FAQ's OBS plugin section
};

// A website page in the language closest to `locale` (OBS's, e.g. "ja-JP").
// The website has Traditional Chinese (its default, without a language in the
// path), English and Japanese:
//   zh-*      https://www.lang.live/legal/terms
//   ja-*      https://www.lang.live/ja-JP/legal/terms
//   others    https://www.lang.live/en-US/legal/terms
// `websiteUrl` is the website's origin; empty means kDefaultWebsiteUrl.
std::string WebsitePageUrl(std::string websiteUrl, WebsitePage page, const std::string &locale);

} // namespace langlive::obsplugin::core
