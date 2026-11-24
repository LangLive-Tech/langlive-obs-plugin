#include "app/mock-langlive-api.hpp"

#include <doctest/doctest.h>

using namespace langlive::obsplugin;

TEST_CASE("ret_code maps to the error the plugin acts on")
{
	CHECK(ApiErrorKindFromRetCode("0") == ApiError::Kind::None);
	CHECK(ApiErrorKindFromRetCode("99300") == ApiError::Kind::TooFrequent);
	CHECK(ApiErrorKindFromRetCode("99301") == ApiError::Kind::SmsLimitReached);
	CHECK(ApiErrorKindFromRetCode("99302") == ApiError::Kind::NotEligible);
	CHECK(ApiErrorKindFromRetCode("99303") == ApiError::Kind::SessionExpired);
	CHECK(ApiErrorKindFromRetCode("99304") == ApiError::Kind::WebLiveActive);
	CHECK(ApiErrorKindFromRetCode("781") == ApiError::Kind::Other);
}

TEST_CASE("X-LOCAL follows OBS's language, English for languages the backend lacks")
{
	CHECK(ApiLocale("zh-TW") == "zh-TW");
	CHECK(ApiLocale("zh-CN") == "zh-CN");
	CHECK(ApiLocale("en-US") == "en-US");
	CHECK(ApiLocale("ja-JP") == "en-US");
	CHECK(ApiLocale("") == "en-US");
}

TEST_CASE("mock API: at most 10 SMS codes per number per day")
{
	MockLangLiveApi api;
	ApiError last;
	for (int i = 0; i < 10; ++i) {
		api.SendSmsCode("+886912345678", [&](ApiError e) { last = e; });
		REQUIRE(last.ok());
	}
	api.SendSmsCode("+886912345678", [&](ApiError e) { last = e; });
	CHECK(last.kind == ApiError::Kind::SmsLimitReached);

	api.SendSmsCode("+886987654321", [&](ApiError e) { last = e; });
	CHECK(last.ok());
}

TEST_CASE("mock API: calls need the token from login")
{
	MockLangLiveApi api;
	ApiError result;
	api.FetchStreamingInfo("not-a-token", [&](ApiError e, StreamingInfo) { result = e; });
	CHECK(result.kind == ApiError::Kind::SessionExpired);

	std::string token;
	std::string nickname;
	api.LoginWithSmsCode("+886912345678", "123456", [&](ApiError e, std::string t, std::string n) {
		result = e;
		token = std::move(t);
		nickname = std::move(n);
	});
	REQUIRE(result.ok());
	CHECK_FALSE(token.empty());
	CHECK_FALSE(nickname.empty());

	StreamingInfo info;
	api.FetchStreamingInfo(token, [&](ApiError e, StreamingInfo i) {
		result = e;
		info = std::move(i);
	});
	CHECK(result.ok());
	CHECK_FALSE(info.channels.empty());
	CHECK_FALSE(info.live);
}

TEST_CASE("mock API: settings are kept, and start / close track the live")
{
	MockLangLiveApi api;
	std::string token;
	api.LoginWithSmsCode("+886912345678", "123456", [&](ApiError, std::string t, std::string) { token = t; });

	ApiError result;
	api.SaveStreamingSettings(token, {"My title", "Come watch", "mock-channel-2"}, [&](ApiError e) { result = e; });
	REQUIRE(result.ok());

	StreamTarget target;
	api.StartStreaming(token, {"mock://cover.jpg", "My title", "", "mock-channel-2", "", ""},
			   [&](ApiError e, StreamTarget t) {
				   result = e;
				   target = std::move(t);
			   });
	REQUIRE(result.ok());
	CHECK(target.serverUrl.empty()); // OBS keeps its configured server and key
	CHECK_FALSE(target.liveId.empty());

	StreamingInfo info;
	api.FetchStreamingInfo(token, [&](ApiError, StreamingInfo i) { info = std::move(i); });
	CHECK(info.title == "My title");
	CHECK(info.channelId == "mock-channel-2");
	CHECK(info.live);
	CHECK(info.platform == StreamingInfo::Platform::Plugin);
	CHECK(info.liveId == target.liveId);

	api.CloseStreaming(token, "someone-else", [&](ApiError e) { result = e; });
	CHECK(result.code == "19");
	api.CloseStreaming(token, target.liveId, [&](ApiError e) { result = e; });
	CHECK(result.ok());
	api.FetchStreamingInfo(token, [&](ApiError, StreamingInfo i) { info = std::move(i); });
	CHECK_FALSE(info.live);
}
