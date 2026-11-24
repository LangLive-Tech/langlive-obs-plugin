#pragma once

#include "app/langlive-api.hpp"
#include "app/mock-langlive-api.hpp"
#include "app/ports.hpp"

#include "langlive/obsplugin/core/validation.hpp"
#include "langlive/obsplugin/core/website.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace langlive::obsplugin {

struct SmsCodeRequest {
	enum class Result {
		Sent,
		InvalidPhoneNumber,
		DailyLimitReached, // this number already received today's codes; nothing was sent
		TooFrequent,       // requested again too soon; nothing was sent
		UpdateRequired,    // the plugin is below the minimum supported version
		NetworkError,      // the backend could not be reached
		Failed,            // the backend refused; `message` says why
	};
	Result result = Result::InvalidPhoneNumber;
	int resendAfterSeconds = 0; // when Sent: wait before requesting again
	std::string message;        // the backend's message, for Failed
};

struct SmsLoginResponse {
	enum class Result {
		Success,
		NotEligible,    // the code was right, but the account may not stream from OBS
		UpdateRequired, // the plugin is below the minimum supported version
		NetworkError,   // the backend could not be reached
		Failed,         // e.g. a wrong code; `message` is the backend's reason
	};
	Result result = Result::Failed;
	std::string message;
};

// Result of checking the update manifest.
struct UpdateCheck {
	enum class Result {
		UpToDate,
		Available,
		Required,
		Failed, // no manifest URL, network error, or an invalid manifest
	};
	Result result = Result::Failed;
	std::string latestVersion;
};

// Where a QR code login stands, for the login dialog.
struct QrLoginUpdate {
	enum class Result {
		Waiting,        // shown, not confirmed yet
		Scanned,        // the app scanned it; the streamer confirms in the app
		LoggedIn,       // the login is complete and saved
		Expired,        // expired or used: a new code is needed
		NotEligible,    // confirmed, but the account may not stream from OBS
		UpdateRequired, // the plugin is below the minimum supported version
		NetworkError,   // the backend could not be reached
		Failed,         // the backend refused; `message` says why
	};
	Result result = Result::Failed;
	std::string content;      // StartQrLogin: what the QR code holds
	int expiresInSeconds = 0; // StartQrLogin: the app scans it before this
	std::string message;      // Failed: the backend's reason
};

// How the current stream was started. Decides what the plugin UI allows
// (go-live dialog locked while streaming through the plugin; go-live
// disabled while streaming with a stream key from the website).
enum class LiveSource {
	None,
	Plugin,  // started from the plugin's go-live dialog
	External // started with OBS's own "Start Streaming"
};

// Required go-live fields that are missing, in the order they are listed.
enum class GoLiveFieldError {
	MissingTitle,
	MissingChannel,
};

// Outcome of StartLive. Started when all are empty / ok.
struct StartLiveResult {
	std::vector<GoLiveFieldError> missingFields; // nothing was sent
	std::vector<core::Violation> violations;     // the pre-stream check failed; nothing was sent
	ApiError error;                              // the backend refused or could not be reached
	// OBS could not start the stream (e.g. its encoder failed to start) and
	// has shown its own error; the live was closed again.
	bool obsStartFailed = false;

	bool started() const { return missingFields.empty() && violations.empty() && error.ok() && !obsStartFailed; }
};

// Outcome of SaveSettings.
struct SaveSettingsResult {
	bool applied = false; // OBS accepted the stream settings
	ApiError error;       // saving title, push notification, and channel to the backend
};

// Application state and use cases. The UI sends intents here and never
// calls the OBS API directly; OBS frontend events are forwarded here by
// plugin-main.
class PluginController {
public:
	// Restores the login saved by the last session, if any.
	PluginController(StreamingHost &host, SettingsStore &store);

