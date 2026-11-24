#include "langlive/obsplugin/core/website.hpp"

#include <doctest/doctest.h>

using namespace langlive::obsplugin::core;

TEST_CASE("legal pages: the website's language for OBS's")
{
	const std::string site = "https://www.example.com";
	CHECK(WebsitePageUrl(site, WebsitePage::Terms, "zh-TW") == "https://www.example.com/legal/terms");
	CHECK(WebsitePageUrl(site, WebsitePage::Privacy, "zh-CN") == "https://www.example.com/legal/privacy");
	CHECK(WebsitePageUrl(site, WebsitePage::Terms, "ja-JP") == "https://www.example.com/ja-JP/legal/terms");
	CHECK(WebsitePageUrl(site, WebsitePage::Privacy, "en-US") == "https://www.example.com/en-US/legal/privacy");
	CHECK(WebsitePageUrl(site, WebsitePage::Terms, "ko-KR") == "https://www.example.com/en-US/legal/terms");
	CHECK(WebsitePageUrl(site, WebsitePage::Terms, "") == "https://www.example.com/en-US/legal/terms");
}

TEST_CASE("legal pages: the production website unless the build sets one")
{
	CHECK(WebsitePageUrl("", WebsitePage::Terms, "zh-TW") == "https://www.lang.live/legal/terms");
	CHECK(WebsitePageUrl("https://www.example.com/", WebsitePage::Terms, "zh-TW") ==
	      "https://www.example.com/legal/terms");
}

TEST_CASE("help center: the FAQ's OBS settings section, in the website's language")
{
	const std::string site = "https://www.example.com";
	CHECK(WebsitePageUrl(site, WebsitePage::HelpCenter, "zh-TW") ==
	      "https://www.example.com/obsFaq?anchor=obsplugin");
	CHECK(WebsitePageUrl(site, WebsitePage::HelpCenter, "ja-JP") ==
	      "https://www.example.com/ja-JP/obsFaq?anchor=obsplugin");
	CHECK(WebsitePageUrl(site, WebsitePage::HelpCenter, "en-US") ==
	      "https://www.example.com/en-US/obsFaq?anchor=obsplugin");
	CHECK(WebsitePageUrl("", WebsitePage::HelpCenter, "zh-TW") == "https://www.lang.live/obsFaq?anchor=obsplugin");
}
