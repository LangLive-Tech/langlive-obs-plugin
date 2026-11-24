#include "app/mock-langlive-api.hpp"

#include <ctime>
#include <utility>

namespace langlive::obsplugin {

namespace {

// The backend sends at most this many codes per number per day; the day
// starts at 00:00 Taiwan time (UTC+8).
constexpr int kSmsDailyLimit = 10;
constexpr long long kTaiwanUtcOffsetSeconds = 8 * 60 * 60;
constexpr const char *kMockLiveId = "mock-live-1";

ApiError Error(ApiError::Kind kind, const char *code, const char *message)
{
	return {kind, code, message};
}

ApiError SessionExpired()
{
	return Error(ApiError::Kind::SessionExpired, "99303", "session expired");
}

} // namespace

void MockLangLiveApi::SendSmsCode(const std::string &cellphone, std::function<void(ApiError)> done)
{
	const long long today = (static_cast<long long>(std::time(nullptr)) + kTaiwanUtcOffsetSeconds) / 86400;
	if (today != smsDay_) {
		smsDay_ = today;
		smsSentToday_.clear();
	}
	int &sent = smsSentToday_[cellphone];
	if (sent >= kSmsDailyLimit) {
		done(Error(ApiError::Kind::SmsLimitReached, "99301", "too many codes"));
		return;
	}
	++sent;
	done({});
}

void MockLangLiveApi::LoginWithSmsCode(const std::string &cellphone, const std::string &code,
				       std::function<void(ApiError, std::string, std::string)> done)
{
	(void)code;
	done({}, kTokenPrefix + cellphone, cellphone.empty() ? std::string("Test Streamer") : cellphone);
}

void MockLangLiveApi::FetchStreamingInfo(const std::string &token, std::function<void(ApiError, StreamingInfo)> done)
{
	if (!ValidToken(token)) {
		done(SessionExpired(), {});
		return;
	}
	StreamingInfo info;
	info.title = saved_.title;
	info.pushNotification = saved_.pushNotification;
	info.channelId = saved_.channelId;
	info.channels = {{"mock-channel-1", "Test Channel 1"},
			 {"mock-channel-2", "Test Channel 2"},
			 {"mock-channel-3", "Test Channel 3"}};
	info.mainEventId = "0";
	info.subEventId = "0";
	info.events = {{"0", "None"}, {"mock-event-1", "Test Event 1"}, {"mock-event-2", "Test Event 2"}};
	info.orientation = core::Orientation::Landscape;
	info.live = live_;
	info.platform = live_ ? StreamingInfo::Platform::Plugin : StreamingInfo::Platform::None;
	info.liveId = live_ ? kMockLiveId : "";
	done({}, std::move(info));
}

void MockLangLiveApi::SaveStreamingSettings(const std::string &token, const StreamingSettings &settings,
					    std::function<void(ApiError)> done)
{
	if (!ValidToken(token)) {
		done(SessionExpired());
		return;
	}
	saved_ = settings;
	done({});
}

void MockLangLiveApi::RequestCoverUpload(const std::string &token, const std::string &filename,
					 std::function<void(ApiError, CoverUpload)> done)
{
	(void)filename;
	if (!ValidToken(token)) {
		done(SessionExpired(), {});
		return;
	}
	done({}, {"mock://upload/cover.jpg", "mock://cover.jpg", {{"Content-Type", "image/jpeg"}}});
}

void MockLangLiveApi::UploadCover(const CoverUpload &upload, const std::string &imagePath,
				  std::function<void(ApiError)> done)
{
	(void)upload;
	(void)imagePath;
	done({});
}

// The mock's covers (mock://) are not real images: none to show.
void MockLangLiveApi::DownloadImage(const std::string &url, std::function<void(ApiError, std::string)> done)
{
	(void)url;
	done(Error(ApiError::Kind::Network, "", "the mock backend has no images"), {});
}

void MockLangLiveApi::StartStreaming(const std::string &token, const StartStreamingRequest &request,
				     std::function<void(ApiError, StreamTarget)> done)
{
	(void)request;
	if (!ValidToken(token)) {
		done(SessionExpired(), {});
		return;
	}
	live_ = true;
	// No server or key: the plugin keeps OBS's configured server and key.
	StreamTarget target;
	target.liveId = kMockLiveId;
	done({}, target);
}

ApiError MockLangLiveApi::CloseStreamingNow(const std::string &token, const std::string &liveId, int)
{
	ApiError result;
	CloseStreaming(token, liveId, [&result](ApiError error) { result = std::move(error); });
	return result;
}

void MockLangLiveApi::CloseStreaming(const std::string &token, const std::string &liveId,
				     std::function<void(ApiError)> done)
{
	if (!ValidToken(token)) {
		done(SessionExpired());
		return;
	}
	if (!live_ || liveId != kMockLiveId) {
		done(Error(ApiError::Kind::Other, "19", "invalid live_id"));
		return;
	}
	live_ = false;
	done({});
}

void MockLangLiveApi::Logout(const std::string &token, std::function<void(ApiError)> done)
{
	done(ValidToken(token) ? ApiError{} : SessionExpired());
}

void MockLangLiveApi::CreateQrLogin(std::function<void(ApiError, QrLoginCode)> done)
{
	const std::string id = std::to_string(++qrCodesCreated_);
	qrChecks_["mock-qr-" + id] = 0;
	done({}, {"mock-qr-" + id, "mock-qr-token", "LANGLIVE-OBS-LOGIN-MOCK:" + id});
}

void MockLangLiveApi::CheckQrLogin(const QrLoginCode &code, std::function<void(ApiError, QrLoginState)> done)
{
	const auto it = qrChecks_.find(code.uuid);
	if (it == qrChecks_.end()) {
		done({}, {QrLoginState::Status::Expired, {}, {}});
		return;
	}
	// Like the backend: "scanned" once, then waiting again for the
	// confirmation.
	const int checks = ++it->second;
	if (checks == 3) {
		done({}, {QrLoginState::Status::Scanned, {}, {}});
	} else if (checks < 5) {
		done({}, {QrLoginState::Status::Waiting, {}, {}});
	} else {
		qrChecks_.erase(it); // used once
		done({}, {QrLoginState::Status::LoggedIn, std::string(kTokenPrefix) + "qr", "QR Streamer"});
	}
}

} // namespace langlive::obsplugin
