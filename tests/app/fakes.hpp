#pragma once

#include "app/plugin-controller.hpp"
#include "app/ports.hpp"

#include <functional>
#include <optional>
#include <utility>
#include <vector>
#include <string>

namespace langlive::obsplugin::testing {

// In-memory StreamingHost. Tests set the state OBS would report and read
// back what the controller asked OBS to do.
class FakeStreamingHost final : public StreamingHost {
public:
	// What OBS reports.
	std::string serverUrl = "rtmp://publish.langhongtw.com/live/";
	core::StreamState state;
	bool applySucceeds = true;
	bool simpleOutputMode = true;

	// What the controller did.
	int startCount = 0;
	int stopCount = 0;
	int clearKeyCount = 0;
	int keyframeFixCount = 0;
	int profileCodecsCount = 0;
	std::optional<core::StreamSpec> appliedSpec;
	std::string streamKey;

	std::string StreamingServerUrl() const override { return serverUrl; }
	core::StreamState ActiveStreamState() const override { return state; }
	// The fake has one state for both: what the profile says is what the
	// next stream uses. Like OBS's simple output mode, where the plugin sets
	// the keyframe interval while starting, that interval counts as right.
	core::StreamState ConfiguredStreamState() const override
	{
		core::StreamState configured = state;
		if (simpleOutputMode)
			configured.keyframeIntervalSeconds = core::kKeyframeIntervalSeconds;
		return configured;
	}

	// Like OBS: the profile then matches the spec, and the codecs are
	// switched to H.264 / AAC.
	bool ApplySpec(const core::StreamSpec &spec) override
	{
		appliedSpec = spec;
		if (!applySucceeds)
			return false;
		state.outputWidth = spec.width;
		state.outputHeight = spec.height;
		state.fps = spec.fps;
		state.bitrateKbps = spec.bitrateKbps;
		state.videoCodec = core::kVideoCodec;
		state.audioCodec = core::kAudioCodec;
		if (!simpleOutputMode)
			state.keyframeIntervalSeconds = core::kKeyframeIntervalSeconds;
		return true;
	}

	bool UseProfileCodecs() override
	{
		++profileCodecsCount;
		return false;
	}

	// Like OBS: only the simple-mode encoder takes the interval while
	// starting.
	bool SetSimpleModeKeyframeInterval(int seconds) override
	{
		++keyframeFixCount;
		if (!simpleOutputMode || state.keyframeIntervalSeconds == seconds)
			return false;
		state.keyframeIntervalSeconds = seconds;
		return true;
	}

	void SetStreamTarget(const std::string &server, const std::string &key) override
	{
		serverUrl = server;
		streamKey = key;
	}
	void ClearStreamKey() override
	{
		streamKey.clear();
		++clearKeyCount;
	}
	// Runs inside StartStreaming, like OBS refusing a start right away.
	std::function<void()> onStart;
	void StartStreaming() override
	{
		++startCount;
		if (onStart)
			onStart();
	}
	void StopStreaming() override { ++stopCount; }
};

// In-memory SettingsStore.
class FakeSettingsStore final : public SettingsStore {
public:
	GoLiveSettings stored;
	int saveCount = 0;

	std::optional<SavedSession> session;

	GoLiveSettings Load() const override { return stored; }
	std::string coverImagePath = "cover.jpg"; // UI tests that write covers use a temp folder
	std::string CoverImagePath() const override { return coverImagePath; }

	void Save(const GoLiveSettings &settings) override
	{
		stored = settings;
		++saveCount;
	}

	std::optional<SavedSession> LoadSession() const override { return session; }
	void SaveSession(const SavedSession &saved) override { session = saved; }
	void ClearSession() override { session.reset(); }

	std::string openLiveId;
	std::string LoadOpenLiveId() const override { return openLiveId; }
	void SaveOpenLiveId(const std::string &liveId) override { openLiveId = liveId; }
};

// UpdateFeed that answers synchronously with what the test set.
class FakeUpdateFeed final : public UpdateFeed {
public:
	std::optional<UpdateManifest> manifest;
	InstallerDownload downloadResult = InstallerDownload::Ok;
	bool launchSucceeds = true;

	int downloadCount = 0;
	std::string launchedPath;

	int fetchCount = 0;

	void FetchManifest(std::function<void(std::optional<UpdateManifest>)> done) override
	{
		++fetchCount;
		done(manifest);
	}

