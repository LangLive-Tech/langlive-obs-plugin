#include "langlive/obsplugin/core/update.hpp"

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

TEST_CASE("update status")
{
	CHECK(EvaluateUpdate(V("1.2.0"), V("1.2.0"), V("1.0.0")) == UpdateStatus::UpToDate);
	CHECK(EvaluateUpdate(V("1.3.0"), V("1.2.0"), V("1.0.0")) == UpdateStatus::UpToDate); // ahead of the manifest
	CHECK(EvaluateUpdate(V("1.1.0"), V("1.2.0"), V("1.0.0")) == UpdateStatus::Available);
	CHECK(EvaluateUpdate(V("1.2.0-rc.1"), V("1.2.0"), V("1.0.0")) == UpdateStatus::Available);
	CHECK(EvaluateUpdate(V("1.9.0"), V("2.0.0"), V("2.0.0")) == UpdateStatus::Required);
	CHECK(EvaluateUpdate(V("0.9.9"), V("1.2.0"), V("1.0.0")) == UpdateStatus::Required);
}

TEST_CASE("update URLs: HTTPS anywhere, HTTP only on localhost")
{
	CHECK(IsAllowedUpdateUrl("https://download.example.com/obs/latest.json"));
	CHECK(IsAllowedUpdateUrl("HTTPS://download.example.com/a.pkg"));
	CHECK(IsAllowedUpdateUrl("http://127.0.0.1:8765/latest.json"));
	CHECK(IsAllowedUpdateUrl("http://localhost/latest.json"));
	CHECK(IsAllowedUpdateUrl("http://[::1]:8765/latest.json"));

	CHECK_FALSE(IsAllowedUpdateUrl("http://download.example.com/latest.json"));
	CHECK_FALSE(IsAllowedUpdateUrl("http://127.0.0.1.example.com/latest.json"));
	CHECK_FALSE(IsAllowedUpdateUrl("http://localhost.example.com/latest.json"));
	CHECK_FALSE(IsAllowedUpdateUrl("ftp://example.com/a.pkg"));
	CHECK_FALSE(IsAllowedUpdateUrl("file:///tmp/a.pkg"));
	CHECK_FALSE(IsAllowedUpdateUrl("https://"));
	CHECK_FALSE(IsAllowedUpdateUrl(""));
}
