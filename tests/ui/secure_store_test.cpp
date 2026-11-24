#include "obs/secure-store.hpp"

#include <doctest/doctest.h>

#include <QCoreApplication>

#include <cstdlib>
#include <string>

using namespace langlive::obsplugin::obs;

// Uses the real keychain / Credential Manager, under a name of its own.
TEST_CASE("secure store: save, read, replace, delete")
{
#if defined(__APPLE__)
	// A CI runner's login keychain may be locked, and unlocking would ask.
	if (std::getenv("CI")) {
		MESSAGE("skipped on macOS CI");
		return;
	}
#endif
	const std::string name = "test-" + std::to_string(QCoreApplication::applicationPid());
	REQUIRE(DeleteSecret(name)); // nothing there: still true
	CHECK_FALSE(LoadSecret(name).has_value());

	REQUIRE(SaveSecret(name, "first-token"));
	CHECK(LoadSecret(name) == std::optional<std::string>("first-token"));

	REQUIRE(SaveSecret(name, "second-token-with-ünicode"));
	CHECK(LoadSecret(name) == std::optional<std::string>("second-token-with-ünicode"));

	CHECK(DeleteSecret(name));
	CHECK_FALSE(LoadSecret(name).has_value());
}