	// The LangLive backend. Until set, a built-in MockLangLiveApi is used.
	// `api` must outlive its use here.
	void SetApi(LangLiveApi *api) { api_ = api ? api : &mockApi_; }

	// --- Session -----------------------------------------------------

	// Phone number login with a one-time code sent by SMS. `done` runs once
	// the backend answers (right away for invalid input or the mock).
	void RequestSmsCode(const std::string &countryCode, const std::string &phoneNumber,
			    std::function<void(SmsCodeRequest)> done);
	// The backend checks the code, then whether the account may stream from
	// OBS. On success the login is saved for the next OBS session.
	void LoginWithSmsCode(const std::string &countryCode, const std::string &phoneNumber, const std::string &code,
			      std::function<void(SmsLoginResponse)> done);

	// Starts a QR code login with a new code (qrcode/create), replacing any
	// earlier one: Waiting with its content, or why there is none.
	void StartQrLogin(std::function<void(QrLoginUpdate)> done);
	// Checks the current code (qrcode/verify); the UI polls this every 2
	// seconds while the code is shown. On LoggedIn the login is saved as
	// for the SMS login.
	void CheckQrLogin(std::function<void(QrLoginUpdate)> done);

	// Logs out: forgets the saved login and revokes the token on the backend
	// (POST logout; locally the streamer is logged out even if that fails,
	// and the token then expires by itself). The UI offers it only when
	// CanLogout(); if
	// called during a live started from the plugin anyway, that live is
	// ended first: the stream stops and the live is closed on the backend.
	// Streams started with OBS's own Start Streaming are left alone.
	void Logout();
	// A live started from the plugin is starting or on. The UI disables
	// logout until it ends (PM decision): the streamer ends the live first.
	bool LogoutEndsLive() const { return liveSource_ == LiveSource::Plugin || backendLiveOpen_ || startingLive_; }
	bool CanLogout() const { return loggedIn_ && !LogoutEndsLive(); }

	// A plugin live ended because the stream stopped outside the plugin
	// (OBS's Stop Streaming, a hotkey, OBS giving up reconnecting): the UI
	// tells the streamer the live has ended. Lives are started and ended
	// only through the plugin, so the stop cannot be undone. Called from
	// OnStreamingStopped, inside OBS's event callback: do not block in it.
	void SetLiveEndedOutsideCallback(std::function<void()> callback) { liveEndedOutside_ = std::move(callback); }

	// Called after a backend call ended the login (ApiError::endsLogin: the
	// plugin has logged out by then), with the reason, so the UI can tell
	// the streamer. Queue any dialog: this runs inside the controller's
	// handling of a reply.
	void SetLoginLostCallback(std::function<void(ApiError::Kind)> callback) { loginLost_ = std::move(callback); }

	// The LangLive website (empty: production) and OBS's language, for links
	// to its pages.
	void SetWebsite(std::string url, std::string locale)
	{
		websiteUrl_ = std::move(url);
		locale_ = std::move(locale);
	}
	// A page of the LangLive website (terms, privacy, help center) in the
	// closest language.
	std::string WebsitePageUrl(core::WebsitePage page) const
	{
		return core::WebsitePageUrl(websiteUrl_, page, locale_);
	}

	bool IsLoggedIn() const { return loggedIn_; }
	const std::string &Nickname() const { return nickname_; }

	// The account's stream orientation, set by LangLive operations. Mock
	// until the API exists: always landscape.
	core::Orientation AccountOrientation() const { return orientation_; }

	// --- Go live -----------------------------------------------------

	GoLiveSettings Settings() const { return settings_; }
	// Saves the form: cover and scenario locally, title and push notification
	// to the backend (streaming/setting, which also requires a channel: the
	// one chosen, or else the backend's own, sent back unchanged), uploads a
	// new cover (streaming/setting has no cover: streaming/start gets it), and applies the
	// recommended stream settings for the scenario and the account's
	// orientation (spec 4.6). The local part is saved even if OBS or the
	// backend refuses.
	void SaveSettings(const GoLiveRequest &request, std::function<void(SaveSettingsResult)> done);
	std::string CoverImagePath() const { return store_.CoverImagePath(); }

