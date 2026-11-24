#include "obs/qa-override.hpp"

#include <doctest/doctest.h>

#include <QFile>
#include <QTemporaryDir>

using langlive::obsplugin::obs::ReadManifestOverride;

namespace {

std::string Write(const QTemporaryDir &dir, const QByteArray &content)
{
	const QString path = dir.filePath(QStringLiteral("qa-override.json"));
	QFile file(path);
	REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
	file.write(content);
	return path.toStdString();
}

} // namespace

TEST_CASE("QA override: the manifest URL from qa-override.json")
{
	QTemporaryDir dir;
	REQUIRE(dir.isValid());

	SUBCASE("no file: no override")
	{
		const auto result = ReadManifestOverride(dir.filePath(QStringLiteral("missing.json")).toStdString());
		CHECK(result.url.empty());
		CHECK(result.error.empty());
	}
	SUBCASE("an HTTPS URL")
	{
		const auto result = ReadManifestOverride(
			Write(dir, R"({"update_manifest_url": "https://example.com/test/scenarios/forced.json"})"));
		CHECK(result.url == "https://example.com/test/scenarios/forced.json");
		CHECK(result.error.empty());
	}
	SUBCASE("no URL in the file: no override")
	{
		const auto result = ReadManifestOverride(Write(dir, "{}"));
		CHECK(result.url.empty());
		CHECK(result.error.empty());
	}
	SUBCASE("plain HTTP is refused")
	{
		const auto result = ReadManifestOverride(
			Write(dir, R"({"update_manifest_url": "http://example.com/latest.json"})"));
		CHECK(result.url.empty());
		CHECK_FALSE(result.error.empty());
	}
	SUBCASE("not JSON")
	{
		const auto result = ReadManifestOverride(Write(dir, "update_manifest_url=https://example.com"));
		CHECK(result.url.empty());
		CHECK_FALSE(result.error.empty());
	}
}
