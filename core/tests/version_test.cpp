#include "langlive/obsplugin/core/version.hpp"

#include <doctest/doctest.h>

using namespace langlive::obsplugin::core;

namespace {

Version V(const char *text)
{
	const std::optional<Version> version = ParseVersion(text);
	REQUIRE_MESSAGE(version.has_value(), text);
	return *version;
}

} // namespace

TEST_CASE("parse")
{
	const Version version = V("v1.2.3-rc.4+qa");
	CHECK(version.major == 1);
	CHECK(version.minor == 2);
	CHECK(version.patch == 3);
	CHECK(version.prerelease == "rc.4");

	CHECK(V("0.1.0").prerelease.empty());
	CHECK(V("1.3.0-dev.42.abc1234+qa").prerelease == "dev.42.abc1234");
}

TEST_CASE("reject invalid versions")
{
	for (const char *text : {"", "1", "1.2", "1.2.3.4", "1.2.x", "01.2.3", "1.2.3-", "1.2.3-rc..1", "-1.2.3"})
		CHECK_FALSE_MESSAGE(ParseVersion(text).has_value(), text);
}

TEST_CASE("compare")
{
	CHECK(CompareVersions(V("1.2.3"), V("1.2.3")) == 0);
	CHECK(CompareVersions(V("1.2.3+qa"), V("1.2.3")) == 0);
	CHECK(CompareVersions(V("1.2.3"), V("1.2.4")) < 0);
	CHECK(CompareVersions(V("1.10.0"), V("1.9.9")) > 0);
	CHECK(CompareVersions(V("2.0.0"), V("1.99.99")) > 0);
}

TEST_CASE("pre-release precedence follows SemVer")
{
	// 1.0.0-alpha < 1.0.0-alpha.1 < 1.0.0-alpha.beta < 1.0.0-beta < 1.0.0-beta.2
	//   < 1.0.0-beta.11 < 1.0.0-rc.1 < 1.0.0
	const char *ordered[] = {"1.0.0-alpha",  "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta",
				 "1.0.0-beta.2", "1.0.0-beta.11", "1.0.0-rc.1",       "1.0.0"};
	for (size_t i = 0; i + 1 < std::size(ordered); ++i)
		CHECK_MESSAGE(CompareVersions(V(ordered[i]), V(ordered[i + 1])) < 0, ordered[i]);

	CHECK(CompareVersions(V("1.2.0-rc.2"), V("1.2.0-rc.10")) < 0);
}

TEST_CASE("forced update")
{
	const Version minSupported = V("2.0.0");
	CHECK(IsUpdateRequired(V("1.9.9"), minSupported));
	CHECK(IsUpdateRequired(V("2.0.0-rc.1"), minSupported));
	CHECK_FALSE(IsUpdateRequired(V("2.0.0"), minSupported));
	CHECK_FALSE(IsUpdateRequired(V("2.3.1"), minSupported));
}