	// Uploads a new cover now (upload/image, then the PUT to S3), from the
	// cropped JPEG at `path`, and reports where the backend has it. Each
	// call counts against the backend's daily limit (20), which it reports
	// as an error with its own message. UseUploadedCover then makes it the
	// cover.
	void UploadCover(const std::string &path, std::function<void(ApiError, std::string url)> done);
	// Makes the cover uploaded to `url` the one the next live starts with,
	// until the room's cover changes (see GoLiveSettings), and saves it.
	void UseUploadedCover(const std::string &url);
	// The image at `url` (GoLiveInfo::coverUrl) to show; empty bytes if it
	// cannot be downloaded.
	void LoadCoverImage(const std::string &url, std::function<void(std::string bytes)> done);

	// Choices and saved values for the form, and the account's orientation
	// (streaming/info). A live the backend still has open from the plugin
	// while this OBS streams none is closed first (CloseStaleLive).
	void LoadGoLiveInfo(std::function<void(ApiError, GoLiveInfo)> done);

	// Missing required fields, empty if the request may be submitted.
	std::vector<GoLiveFieldError> ValidateGoLive(const GoLiveRequest &request) const;

	LiveSource CurrentLiveSource() const { return liveSource_; }

	// The stream dropped and OBS is reconnecting. The live goes on; it
	// ends only if OBS gives up (OnStreamingStopped).
	bool IsReconnecting() const { return reconnecting_; }
	bool CanOpenGoLive() const { return loggedIn_ && liveSource_ != LiveSource::External; }

	// If the request is complete, no live from the website is running
	// (streaming/info; else WebLiveActive, before the pre-stream check), and
	// OBS's settings pass the pre-stream check: saves the form like SaveSettings (streaming/setting; the
	// backend does not keep what streaming/start gets), uploads the cover if
	// this login has not (see GoLiveSettings), opens the live on the backend (streaming/start), points OBS at the
	// returned ingest, and starts the stream. Otherwise reports the missing fields or the violations and
	// does nothing; for violations, ApplySettingsAndStart retries. The live
	// counts as started once OBS reports it (OnStreamingStarted).
	void StartLive(const GoLiveRequest &request, std::function<void(StartLiveResult)> done);
	bool IsStartingLive() const { return startingLive_; }

	// Stops the stream. The live itself ends in OnStreamingStopped, so
	// stopping from OBS ends it the same way.
	void EndLive();

	// The scenario matching the current output resolution, falling back to
	// the saved one when the resolution matches none.
	core::Scenario CurrentScenario() const;

	// Applies the recommended settings for a scenario and the account's
	// orientation.
	bool ApplyRecommendedSettings(core::Scenario scenario);

	// The pre-stream check's "Apply settings and go live": applies the
	// recommended settings, then starts the live the check stopped again
	// with the same request (StartLive). Returns false, without starting, if
	// OBS rejected the settings or there is no such live; otherwise `done`
	// gets the start's result.
	bool ApplySettingsAndStart(core::Scenario scenario, std::function<void(StartLiveResult)> done);

	// --- OBS frontend events -----------------------------------------

	// OBS is about to start a stream (its encoder is not running yet). For
	// lives started from the go-live dialog, the stream gets encoders matching the
	// profile's codecs if the plugin changed them (see
	// StreamingHost::UseProfileCodecs), and in OBS's simple output mode
	// (which has no keyframe interval setting) the interval is set to the
	// spec's 2 seconds for that stream. In advanced mode the streamer's
	// setting is left to the pre-stream check, whose Apply fixes it in the
	// profile.
	void OnStreamingStarting();

