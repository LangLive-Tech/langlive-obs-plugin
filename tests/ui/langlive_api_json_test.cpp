// Parsing the LangLive API's JSON, using the examples from its
// documentation (with the documentation's typos fixed where it is not
// valid JSON).

#include "obs/langlive-api-json.hpp"

#include <doctest/doctest.h>

#include <QJsonDocument>

using namespace langlive::obsplugin;
namespace api_json = langlive::obsplugin::obs::api_json;

namespace {

QJsonObject Data(const char *json)
{
	QJsonObject data;
	REQUIRE(api_json::ParseEnvelope(QByteArray(json), &data).ok());
	return data;
}

} // namespace

TEST_CASE("API envelope: ret_code as a string or a number")
{
	QJsonObject data;
	ApiError result = api_json::ParseEnvelope(R"({"ret_code":"0","ret_msg":"ok","data":{"x":1}})", &data);
	CHECK(result.ok());
	CHECK(data.value("x").toInt() == 1);

	result = api_json::ParseEnvelope(R"({"ret_code":99303,"ret_msg":"expired"})", &data);
	CHECK(result.kind == ApiError::Kind::SessionExpired);
	CHECK(result.code == "99303");
	CHECK(result.message == "expired");
	CHECK(data.isEmpty());
}

TEST_CASE("API envelope: anything else is an invalid response")
{
	CHECK(api_json::ParseEnvelope("<html>502</html>", nullptr).kind == ApiError::Kind::InvalidResponse);
	CHECK(api_json::ParseEnvelope("[]", nullptr).kind == ApiError::Kind::InvalidResponse);
	CHECK(api_json::ParseEnvelope(R"({"ret_msg":"ok"})", nullptr).kind == ApiError::Kind::InvalidResponse);
}

TEST_CASE("API sms/login: token, and the nickname from ret_msg")
{
	const QByteArray body = QStringLiteral(R"({"ret_code":"0","ret_msg":"小浪，登入成功",)"
					       R"("data":{"authorization_token":"123456"}})")
					.toUtf8();
	QJsonObject data;
	const ApiError result = api_json::ParseEnvelope(body, &data);
	REQUIRE(result.ok());

	std::string token;
	std::string nickname;
	REQUIRE(api_json::ParseLogin(data, QString::fromStdString(result.message), &token, &nickname));
	CHECK(token == "123456");
	CHECK(nickname == QStringLiteral("小浪").toStdString());

	CHECK_FALSE(api_json::ParseLogin(QJsonObject(), QString(), &token, &nickname));
}

TEST_CASE("API streaming/info: the documented example")
{
	StreamingInfo info;
	REQUIRE(api_json::ParseStreamingInfo(Data(R"({
		"ret_code": "0", "ret_msg": "ok",
		"data": {
			"liveimg": "https://img.example.com/cover.jpg",
			"title": "Title", "push_text": "Push", "topic_id": "",
			"topic_list": [{"topic_id": "84", "topic_name": "Chat"}, {"topic_id": 85, "topic_name": "Music"}],
			"act1": 0, "act2": 0,
			"act_list": [{"name": "None", "tab_id": 0}, {"name": "Event", "tab_id": 12}],
			"stream_info": {"direction": 0, "status": 1, "platform": "OBS_PLUGIN", "live_id": "L123"}
		}})"),
					     &info));
	CHECK(info.coverUrl == "https://img.example.com/cover.jpg");
	CHECK(info.title == "Title");
	CHECK(info.pushNotification == "Push");
	CHECK(info.channelId.empty());
	REQUIRE(info.channels.size() == 2);
	CHECK(info.channels[0].id == "84");
	CHECK(info.channels[1].id == "85");
	CHECK(info.channels[1].name == "Music");
	CHECK(info.mainEventId == "0");
	REQUIRE(info.events.size() == 2);
	CHECK(info.events[1].id == "12");
	CHECK(info.orientation == core::Orientation::Portrait);
	CHECK(info.live);
	CHECK(info.platform == StreamingInfo::Platform::Plugin);
	CHECK(info.liveId == "L123");
}

TEST_CASE("API streaming/info: landscape, not live, started from the website")
{
	StreamingInfo info;
	REQUIRE(api_json::ParseStreamingInfo(
		Data(R"({"ret_code":"0","data":{"stream_info":{"direction":1,"status":0,"platform":"OBS_WEB"}}})"),
		&info));
	CHECK(info.orientation == core::Orientation::Landscape);
	CHECK_FALSE(info.live);
	CHECK(info.platform == StreamingInfo::Platform::Web);
	CHECK(info.liveId.empty());
}