	void DownloadInstaller(const std::string &, const std::string &, std::function<void(int)> progress,
			       std::function<void(InstallerDownload, std::string)> done) override
	{
		++downloadCount;
		if (!finishDownloads) {
			pendingDownload = std::move(done);
			return;
		}
		progress(100);
		done(downloadResult, downloadResult == InstallerDownload::Ok ? "/tmp/installer" : "");
	}

	int cancelCount = 0;
	bool finishDownloads = true; // false: downloads stay in progress until FinishDownload
	std::function<void(InstallerDownload, std::string)> pendingDownload;
	void CancelDownload() override { ++cancelCount; }
	void FinishDownload()
	{
		if (pendingDownload)
			std::exchange(pendingDownload, {})(downloadResult, "/tmp/installer");
	}

	bool LaunchInstaller(const std::string &path) override
	{
		launchedPath = path;
		return launchSucceeds;
	}
};

// LangLiveApi that answers right away with what the test set. Defaults to
// success.
class FakeLangLiveApi final : public LangLiveApi {
public:
	static StreamingInfo LandscapeInfo()
	{
		StreamingInfo info;
		info.orientation = core::Orientation::Landscape;
		return info;
	}

	ApiError sendCodeError;
	ApiError loginError;
	ApiError streamingError; // for every call that needs the token
	std::string token = "fake-token";
	std::string nickname = "Streamer";
	StreamingInfo info = LandscapeInfo(); // matches ValidState of a landscape spec
	StreamTarget target{"rtmp://ingest.example.com/live", "fake-key", "live-1"};
	std::string lastCloseLiveId;
	ApiError closeError; // streaming/close only, when set; else streamingError

	std::vector<std::string> calls; // names of the calls made, in order
	std::string lastToken;
	std::string lastCellphone;
	StartStreamingRequest lastStart;
	StreamingSettings lastSettings;

	void SendSmsCode(const std::string &cellphone, std::function<void(ApiError)> done) override
	{
		calls.push_back("send_code");
		lastCellphone = cellphone;
		done(sendCodeError);
	}
	void LoginWithSmsCode(const std::string &cellphone, const std::string &,
			      std::function<void(ApiError, std::string, std::string)> done) override
	{
		calls.push_back("login");
		lastCellphone = cellphone;
		done(loginError, loginError.ok() ? token : "", loginError.ok() ? nickname : "");
	}
	// While set, streaming/info answers only when the test calls
	// AnswerHeldInfo, with what is set then: for replies that overlap.
	bool holdInfo = false;
	std::vector<std::function<void()>> heldInfo;
	void AnswerHeldInfo()
	{
		auto held = std::move(heldInfo);
		heldInfo.clear();
		for (auto &answer : held)
			answer();
	}
	void FetchStreamingInfo(const std::string &t, std::function<void(ApiError, StreamingInfo)> done) override
	{
		calls.push_back("info");
		lastToken = t;
		if (holdInfo) {
			heldInfo.push_back([this, done]() { done(streamingError, info); });
			return;
		}
		done(streamingError, info);
	}
	void SaveStreamingSettings(const std::string &t, const StreamingSettings &settings,
				   std::function<void(ApiError)> done) override
	{
		calls.push_back("setting");
		lastToken = t;
		lastSettings = settings;
		done(streamingError);
	}
	ApiError coverUploadError; // upload/image
	ApiError putError;         // the PUT to S3
	std::string lastUploadFilename;
	std::string coverImageUrl = "https://img.example.com/c.jpg";
	void RequestCoverUpload(const std::string &t, const std::string &filename,
				std::function<void(ApiError, CoverUpload)> done) override
	{
		calls.push_back("cover_upload_url");
		lastToken = t;
		lastUploadFilename = filename;
		done(streamingError.ok() ? coverUploadError : streamingError,
		     {"https://upload.example.com/c.jpg?sig", coverImageUrl, {{"Content-Type", "image/jpeg"}}});
	}
	// The bytes DownloadImage answers (e.g. a PNG a UI test made).
	std::string imageBytes;
	ApiError imageError;
	std::string lastImageUrl;
	void DownloadImage(const std::string &url, std::function<void(ApiError, std::string)> done) override
	{
		calls.push_back("image");
		lastImageUrl = url;
		done(imageError, imageError.ok() ? imageBytes : std::string());
	}
	void UploadCover(const CoverUpload &, const std::string &, std::function<void(ApiError)> done) override
	{
		calls.push_back("upload");
		done(putError);
	}
	ApiError startError; // streaming/start only
	void StartStreaming(const std::string &t, const StartStreamingRequest &request,
			    std::function<void(ApiError, StreamTarget)> done) override
	{
		calls.push_back("start");
		lastToken = t;
		lastStart = request;
		done(streamingError.ok() ? startError : streamingError, target);
	}
	void CloseStreaming(const std::string &t, const std::string &liveId,
			    std::function<void(ApiError)> done) override
	{
		calls.push_back("close");
		lastToken = t;
		lastCloseLiveId = liveId;
		done(closeError.ok() ? streamingError : closeError);
	}
	// qrcode/create, and the answers to qrcode/verify in order (the last
	// one repeats).
	ApiError qrCreateError;
	QrLoginCode qrCode{"uuid-1", "qr-token", "https://example.com/qrcode_auth?uuid=uuid-1"};
	std::vector<std::pair<ApiError, QrLoginState>> qrStates{{{}, {}}};
	size_t qrChecks = 0;
	QrLoginCode lastQrChecked;
	void CreateQrLogin(std::function<void(ApiError, QrLoginCode)> done) override
	{
		calls.push_back("qrcode/create");
		done(qrCreateError, qrCreateError.ok() ? qrCode : QrLoginCode{});
	}
	void CheckQrLogin(const QrLoginCode &code, std::function<void(ApiError, QrLoginState)> done) override
	{
		calls.push_back("qrcode/verify");
		lastQrChecked = code;
		const auto &answer = qrStates[std::min(qrChecks++, qrStates.size() - 1)];
		done(answer.first, answer.second);
	}