	// Runs the pre-stream check for lives started from the go-live dialog.
	// Returns the violations that stopped the stream; empty if the stream
	// may continue. Streams started with OBS's own Start Streaming are never
	// checked: the plugin cannot tell whether they go to LangLive.
	std::vector<core::Violation> OnStreamingStarted();

	// OBS could not start a stream the plugin asked for and showed its own
	// error: it sends no STREAMING_STOPPED for this, so without this call
	// the live would stay open and the plugin stuck in "starting". Ends it
	// like OnStreamingStopped (live closed, stream key cleared). Does
	// nothing unless the plugin is waiting for its stream to start.
	void OnStreamStartFailed();

	// OBS lost the stream connection and is retrying / got it back.
	void OnStreamReconnecting();
	void OnStreamReconnected();

	// Ends a live started from the plugin, however the stream stopped:
	// the go-live dialog's End Live, OBS's own Stop Streaming, OBS giving
	// up reconnecting, or the pre-stream check. Closes it on the backend
	// (streaming/close) and clears the stream key the backend gave. When
	// the stop did not come from the plugin, runs the live-ended-outside
	// callback. Returns true if a plugin live was ended.
	bool OnStreamingStopped();

	// OBS is exiting: a plugin live still open on the backend, or one whose
	// close is still on its way, is closed now, waiting a few seconds at
	// most, since requests in flight end with the plugin. If that fails the
	// live stays recorded for RecoverUnclosedLive.
	void OnExit();

	// Closes the live a previous OBS session left open (a crash, a forced
	// quit, or a failed close): the live_id recorded when it opened, if the
	// backend still has it open from the plugin. Called when OBS has loaded
	// and after logging in.
	void RecoverUnclosedLive();

	// When OBS has loaded: a login restored from the last session is checked
	// with the backend (streaming/info), so a token that is no longer valid
	// logs out now rather than at the next Go Live; the same call recovers
	// a live left open (RecoverUnclosedLive). A network failure keeps the
	// login.
	void CheckSavedLogin();

	// --- Updates -------------------------------------------------------

	// Enables update checks. Without a feed, CheckForUpdates reports Failed.
	void SetUpdateFeed(UpdateFeed *feed, std::string currentVersion);
	const std::string &CurrentVersion() const { return currentVersion_; }

	// Reads the manifest. A Required result blocks login until updated.
	void CheckForUpdates(std::function<void(UpdateCheck)> done);
	bool UpdateRequired() const { return updateRequired_; }

	// Downloads the installer from the last successful check and verifies
	// it. Not allowed while live (`done` gets Failed). The installer is
	// ready when `done` gets Ok.
	bool CanInstallUpdate() const { return pendingUpdate_.has_value() && liveSource_ == LiveSource::None; }
	void DownloadUpdate(std::function<void(int percent)> progress, std::function<void(InstallerDownload)> done);
	// The streamer closed the update dialog (X): stops the download, e.g.
	// one stuck on a bad connection.
	void CancelUpdateDownload();

	// Opens the downloaded installer. The caller then quits OBS, because
	// the installer refuses to run while OBS is open.
	bool LaunchUpdateInstaller();

	// --- Notifications -------------------------------------------------

	// Called after any change to login or live state.
	void SetStateChangedCallback(std::function<void()> callback);

