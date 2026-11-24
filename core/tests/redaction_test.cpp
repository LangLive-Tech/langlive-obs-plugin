#include "langlive/obsplugin/core/redaction.hpp"

#include <doctest/doctest.h>

using namespace langlive::obsplugin::core;

TEST_CASE("JSON fields")
{
	CHECK(RedactSensitiveFields(R"({"server": "rtmp://example.com/live", "key": "abc123"})") ==
	      R"({"server": "rtmp://example.com/live", "key": "[REDACTED]"})");
	CHECK(RedactSensitiveFields(R"({"Password":"hunter2","access_token" : "t0k"})") ==
	      R"({"Password":"[REDACTED]","access_token" : "[REDACTED]"})");
}

TEST_CASE("INI lines")
{
	CHECK(RedactSensitiveFields("[Stream]\nkey=abc123\nserver=rtmp://example.com\n") ==
	      "[Stream]\nkey=[REDACTED]\nserver=rtmp://example.com\n");
	CHECK(RedactSensitiveFields("password = hunter2") == "password = [REDACTED]");
}

TEST_CASE("any field name ending in a secret word, in any case")
{
	// OBS's basic.ini keeps third-party logins (Twitch, YouTube, Restream).
	CHECK(RedactSensitiveFields(
		      "[Twitch]\nName=streamer\nRefreshToken=r3fresh\nToken=acc3ss\nExpireTime=1700000000\n") ==
	      "[Twitch]\nName=streamer\nRefreshToken=[REDACTED]\nToken=[REDACTED]\nExpireTime=1700000000\n");
	// WHIP's service.json
	CHECK(RedactSensitiveFields(R"({"server": "https://whip.example.com", "bearer_token": "b34rer"})") ==
	      R"({"server": "https://whip.example.com", "bearer_token": "[REDACTED]"})");
	CHECK(RedactSensitiveFields(R"({"StreamKey":"k","client_secret":"s","push_code":"x"})") ==
	      R"({"StreamKey":"[REDACTED]","client_secret":"[REDACTED]","push_code":"x"})");
	CHECK(RedactSensitiveFields("stream_key = abc\nApiKey=def") == "stream_key = [REDACTED]\nApiKey=[REDACTED]");
}

TEST_CASE("names that only start with a secret word are kept")
{
	const std::string encoder = R"({"keyint_sec": 2, "rate_control": "CBR", "keyframe_mode": "auto"})";
	CHECK(RedactSensitiveFields(encoder) == encoder);
	CHECK(RedactSensitiveFields("KeyframeIntervalSec=2\n") == "KeyframeIntervalSec=2\n");
}

TEST_CASE("a JSON value with escaped quotes is redacted whole")
{
	CHECK(RedactSensitiveFields(R"({"key": "ab\"cd\\", "server": "rtmp://x"})") ==
	      R"({"key": "[REDACTED]", "server": "rtmp://x"})");
}

TEST_CASE("signed RTMP URL parameters")
{
	CHECK(RedactSensitiveFields(
		      "rtmp://example.com/live/abc?push_args=XYZ&txSecret=deadbeef&txTime=6A99&txPRI=20") ==
	      "rtmp://example.com/live/abc?push_args=[REDACTED]&txSecret=[REDACTED]&txTime=[REDACTED]&txPRI=20");
}

TEST_CASE("bearer tokens")
{
	CHECK(RedactSensitiveFields("Authorization: Bearer eyJhbGciOi.payload.sig") ==
	      "Authorization: Bearer [REDACTED]");
}

TEST_CASE("non-sensitive text is unchanged")
{
	const std::string text = "11:22:33.444: [langlive-obs-plugin] keyframe interval set to 2s\n"
				 "Output resolution: 1280x720\n";
	CHECK(RedactSensitiveFields(text) == text);
}