	ApiError logoutError;
	void Logout(const std::string &t, std::function<void(ApiError)> done) override
	{
		calls.push_back("logout");
		lastToken = t;
		done(logoutError);
	}
	ApiError CloseStreamingNow(const std::string &t, const std::string &liveId, int timeoutSeconds) override
	{
		calls.push_back("close_now");
		lastToken = t;
		lastCloseLiveId = liveId;
		lastCloseTimeoutSeconds = timeoutSeconds;
		return streamingError;
	}
	int lastCloseTimeoutSeconds = 0;
};

// The controller's asynchronous login calls, for tests whose backend (the
// mock, or a fake) answers right away.
inline SmsCodeRequest RequestCode(PluginController &controller, const std::string &countryCode,
				  const std::string &phoneNumber)
{
	SmsCodeRequest result;
	result.result = SmsCodeRequest::Result::Failed;
	result.message = "no answer";
	controller.RequestSmsCode(countryCode, phoneNumber, [&](SmsCodeRequest r) { result = std::move(r); });
	return result;
}

inline SmsLoginResponse::Result LogIn(PluginController &controller, const std::string &countryCode = "+886",
				      const std::string &phoneNumber = "912345678", const std::string &code = "123456")
{
	SmsLoginResponse response;
	response.message = "no answer";
	controller.LoginWithSmsCode(countryCode, phoneNumber, code,
				    [&](SmsLoginResponse r) { response = std::move(r); });
	return response.result;
}

// StartLive / SaveSettings for tests whose backend answers right away.
inline StartLiveResult StartLive(PluginController &controller, const GoLiveRequest &request)
{
	StartLiveResult result;
	result.error = {ApiError::Kind::Other, {}, "no answer"};
	controller.StartLive(request, [&](StartLiveResult r) { result = std::move(r); });
	return result;
}

inline SaveSettingsResult Save(PluginController &controller, const GoLiveRequest &request)
{
	SaveSettingsResult result;
	result.error = {ApiError::Kind::Other, {}, "no answer"};
	controller.SaveSettings(request, [&](SaveSettingsResult r) { result = std::move(r); });
	return result;
}

// Stream state that passes the pre-stream check for the given spec.
inline core::StreamState ValidState(const core::StreamSpec &spec)
{
	core::StreamState state;
	state.outputWidth = spec.width;
	state.outputHeight = spec.height;
	state.fps = spec.fps;
	state.videoCodec = core::kVideoCodec;
	state.audioCodec = core::kAudioCodec;
	state.keyframeIntervalSeconds = core::kKeyframeIntervalSeconds;
	state.bitrateKbps = spec.bitrateKbps;
	return state;
}

} // namespace langlive::obsplugin::testing