	// Receives what the controller decides (pre-stream check results, live
	// ids), for the OBS log. Never given tokens, stream keys or phone numbers.
	enum class LogLevel { Info, Warning };
	void SetLogCallback(std::function<void(LogLevel, const std::string &)> callback) { log_ = std::move(callback); }

private:
	std::string OrientationName() const
	{
		return orientation_ == core::Orientation::Portrait ? "portrait" : "landscape";
	}
	void Log(LogLevel level, const std::string &message) const
	{
		if (log_)
			log_(level, message);
	}
	void CompleteLogin(const std::string &token, const std::string &nickname);
	// Forgets the login without touching a live: for an expired login, whose
	// live keeps streaming and is closed after the next login
	// (RecoverUnclosedLive).
	void ForgetLogin();
	// A live streaming/info reports open from the plugin while this OBS has
	// none open or closing: left by a close that could not be sent (no
	// network when the stream stopped) or by a crash, and it blocks
	// streaming/start until the backend closes it for lack of stream. It is
	// closed without asking (PM's decision): another computer streaming from
	// the plugin on the same account is rare.
	bool IsStalePluginLive(const StreamingInfo &info) const;
	void CloseStaleLive(const std::string &liveId, std::function<void(ApiError)> done);
	// After streaming/close for `liveId`: forgets it unless it could not be
	// sent or the login expired, so the next session tries again.
	void FinishClose(const std::string &liveId, const ApiError &error);
	// Logs out if `error` says the login expired. Returns true if it did.
	bool HandleSessionError(const ApiError &error);
	void NotifyStateChanged();

	// Keeps the form's values; the cover stays as UseUploadedCover set it.
	void StoreForm(const GoLiveRequest &request);
	// StartLive once streaming/info showed no live from the website: the
	// pre-stream check, then the backend and OBS.
	void StartCheckedLive(const GoLiveRequest &request, std::function<void(StartLiveResult)> done);
	// The cover the next live starts with (GoLiveInfo::coverUrl).
	std::string EffectiveCoverUrl() const;
	void StartWithCover(const GoLiveRequest &request, std::string coverUrl,
			    std::function<void(StartLiveResult)> done);
	// POST logout for a token already forgotten locally; only logged.
	void RevokeToken(const std::string &token);
	void FinishStart(ApiError error, std::function<void(StartLiveResult)> done, bool obsStartFailed = false);

	MockLangLiveApi mockApi_;
	LangLiveApi *api_ = &mockApi_;
	std::string token_;
	std::string serverCoverUrl_;    // the room's cover on the backend (streaming/info)
	bool serverCoverKnown_ = false; // streaming/info answered since OBS started
	// The channel the backend keeps (streaming/info). Never shown; only sent
	// back by Save when none is chosen, since streaming/setting requires one.
	std::string serverChannelId_;
	bool startingLive_ = false;    // StartLive is waiting for the backend
	bool backendLiveOpen_ = false; // streaming/start succeeded; close it when the stream stops
	std::string backendLiveId_;    // that live's id, for streaming/close
	std::string closingLiveId_;    // streaming/close sent, no answer yet
	std::function<void(ApiError::Kind)> loginLost_;
	std::function<void()> liveEndedOutside_;
	bool endRequestedByPlugin_ = false; // EndLive or Logout stopped the stream
	bool streamKeySet_ = false;         // OBS holds the backend's stream key
	std::function<void(LogLevel, const std::string &)> log_;

	StreamingHost &host_;
	SettingsStore &store_;

	bool loggedIn_ = false;
	std::string nickname_;
	std::string websiteUrl_;
	std::string locale_;
	core::Orientation orientation_ = core::Orientation::Landscape;
	GoLiveSettings settings_;

	LiveSource liveSource_ = LiveSource::None;
	bool reconnecting_ = false;
	bool startRequestedByPlugin_ = false;
	std::optional<GoLiveRequest> lastLiveRequest_; // the plugin live started last
	// The live the pre-stream check stopped, to start again with this request.
	std::optional<GoLiveRequest> retryLive_;

	UpdateFeed *updateFeed_ = nullptr;
	std::string currentVersion_;
	bool updateRequired_ = false;
	std::optional<QrLoginCode> qrCode_;           // the QR code being shown
	int downloadGeneration_ = 0;                  // a newer or canceled download makes older results stale
	std::optional<UpdateManifest> pendingUpdate_; // newer version from the last check
	std::string downloadedInstaller_;

	std::function<void()> stateChanged_;
};

} // namespace langlive::obsplugin
