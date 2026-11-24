#pragma once

#include "langlive/obsplugin/core/stream_spec.hpp"

#include <functional>
#include <string>
#include <utility>
#include <vector>

// The LangLive backend API used by the plugin (/v6/obs_plugin/...). Plain
// C++ types so the application layer stays free of Qt; implemented over
// HTTP in src/obs and by MockLangLiveApi.

namespace langlive::obsplugin {

// Outcome of a call. `kind` is what the plugin acts on; `code` and
// `message` are the server's ret_code / ret_msg, for logs.
struct ApiError {
	enum class Kind {
		None,            // success (ret_code 0)
		Network,         // no response, timeout, or an HTTP error status
		InvalidResponse, // not the expected JSON
		TooFrequent,     // 99300: try again later
		SmsLimitReached, // 99301: this number got too many codes today
		NotEligible,     // 99302: the account may not stream from OBS
		SessionExpired,  // 99303: the token is no longer valid; log in again
		WebLiveActive,   // 99304: a live started from the website is running
		Other,           // any other ret_code
	};
	Kind kind = Kind::None;
	std::string code;
	std::string message;

	bool ok() const { return kind == Kind::None; }
	// The backend no longer accepts the login: the token is invalid
	// (99303), or the account may no longer stream from OBS (99302). The
	// plugin has logged out; the menu tells the streamer.
	bool endsLogin() const { return kind == Kind::SessionExpired || kind == Kind::NotEligible; }
};

// Maps a ret_code to ApiError::Kind.
ApiError::Kind ApiErrorKindFromRetCode(const std::string &retCode);

// The X-LOCAL header for an OBS locale ("zh-TW"), choosing the language of
// the backend's ret_msg: zh-TW, zh-CN, or en-US for any other language.
std::string ApiLocale(const std::string &obsLocale);

// One entry of a backend list (channels, events).
struct ApiChoice {
	std::string id;
	std::string name;
};

// GET streaming/info: the live room as the backend has it.
struct StreamingInfo {
	std::string coverUrl;         // liveimg
	std::string title;            // title
	std::string pushNotification; // push_text
	std::string channelId;        // topic_id, empty if none saved
	std::vector<ApiChoice> channels;
	std::string mainEventId; // act1
	std::string subEventId;  // act2
	std::vector<ApiChoice> events;
	core::Orientation orientation = core::Orientation::Portrait; // direction: 0 portrait, 1 landscape

	enum class Platform { None, Web, Plugin };
	bool live = false; // status 1
	Platform platform = Platform::None;
	std::string liveId; // live_id, empty when not live
};

// POST streaming/start.
struct StartStreamingRequest {
	std::string coverUrl; // an uploaded cover's URL
	std::string title;
	std::string pushNotification; // optional
	std::string channelId;
	std::string mainEventId; // optional
	std::string subEventId;  // optional
};

// The live streaming/start opened: where OBS streams to (push_url / push_code
// in the API) and the id streaming/close needs (stream_info.live_id).
struct StreamTarget {
	std::string serverUrl;
	std::string streamKey;
	std::string liveId;
};

// POST streaming/setting.
struct StreamingSettings {
	std::string title;
	std::string pushNotification;
	std::string channelId;
};

// A pre-signed upload for a cover image (upload/image).
struct CoverUpload {
	std::string uploadUrl; // HTTP PUT the image here (uploadUrl)
	std::string imageUrl;  // then pass this as the cover in streaming/start (playUrl)
	// Headers the PUT must send (header): Content-Type, Cache-Control.
	std::vector<std::pair<std::string, std::string>> headers;
};

// GET qrcode/create: a login QR code, valid 300 seconds. `uuid` and `token`
// identify it for qrcode/verify; this token is not a login.
struct QrLoginCode {
	std::string uuid;
	std::string token;
	std::string content; // qrcode_data: what the QR code holds (the app opens it)
};

// POST qrcode/verify: where a QR code login stands.
struct QrLoginState {
	enum class Status {
		Waiting,  // 1802: not scanned yet, or scanned and waiting for the confirmation
		Scanned,  // 1801: the app scanned it; the streamer confirms in the app
		LoggedIn, // 0: confirmed; `token` authorizes the calls below
		Expired,  // -1: expired, used, or unknown; a new code is needed
	};
	Status status = Status::Waiting;
	std::string token;
	std::string nickname; // LoggedIn
};

// Callbacks run on the main thread, possibly before the call returns (the
// mock answers immediately).
class LangLiveApi {
public:
	virtual ~LangLiveApi() = default;

	// POST sms/send_code
	virtual void SendSmsCode(const std::string &cellphone, std::function<void(ApiError)> done) = 0;

	// POST sms/login. On success, `token` authorizes the calls below and
	// `nickname` is the account's name.
	virtual void LoginWithSmsCode(const std::string &cellphone, const std::string &code,
				      std::function<void(ApiError, std::string token, std::string nickname)> done) = 0;

	// GET streaming/info
	virtual void FetchStreamingInfo(const std::string &token,
					std::function<void(ApiError, StreamingInfo)> done) = 0;

	// POST streaming/setting
	virtual void SaveStreamingSettings(const std::string &token, const StreamingSettings &settings,
					   std::function<void(ApiError)> done) = 0;

	// POST upload/image: a pre-signed URL for an image named `filename`
	// (the extension sets its type), valid 15 minutes. At most 20 a day per
	// account; more answer -1 with a message for the streamer.
	virtual void RequestCoverUpload(const std::string &token, const std::string &filename,
					std::function<void(ApiError, CoverUpload)> done) = 0;
	// HTTP PUT of the image file to the pre-signed URL.
	virtual void UploadCover(const CoverUpload &upload, const std::string &imagePath,
				 std::function<void(ApiError)> done) = 0;
	// HTTPS GET of an image the backend refers to, e.g. the room's cover
	// from streaming/info: its bytes, at most a few MB.
	virtual void DownloadImage(const std::string &url, std::function<void(ApiError, std::string)> done) = 0;

	// POST streaming/start
	virtual void StartStreaming(const std::string &token, const StartStreamingRequest &request,
				    std::function<void(ApiError, StreamTarget)> done) = 0;

	// POST streaming/close, for the live streaming/start returned
	virtual void CloseStreaming(const std::string &token, const std::string &liveId,
				    std::function<void(ApiError)> done) = 0;
	// The same, waiting at most `timeoutSeconds` for the answer, for when
	// OBS exits and no callback could run any more.
	virtual ApiError CloseStreamingNow(const std::string &token, const std::string &liveId, int timeoutSeconds) = 0;

	// POST logout: revokes this token (only this one; a live it opened goes
	// on). Calls with it then answer 99303.
	virtual void Logout(const std::string &token, std::function<void(ApiError)> done) = 0;

	// GET qrcode/create, no token needed.
	virtual void CreateQrLogin(std::function<void(ApiError, QrLoginCode)> done) = 0;
	// POST qrcode/verify, polled every 1 to 2 seconds. The waiting states are
	// not errors; 99302 (not eligible) and 609 (account deleted) are.
	virtual void CheckQrLogin(const QrLoginCode &code, std::function<void(ApiError, QrLoginState)> done) = 0;
};

} // namespace langlive::obsplugin