TEST_CASE("API streaming/info: status and direction as strings")
{
	StreamingInfo info;
	REQUIRE(api_json::ParseStreamingInfo(
		Data(R"({"ret_code":"0","data":{"stream_info":{"direction":"1","status":"1","live_id":"L9"}}})"),
		&info));
	CHECK(info.orientation == core::Orientation::Landscape);
	CHECK(info.live);
}

TEST_CASE("API streaming/info: no stream_info is not 'not live'")
{
	StreamingInfo info;
	CHECK_FALSE(api_json::ParseStreamingInfo(Data(R"({"ret_code":"0","data":{"title":"x"}})"), &info));
	CHECK_FALSE(api_json::ParseStreamingInfo(QJsonObject(), &info)); // data: [] or missing
}

TEST_CASE("API responses: a gateway error page is a network failure, not an invalid answer")
{
	QJsonObject data;
	// The envelope wins, whatever the status.
	CHECK(api_json::ParseResponse(500, "", R"({"ret_code":"99303","ret_msg":"x"})", &data).kind ==
	      ApiError::Kind::SessionExpired);
	CHECK(api_json::ParseResponse(503, "", "<html>Service Unavailable</html>", &data).kind ==
	      ApiError::Kind::Network);
	CHECK(api_json::ParseResponse(502, "", "", &data).kind == ApiError::Kind::Network);
	CHECK(api_json::ParseResponse(0, "Could not resolve host", "", &data).kind == ApiError::Kind::Network);
	// A 200 that is not the envelope is the backend's answer, just wrong.
	CHECK(api_json::ParseResponse(200, "", "<html>ok</html>", &data).kind == ApiError::Kind::InvalidResponse);
}

TEST_CASE("API streaming/start: request body and stream target")
{
	const QJsonObject body =
		api_json::StartStreamingBody({"https://img.example.com/c.jpg", "Title", "", "84", "12", ""});
	CHECK(body.value("liveimg").toString() == "https://img.example.com/c.jpg");
	CHECK(body.value("title").toString() == "Title");
	CHECK(body.value("topic_id").toInteger() == 84);
	CHECK(body.value("act1").toInteger() == 12);
	CHECK_FALSE(body.contains("act2")); // optional and not chosen
	// Optional, but the backend fails without it: sent empty.
	REQUIRE(body.contains("push_text"));
	CHECK(body.value("push_text").toString().isEmpty());

	StreamTarget target;
	REQUIRE(api_json::ParseStreamTarget(
		Data(R"({"ret_code":"0","data":{"push_url":"rtmp://ingest.example.com/live","push_code":"key",
			"stream_info":{"status":1,"platform":"OBS_PLUGIN","live_id":"L123"}}})"),
		&target));
	CHECK(target.serverUrl == "rtmp://ingest.example.com/live");
	CHECK(target.streamKey == "key");
	CHECK(target.liveId == "L123");
	CHECK_FALSE(api_json::ParseStreamTarget(QJsonObject(), &target));
}

TEST_CASE("API upload/image: the documented example")
{
	CoverUpload upload;
	REQUIRE(api_json::ParseCoverUpload(Data(R"({"ret_code":"0","ret_msg":"ok","data":{
			"uploadUrl":"https://bucket.s3.example.com/live/1/f?X-Amz-Signature=x",
			"playUrl":"https://static.example.com/live/1/f",
			"header":{"Cache-Control":"max-age=2592000","Content-Type":"image/png"}}})"),
					   &upload));
	CHECK(upload.uploadUrl == "https://bucket.s3.example.com/live/1/f?X-Amz-Signature=x");
	CHECK(upload.imageUrl == "https://static.example.com/live/1/f");
	CHECK(upload.headers == std::vector<std::pair<std::string, std::string>>{{"Content-Type", "image/png"},
										 {"Cache-Control", "max-age=2592000"}});

	// No headers: still sent as an image.
	REQUIRE(api_json::ParseCoverUpload(
		Data(R"({"ret_code":"0","data":{"uploadUrl":"https://u","playUrl":"https://p"}})"), &upload));
	CHECK(upload.headers == std::vector<std::pair<std::string, std::string>>{{"Content-Type", "image/jpeg"}});

	// A value that would add a header line, or a missing URL.
	CHECK_FALSE(api_json::ParseCoverUpload(
		Data(R"({"ret_code":"0","data":{"uploadUrl":"https://u","playUrl":"https://p",
			"header":{"Content-Type":"image/png\r\nX-Evil: 1"}}})"),
		&upload));
	CHECK_FALSE(api_json::ParseCoverUpload(Data(R"({"ret_code":"0","data":{"playUrl":"https://p"}})"), &upload));
}

TEST_CASE("API upload/image: the daily limit, as the backend answers it")
{
	// Observed: ret_code -1 with "data": [] (the documentation says an empty
	// object). The message is for the streamer.
	QJsonObject data;
	const ApiError error = api_json::ParseEnvelope(
		QByteArray(R"({"ret_code":"-1","ret_msg":"Daily upload limit reached","data":[]})"), &data);
	CHECK(error.kind == ApiError::Kind::Other);
	CHECK(error.code == "-1");
	CHECK(error.message == "Daily upload limit reached");
	CHECK(data.isEmpty());
}

TEST_CASE("API qrcode/create: the documented example")
{
	QrLoginCode code;
	REQUIRE(api_json::ParseQrLoginCode(Data(R"({"ret_code":"0","ret_msg":"ok","data":{"uuid":"U1","token":"T1",
			"qrcode_data":"https://example.com/login/qrcode_auth.html?uuid=U1&token=T1",
			"qrcode_url":"https://example.com/qrcode/create?data=x","server_time":1700000000000}})"),
					   &code));
	CHECK(code.uuid == "U1");
	CHECK(code.token == "T1");
	CHECK(code.content == "https://example.com/login/qrcode_auth.html?uuid=U1&token=T1");
	CHECK_FALSE(api_json::ParseQrLoginCode(Data(R"({"ret_code":"0","data":{"uuid":"U1","token":"T1"}})"), &code));
}

TEST_CASE("API qrcode/verify: its states and errors")
{
	const auto parse = [](const char *json, ApiError *error, QrLoginState *state) {
		QJsonObject data;
		*error = api_json::ParseEnvelope(QByteArray(json), &data);
		return api_json::ParseQrLoginState(error, data, state);
	};
	ApiError error;
	QrLoginState state;

	REQUIRE(parse(R"({"ret_code":"1802","ret_msg":"waiting","data":{"server_time":1}})", &error, &state));
	CHECK(state.status == QrLoginState::Status::Waiting);
	REQUIRE(parse(R"({"ret_code":"1801","ret_msg":"scanned","data":{"nickname":"N","avatar":""}})", &error,
		      &state));
	CHECK(state.status == QrLoginState::Status::Scanned);
	// After 1801 the backend answers 1802 again ("waiting for the
	// confirmation"), with the same data as before the scan.
	REQUIRE(parse(R"({"ret_code":"1802","ret_msg":"confirming","data":{"server_time":2}})", &error, &state));
	CHECK(state.status == QrLoginState::Status::Waiting);
	// As the backend answers it: "data": [].
	REQUIRE(parse(R"({"ret_code":"-1","ret_msg":"expired","data":[]})", &error, &state));
	CHECK(state.status == QrLoginState::Status::Expired);

	// Logged in: the token, and the nickname from ret_msg as for sms/login.
	REQUIRE(parse("{\"ret_code\":\"0\",\"ret_msg\":\"Streamer\uff0c\u767b\u5165\u6210\u529f\","
		      "\"data\":{\"authorization_token\":\"TOKEN\"}}",
		      &error, &state));
	CHECK(state.status == QrLoginState::Status::LoggedIn);
	CHECK(state.token == "TOKEN");
	CHECK(state.nickname == "Streamer");
	CHECK_FALSE(parse(R"({"ret_code":"0","ret_msg":"ok","data":{}})", &error, &state));
	CHECK(error.kind == ApiError::Kind::InvalidResponse);

	CHECK_FALSE(parse(R"({"ret_code":"99302","ret_msg":"not eligible","data":{}})", &error, &state));
	CHECK(error.kind == ApiError::Kind::NotEligible);
	CHECK_FALSE(parse(R"({"ret_code":"609","ret_msg":"deleted","data":{}})", &error, &state));
	CHECK(error.kind == ApiError::Kind::Other);
	CHECK(error.message == "deleted");

	CHECK(api_json::IsQrLoginPending("1801"));
	CHECK(api_json::IsQrLoginPending("1802"));
	CHECK_FALSE(api_json::IsQrLoginPending("0"));
	CHECK_FALSE(api_json::IsQrLoginPending("-1"));
}

TEST_CASE("API streaming/setting: request body")
{
	const QJsonObject body = api_json::StreamingSettingsBody({"Title", "Push", "84"});
	CHECK(body.value("title").toString() == "Title");
	CHECK(body.value("push_text").toString() == "Push");
	CHECK(body.value("topic_id").toInteger() == 84);

	// An empty notification is sent, so clearing it clears it on the backend.
	const QJsonObject cleared = api_json::StreamingSettingsBody({"Title", "", "84"});
	REQUIRE(cleared.contains("push_text"));
	CHECK(cleared.value("push_text").toString().isEmpty());
}
