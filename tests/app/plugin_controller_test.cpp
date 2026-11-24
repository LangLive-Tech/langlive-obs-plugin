#include "app/plugin-controller.hpp"

#include "fakes.hpp"

#include <doctest/doctest.h>

#include <algorithm>

using namespace langlive::obsplugin;
using namespace langlive::obsplugin::testing;
using core::Orientation;
using core::Scenario;
using core::Violation;

namespace {

const core::StreamSpec kGeneralLandscape = core::GetStreamSpec(Scenario::General, Orientation::Landscape);

struct Fixture {
	FakeStreamingHost host;
	FakeSettingsStore store;
	PluginController controller{host, store};
	int stateChanges = 0;

	Fixture()
	{
		host.state = ValidState(kGeneralLandscape);
		controller.SetStateChangedCallback([this]() { ++stateChanges; });
	}

	void LogIn() { REQUIRE(testing::LogIn(controller) == SmsLoginResponse::Result::Success); }

	static GoLiveRequest CompleteRequest()
	{
		GoLiveRequest request;
		request.settings.title = "Tonight";
		request.channelId = "channel-1";
		return request;
	}
};

} // namespace

// --- Session -----------------------------------------------------------

TEST_CASE_FIXTURE(Fixture, "SMS code requests need a valid phone number")
{
	for (const char *number : {"", "12345", "0912-345-678", "1234567890123456"}) {
		CHECK_MESSAGE(RequestCode(controller, "+886", number).result ==
				      SmsCodeRequest::Result::InvalidPhoneNumber,
			      number);
	}

	const SmsCodeRequest request = RequestCode(controller, "+886", "912345678");
	CHECK(request.result == SmsCodeRequest::Result::Sent);
	CHECK(request.resendAfterSeconds > 0);
}

TEST_CASE_FIXTURE(Fixture, "the mock backend allows 10 SMS codes per phone number per day")
{
	for (int i = 0; i < 10; ++i)
		REQUIRE(RequestCode(controller, "+886", "912345678").result == SmsCodeRequest::Result::Sent);

	const SmsCodeRequest limited = RequestCode(controller, "+886", "912345678");
	CHECK(limited.result == SmsCodeRequest::Result::DailyLimitReached);
	CHECK(limited.resendAfterSeconds == 0);
	CHECK(RequestCode(controller, "+886", "987654321").result == SmsCodeRequest::Result::Sent);
}

TEST_CASE_FIXTURE(Fixture, "SMS code errors from the backend")
{
	FakeLangLiveApi api;
	controller.SetApi(&api);

	CHECK(RequestCode(controller, "+886", "912345678").result == SmsCodeRequest::Result::Sent);
	CHECK(api.lastCellphone == "886912345678");

	api.sendCodeError = {ApiError::Kind::TooFrequent, "99300", "slow down"};
	CHECK(RequestCode(controller, "+886", "912345678").result == SmsCodeRequest::Result::TooFrequent);
	api.sendCodeError = {ApiError::Kind::SmsLimitReached, "99301", "too many"};
	CHECK(RequestCode(controller, "+886", "912345678").result == SmsCodeRequest::Result::DailyLimitReached);
	api.sendCodeError = {ApiError::Kind::Network, "", "timeout"};
	CHECK(RequestCode(controller, "+886", "912345678").result == SmsCodeRequest::Result::NetworkError);
	api.sendCodeError = {ApiError::Kind::Other, "12345", "nope"};
	const SmsCodeRequest failed = RequestCode(controller, "+886", "912345678");
	CHECK(failed.result == SmsCodeRequest::Result::Failed);
	CHECK(failed.message == "nope");
}

TEST_CASE_FIXTURE(Fixture, "mock SMS login always succeeds, even with empty fields")
{
	CHECK(testing::LogIn(controller, "+886", "", "") == SmsLoginResponse::Result::Success);
	CHECK(controller.IsLoggedIn());
	CHECK_FALSE(controller.Nickname().empty());
	CHECK(controller.AccountOrientation() == Orientation::Landscape);
	CHECK(stateChanges == 1);
}

TEST_CASE_FIXTURE(Fixture, "SMS login errors from the backend leave the plugin logged out")
{
	FakeLangLiveApi api;
	controller.SetApi(&api);

	api.loginError = {ApiError::Kind::NotEligible, "99302", "no permission"};
	CHECK(testing::LogIn(controller) == SmsLoginResponse::Result::NotEligible);
	api.loginError = {ApiError::Kind::Network, "", "timeout"};
	CHECK(testing::LogIn(controller) == SmsLoginResponse::Result::NetworkError);
	api.loginError = {ApiError::Kind::Other, "1", "wrong code"};
	CHECK(testing::LogIn(controller) == SmsLoginResponse::Result::Failed);

	CHECK_FALSE(controller.IsLoggedIn());
	CHECK_FALSE(store.session.has_value());
	CHECK(stateChanges == 0);
}

namespace {

using QrResult = QrLoginUpdate::Result;

QrLoginUpdate StartQr(PluginController &controller)
{
	std::optional<QrLoginUpdate> result;
	controller.StartQrLogin([&](QrLoginUpdate update) { result = std::move(update); });
	REQUIRE(result.has_value());
	return *result;
}

QrLoginUpdate CheckQr(PluginController &controller)
{
	std::optional<QrLoginUpdate> result;
	controller.CheckQrLogin([&](QrLoginUpdate update) { result = std::move(update); });
	REQUIRE(result.has_value());
	return *result;
}

QrLoginState QrState(QrLoginState::Status status, std::string token = {}, std::string nickname = {})
{
	return {status, std::move(token), std::move(nickname)};
}

} // namespace

TEST_CASE_FIXTURE(Fixture, "QR login: shown, scanned, then confirmed logs in like the SMS login")
{
	FakeLangLiveApi api;
	controller.SetApi(&api);
	// As the backend answered with the app: 1802 until the app scans, 1801
	// once, 1802 again until the app confirms, then 0 (and 0 again if asked
	// again).
	api.qrStates = {{{}, QrState(QrLoginState::Status::Waiting)},
			{{}, QrState(QrLoginState::Status::Scanned)},
			{{}, QrState(QrLoginState::Status::Waiting)},
			{{}, QrState(QrLoginState::Status::LoggedIn, "qr-login-token", "Streamer")}};

	const QrLoginUpdate shown = StartQr(controller);
	CHECK(shown.result == QrResult::Waiting);
	CHECK(shown.content == "https://example.com/qrcode_auth?uuid=uuid-1");
	CHECK(shown.expiresInSeconds == 300);

	CHECK(CheckQr(controller).result == QrResult::Waiting);
	CHECK(api.lastQrChecked.uuid == "uuid-1");
	CHECK(api.lastQrChecked.token == "qr-token");
	CHECK(CheckQr(controller).result == QrResult::Scanned);
	CHECK(CheckQr(controller).result == QrResult::Waiting); // waiting for the confirmation
	CHECK_FALSE(controller.IsLoggedIn());
	CHECK(CheckQr(controller).result == QrResult::LoggedIn);
	CHECK(controller.IsLoggedIn());
	CHECK(controller.Nickname() == "Streamer");
	REQUIRE(store.session.has_value());
	CHECK(store.session->token == "qr-login-token");

	// Used up: no more checks go out, although the backend would answer
	// the login again.
	const size_t checks = api.qrChecks;
	CHECK(CheckQr(controller).result == QrResult::Expired);
	CHECK(api.qrChecks == checks);
	controller.SetApi(nullptr);
}

TEST_CASE_FIXTURE(Fixture, "QR login: an expired or refused code needs a new one, a network error does not")
{
	FakeLangLiveApi api;
	controller.SetApi(&api);

	api.qrStates = {{{ApiError::Kind::Network, "", "timeout"}, {}}, {{}, QrState(QrLoginState::Status::Expired)}};
	StartQr(controller);
	CHECK(CheckQr(controller).result == QrResult::NetworkError);
	CHECK(CheckQr(controller).result == QrResult::Expired); // the code was kept after the network error
	const size_t checks = api.qrChecks;
	CHECK(CheckQr(controller).result == QrResult::Expired);
	CHECK(api.qrChecks == checks);

	api.qrChecks = 0;
	api.qrStates = {{{ApiError::Kind::NotEligible, "99302", "not eligible"}, {}}};
	StartQr(controller);
	CHECK(CheckQr(controller).result == QrResult::NotEligible);
	CHECK_FALSE(controller.IsLoggedIn());

	api.qrChecks = 0;
	api.qrStates = {{{ApiError::Kind::Other, "609", "Account deleted"}, {}}};
	StartQr(controller);
	const QrLoginUpdate refused = CheckQr(controller);
	CHECK(refused.result == QrResult::Failed);
	CHECK(refused.message == "Account deleted");
	CHECK(CheckQr(controller).result == QrResult::Expired);
	CHECK_FALSE(controller.IsLoggedIn());

	// No code at all.
	api.qrCreateError = {ApiError::Kind::Network, "", "timeout"};
	CHECK(StartQr(controller).result == QrResult::NetworkError);
	CHECK(CheckQr(controller).result == QrResult::Expired);
	controller.SetApi(nullptr);
}

TEST_CASE_FIXTURE(Fixture, "QR login: the built-in mock confirms after a few checks")
{
	const QrLoginUpdate shown = StartQr(controller);
	REQUIRE(shown.result == QrResult::Waiting);
	CHECK_FALSE(shown.content.empty());
	CHECK(StartQr(controller).content != shown.content);

	std::vector<QrResult> results;
	for (int i = 0; i < 5; ++i)
		results.push_back(CheckQr(controller).result);
	CHECK(results == std::vector<QrResult>{QrResult::Waiting, QrResult::Waiting, QrResult::Scanned,
					       QrResult::Waiting, QrResult::LoggedIn});
	CHECK(controller.IsLoggedIn());
}

TEST_CASE_FIXTURE(Fixture, "login is saved for the next OBS session, logout forgets it")
{
	FakeLangLiveApi api;
	controller.SetApi(&api);

	LogIn();
	CHECK(controller.IsLoggedIn());
	CHECK(controller.Nickname() == "Streamer");
	CHECK(stateChanges == 1);
	REQUIRE(store.session.has_value());
	CHECK(store.session->token == "fake-token");

	{
		PluginController restarted{host, store};
		CHECK(restarted.IsLoggedIn());
		CHECK(restarted.Nickname() == "Streamer");
	}

	controller.Logout();
	CHECK_FALSE(controller.IsLoggedIn());
	CHECK(controller.Nickname().empty());
	CHECK_FALSE(store.session.has_value());
	CHECK(stateChanges == 2);
	// Revoked on the backend too.
	CHECK(api.calls.back() == "logout");
	CHECK(api.lastToken == "fake-token");

	PluginController restarted{host, store};
	CHECK_FALSE(restarted.IsLoggedIn());
}

// --- Settings ------------------------------------------------------------

TEST_CASE("saved settings are loaded at startup")
{
	FakeStreamingHost host;
	FakeSettingsStore store;
	store.stored.title = "Hello";
	store.stored.uploadedCoverUrl = "https://img.example.com/c.jpg";
	store.stored.scenario = Scenario::HighQuality;

	PluginController controller(host, store);

	CHECK(controller.Settings().title == "Hello");
	CHECK(controller.Settings().uploadedCoverUrl == "https://img.example.com/c.jpg");
	CHECK(controller.Settings().scenario == Scenario::HighQuality);
}

TEST_CASE_FIXTURE(Fixture, "SaveSettings persists and applies the scenario's settings")
{
	LogIn();
	GoLiveRequest request;
	request.settings.title = "Tonight";
	request.settings.scenario = Scenario::HighQuality;

	const SaveSettingsResult result = Save(controller, request);
	CHECK(result.applied);
	CHECK(result.error.ok());

	CHECK(store.saveCount == 1);
	CHECK(store.stored.title == "Tonight");
	CHECK(store.stored.scenario == Scenario::HighQuality);
	REQUIRE(host.appliedSpec.has_value());
	CHECK(host.appliedSpec->width == 1920);
	CHECK(host.appliedSpec->height == 1080);
	CHECK(host.appliedSpec->fps == 30);
	CHECK(host.appliedSpec->bitrateKbps == 6000);
}

TEST_CASE_FIXTURE(Fixture, "SaveSettings saves the form even when OBS rejects the settings")
{
	host.applySucceeds = false;
	GoLiveRequest request;
	request.settings.title = "Tonight";

	CHECK_FALSE(Save(controller, request).applied);
	CHECK(store.stored.title == "Tonight");
}

// --- Pre-stream check ------------------------------------------------------

TEST_CASE_FIXTURE(Fixture, "logged out: no check, even with invalid settings")
{
	host.state.keyframeIntervalSeconds = 0;

	CHECK(controller.OnStreamingStarted().empty());
	CHECK(host.stopCount == 0);
	CHECK(controller.CurrentLiveSource() == LiveSource::External);
}

TEST_CASE_FIXTURE(Fixture, "OBS's own Start Streaming: no check and no changes, whatever the server")
{
	LogIn();
	host.state.videoCodec = "hevc";
	host.state.keyframeIntervalSeconds = 0;

	controller.OnStreamingStarting();
	CHECK(host.profileCodecsCount == 0);
	CHECK(host.keyframeFixCount == 0);
	CHECK(controller.OnStreamingStarted().empty());
	CHECK(host.stopCount == 0);
	CHECK(controller.CurrentLiveSource() == LiveSource::External);
}

TEST_CASE_FIXTURE(Fixture, "a live from the go-live dialog is checked on any server")
{
	LogIn();
	host.serverUrl = "rtmp://127.0.0.1/live";
	host.state.keyframeIntervalSeconds = 0;
	REQUIRE(testing::StartLive(controller, CompleteRequest()).started());

	CHECK(controller.OnStreamingStarted() == std::vector<Violation>{Violation::KeyframeIntervalZero});
	CHECK(host.stopCount == 1);
	CHECK(controller.CurrentLiveSource() == LiveSource::None);
}

TEST_CASE_FIXTURE(Fixture, "valid settings: the stream continues")
{
	LogIn();
	REQUIRE(testing::StartLive(controller, CompleteRequest()).started());

	CHECK(controller.OnStreamingStarted().empty());
	CHECK(host.stopCount == 0);
	CHECK(controller.CurrentLiveSource() == LiveSource::Plugin);
}

TEST_CASE_FIXTURE(Fixture, "invalid settings: the stream is stopped and every violation reported")
{
	LogIn();
	REQUIRE(testing::StartLive(controller, CompleteRequest()).started());
	host.state.audioCodec = "opus"; // OBS's encoders differ from the profile
	host.state.keyframeIntervalSeconds = 0;
	host.state.bitrateKbps = 8000;
	const int changesBefore = stateChanges;

	const auto violations = controller.OnStreamingStarted();

	CHECK(violations == std::vector<Violation>{Violation::UnsupportedAudioCodec, Violation::KeyframeIntervalZero,
						   Violation::BitrateTooHigh720p});
	CHECK(host.stopCount == 1);
	CHECK(controller.CurrentLiveSource() == LiveSource::None);
	CHECK(stateChanges == changesBefore);
}

TEST_CASE_FIXTURE(Fixture, "the check uses the account orientation")
{
	LogIn();
	REQUIRE(testing::StartLive(controller, CompleteRequest()).started());
	host.state = ValidState(core::GetStreamSpec(Scenario::General, Orientation::Portrait));
	CHECK(controller.OnStreamingStarted() == std::vector<Violation>{Violation::LandscapeResolutionMismatch});
}

TEST_CASE_FIXTURE(Fixture, "an automatic keyframe interval (simple output mode) is set to 2 s while starting")
{
	LogIn();
	REQUIRE(testing::StartLive(controller, CompleteRequest()).started());
	host.state.keyframeIntervalSeconds = 0; // OBS simple output mode

	controller.OnStreamingStarting();
	CHECK(host.state.keyframeIntervalSeconds == 2);
	CHECK(controller.OnStreamingStarted().empty());
}

TEST_CASE_FIXTURE(Fixture, "advanced output mode: the streamer's keyframe interval is checked, not changed")
{
	LogIn();
	host.simpleOutputMode = false;
	REQUIRE(testing::StartLive(controller, CompleteRequest()).started());

	SUBCASE("too long")
	{
		host.state.keyframeIntervalSeconds = 5;
		controller.OnStreamingStarting();
		CHECK(host.state.keyframeIntervalSeconds == 5);
		CHECK(controller.OnStreamingStarted() == std::vector<Violation>{Violation::KeyframeIntervalTooLong});
	}
	SUBCASE("automatic")
	{
		host.state.keyframeIntervalSeconds = 0;
		controller.OnStreamingStarting();
		CHECK(host.state.keyframeIntervalSeconds == 0);
		CHECK(controller.OnStreamingStarted() == std::vector<Violation>{Violation::KeyframeIntervalZero});
	}
	CHECK(host.stopCount == 1);
}

TEST_CASE_FIXTURE(Fixture, "streams the check does not apply to are left alone")
{
	host.state.keyframeIntervalSeconds = 0;
	controller.OnStreamingStarting(); // logged out
	CHECK(host.keyframeFixCount == 0);

	LogIn();
	host.serverUrl = "rtmp://a.rtmp.youtube.com/live2";
	controller.OnStreamingStarting(); // another service, started from OBS
	CHECK(host.keyframeFixCount == 0);
	CHECK(host.state.keyframeIntervalSeconds == 0);
}

TEST_CASE_FIXTURE(Fixture, "a live from the go-live dialog gets the keyframe interval on any server")
{
	LogIn();
	host.serverUrl = "rtmp://127.0.0.1/live";
	host.state.keyframeIntervalSeconds = 0;
	REQUIRE(testing::StartLive(controller, CompleteRequest()).started());

	controller.OnStreamingStarting();
	CHECK(host.state.keyframeIntervalSeconds == 2);
	CHECK(controller.OnStreamingStarted().empty());
	CHECK(controller.CurrentLiveSource() == LiveSource::Plugin);
}

// --- Go live -----------------------------------------------------------------

TEST_CASE_FIXTURE(Fixture, "go-live requires a title and a channel")
{
	LogIn();
	CHECK(testing::StartLive(controller, GoLiveRequest{}).missingFields ==
	      std::vector<GoLiveFieldError>{GoLiveFieldError::MissingTitle, GoLiveFieldError::MissingChannel});

	GoLiveRequest request;
	request.channelId = "channel-1";
	CHECK(testing::StartLive(controller, request).missingFields ==
	      std::vector<GoLiveFieldError>{GoLiveFieldError::MissingTitle});

	request.settings.title = "Tonight";
	request.channelId.clear();
	CHECK(testing::StartLive(controller, request).missingFields ==
	      std::vector<GoLiveFieldError>{GoLiveFieldError::MissingChannel});

	CHECK(host.startCount == 0);
	CHECK(testing::StartLive(controller, CompleteRequest()).started());
	CHECK(host.startCount == 1);
}

TEST_CASE_FIXTURE(Fixture, "go-live info lists channels and events (mock backend)")
{
	LogIn();
	ApiError error{ApiError::Kind::Other, {}, "no answer"};
	GoLiveInfo info;
	controller.LoadGoLiveInfo([&](ApiError e, GoLiveInfo i) {
		error = e;
		info = std::move(i);
	});
	CHECK(error.ok());
	CHECK_FALSE(info.channels.empty());
	CHECK_FALSE(info.events.empty());
}

TEST_CASE_FIXTURE(Fixture, "StartLive requires login")
{
	CHECK_FALSE(testing::StartLive(controller, CompleteRequest()).started());
	CHECK(host.startCount == 0);
}

TEST_CASE_FIXTURE(Fixture, "a stream started from the plugin is a plugin stream")
{
	LogIn();
	testing::StartLive(controller, CompleteRequest());
	CHECK(host.startCount == 1);

	CHECK(controller.OnStreamingStarted().empty());
	CHECK(controller.CurrentLiveSource() == LiveSource::Plugin);
	CHECK(controller.CanOpenGoLive());

	controller.EndLive();
	CHECK(host.stopCount == 1);

	CHECK(controller.OnStreamingStopped());
	CHECK(controller.CurrentLiveSource() == LiveSource::None);
}

TEST_CASE_FIXTURE(Fixture, "stopping the stream in OBS ends a plugin live")
{
	LogIn();
	testing::StartLive(controller, CompleteRequest());
	REQUIRE(controller.OnStreamingStarted().empty());
	REQUIRE(controller.CurrentLiveSource() == LiveSource::Plugin);

	// OBS's Stop Streaming, not the plugin's End Live
	CHECK(controller.OnStreamingStopped());
	CHECK(host.stopCount == 0);
	CHECK(controller.CurrentLiveSource() == LiveSource::None);
	CHECK(controller.CanOpenGoLive());
}

TEST_CASE_FIXTURE(Fixture, "stopping a stream started from OBS ends no plugin live")
{
	LogIn();
	REQUIRE(controller.OnStreamingStarted().empty());
	CHECK_FALSE(controller.OnStreamingStopped());
}

TEST_CASE_FIXTURE(Fixture, "a stream started from OBS disables go-live and cannot be ended by the plugin")
{
	LogIn();
	CHECK(controller.OnStreamingStarted().empty());

	CHECK(controller.CurrentLiveSource() == LiveSource::External);
	CHECK_FALSE(controller.CanOpenGoLive());

	controller.EndLive();
	CHECK(host.stopCount == 0);

	testing::StartLive(controller, CompleteRequest());
	CHECK(host.startCount == 0);
}

TEST_CASE_FIXTURE(Fixture, "a plugin start that fails to connect does not mark the next stream as a plugin stream")
{
	LogIn();
	testing::StartLive(controller, CompleteRequest());
	controller.OnStreamingStopped(); // OBS reports stopped when the connection fails

	CHECK(controller.OnStreamingStarted().empty()); // later started from OBS
	CHECK(controller.CurrentLiveSource() == LiveSource::External);
}

TEST_CASE_FIXTURE(Fixture, "a plugin start blocked by the check leaves no live stream")
{
	LogIn();
	REQUIRE(testing::StartLive(controller, CompleteRequest()).started());
	host.state.videoCodec = "av1";

	CHECK(controller.OnStreamingStarted() == std::vector<Violation>{Violation::UnsupportedVideoCodec});
	CHECK(controller.CurrentLiveSource() == LiveSource::None);
	CHECK(controller.CanOpenGoLive());
}

TEST_CASE_FIXTURE(Fixture, "live state changes are notified")
{
	LogIn();
	const int afterLogin = stateChanges;

	controller.OnStreamingStarted();
	CHECK(stateChanges == afterLogin + 1);

	controller.OnStreamingStopped();
	CHECK(stateChanges == afterLogin + 2);

	controller.OnStreamingStopped(); // already stopped: no change
	CHECK(stateChanges == afterLogin + 2);
}

// --- Reconnecting -------------------------------------------------------------

TEST_CASE_FIXTURE(Fixture, "a dropped stream reconnects without ending the live")
{
	LogIn();
	REQUIRE(testing::StartLive(controller, CompleteRequest()).started());
	REQUIRE(controller.OnStreamingStarted().empty());
	const int before = stateChanges;

	controller.OnStreamReconnecting();
	CHECK(controller.IsReconnecting());
	CHECK(controller.CurrentLiveSource() == LiveSource::Plugin);
	controller.OnStreamReconnecting(); // OBS signals every retry
	CHECK(stateChanges == before + 1);

	controller.OnStreamReconnected();
	CHECK_FALSE(controller.IsReconnecting());
	CHECK(controller.CurrentLiveSource() == LiveSource::Plugin);
	CHECK(stateChanges == before + 2);
}

TEST_CASE_FIXTURE(Fixture, "giving up reconnecting ends the live")
{
	LogIn();
	REQUIRE(testing::StartLive(controller, CompleteRequest()).started());
	REQUIRE(controller.OnStreamingStarted().empty());
	controller.OnStreamReconnecting();

	CHECK(controller.OnStreamingStopped());
	CHECK_FALSE(controller.IsReconnecting());
	CHECK(controller.CurrentLiveSource() == LiveSource::None);
	CHECK(controller.CanOpenGoLive());
}

TEST_CASE_FIXTURE(Fixture, "reconnect signals without a live are ignored")
{
	controller.OnStreamReconnecting();
	CHECK_FALSE(controller.IsReconnecting());
	CHECK(stateChanges == 0);
}

// --- Recommended settings ----------------------------------------------------

TEST_CASE_FIXTURE(Fixture, "apply recommended settings for the scenario and account orientation")
{
	LogIn();

	CHECK(controller.ApplyRecommendedSettings(Scenario::HighQuality));

	REQUIRE(host.appliedSpec.has_value());
	CHECK(host.appliedSpec->width == 1920);
	CHECK(host.appliedSpec->height == 1080);
	CHECK(host.appliedSpec->fps == 30);
	CHECK(host.appliedSpec->bitrateKbps == 6000);
	CHECK(store.stored.scenario == Scenario::HighQuality);
}

TEST_CASE_FIXTURE(Fixture, "apply reports failure from OBS")
{
	host.applySucceeds = false;
	CHECK_FALSE(controller.ApplyRecommendedSettings(Scenario::General));
}

TEST_CASE_FIXTURE(Fixture, "CurrentScenario follows the output resolution")
{
	host.state = ValidState(core::GetStreamSpec(Scenario::HighQuality, Orientation::Landscape));
	CHECK(controller.CurrentScenario() == Scenario::HighQuality);

	SUBCASE("falls back to the saved scenario when the resolution matches none")
	{
		host.state.outputWidth = 2560;
		host.state.outputHeight = 1440;
		CHECK(controller.CurrentScenario() == Scenario::General);
	}
}

// --- Updates -------------------------------------------------------------------

namespace {

const std::string kSha256(64, 'a');

UpdateManifest Manifest(const char *version, const char *minSupported)
{
	UpdateManifest manifest;
	manifest.version = version;
	manifest.minSupportedVersion = minSupported;
	manifest.installerUrl = "https://download.example.com/plugin.pkg";
	manifest.installerSha256 = kSha256;
	return manifest;
}

struct UpdateFixture : Fixture {
	FakeUpdateFeed feed;

	UpdateFixture() { controller.SetUpdateFeed(&feed, "1.1.0"); }

	UpdateCheck Check()
	{
		std::optional<UpdateCheck> result;
		controller.CheckForUpdates([&](UpdateCheck check) { result = check; });
		REQUIRE(result.has_value());
		return *result;
	}
};

} // namespace

TEST_CASE_FIXTURE(UpdateFixture, "update check results")
{
	feed.manifest = Manifest("1.1.0", "1.0.0");
	CHECK(Check().result == UpdateCheck::Result::UpToDate);
	CHECK_FALSE(controller.CanInstallUpdate());

	feed.manifest = Manifest("1.2.0", "1.0.0");
	const UpdateCheck available = Check();
	CHECK(available.result == UpdateCheck::Result::Available);
	CHECK(available.latestVersion == "1.2.0");
	CHECK(controller.CanInstallUpdate());
	CHECK_FALSE(controller.UpdateRequired());

	feed.manifest = Manifest("2.0.0", "2.0.0");
	CHECK(Check().result == UpdateCheck::Result::Required);
	CHECK(controller.UpdateRequired());
}

TEST_CASE_FIXTURE(UpdateFixture, "a bad or missing manifest fails the check")
{
	CHECK(Check().result == UpdateCheck::Result::Failed); // network error

	UpdateManifest manifest = Manifest("1.2.0", "1.0.0");
	manifest.version = "latest";
	feed.manifest = manifest;
	CHECK(Check().result == UpdateCheck::Result::Failed);

	manifest = Manifest("1.2.0", "1.0.0");
	manifest.installerSha256 = "abc";
	feed.manifest = manifest;
	CHECK(Check().result == UpdateCheck::Result::Failed);

	manifest = Manifest("1.2.0", "1.0.0");
	manifest.installerUrl = "http://download.example.com/plugin.pkg"; // not HTTPS
	feed.manifest = manifest;
	CHECK(Check().result == UpdateCheck::Result::Failed);
	CHECK_FALSE(controller.CanInstallUpdate());
}

TEST_CASE("without an update feed the check fails")
{
	FakeStreamingHost host;
	FakeSettingsStore store;
	PluginController controller(host, store);
	std::optional<UpdateCheck> result;
	controller.CheckForUpdates([&](UpdateCheck check) { result = check; });
	REQUIRE(result.has_value());
	CHECK(result->result == UpdateCheck::Result::Failed);
}

TEST_CASE_FIXTURE(UpdateFixture, "a required update blocks login")
{
	feed.manifest = Manifest("2.0.0", "2.0.0");
	Check();

	CHECK(RequestCode(controller, "+886", "912345678").result == SmsCodeRequest::Result::UpdateRequired);
	CHECK(testing::LogIn(controller) == SmsLoginResponse::Result::UpdateRequired);
	std::optional<QrLoginUpdate> qr;
	controller.StartQrLogin([&](QrLoginUpdate update) { qr = update; });
	REQUIRE(qr.has_value());
	CHECK(qr->result == QrLoginUpdate::Result::UpdateRequired);
	CHECK_FALSE(controller.IsLoggedIn());
}

TEST_CASE_FIXTURE(UpdateFixture, "a canceled download reports nothing and cannot be launched")
{
	feed.manifest = Manifest("1.2.0", "1.0.0");
	Check();
	feed.finishDownloads = false;

	int results = 0;
	controller.DownloadUpdate([](int) {}, [&](InstallerDownload) { ++results; });
	controller.CancelUpdateDownload(); // the update dialog's X
	CHECK(feed.cancelCount == 1);
	feed.FinishDownload(); // the aborted transfer ends
	CHECK(results == 0);
	CHECK_FALSE(controller.LaunchUpdateInstaller());

	// Trying again later works.
	controller.DownloadUpdate([](int) {}, [&](InstallerDownload) { ++results; });
	feed.FinishDownload();
	CHECK(results == 1);
	CHECK(controller.LaunchUpdateInstaller());
}

TEST_CASE_FIXTURE(UpdateFixture, "download and launch the installer")
{
	feed.manifest = Manifest("1.2.0", "1.0.0");
	Check();

	CHECK_FALSE(controller.LaunchUpdateInstaller()); // nothing downloaded yet

	int lastPercent = -1;
	auto result = InstallerDownload::Failed;
	controller.DownloadUpdate([&](int percent) { lastPercent = percent; },
				  [&](InstallerDownload r) { result = r; });
	CHECK(result == InstallerDownload::Ok);
	CHECK(lastPercent == 100);

	CHECK(controller.LaunchUpdateInstaller());
	CHECK(feed.launchedPath == "/tmp/installer");
}

TEST_CASE_FIXTURE(UpdateFixture, "a failed download cannot be launched")
{
	feed.manifest = Manifest("1.2.0", "1.0.0");
	feed.downloadResult = InstallerDownload::Failed;
	Check();

	auto result = InstallerDownload::Ok;
	controller.DownloadUpdate([](int) {}, [&](InstallerDownload r) { result = r; });
	CHECK(result == InstallerDownload::Failed);
	CHECK_FALSE(controller.LaunchUpdateInstaller());
}

TEST_CASE_FIXTURE(UpdateFixture, "a checksum mismatch is reported and cannot be launched")
{
	feed.manifest = Manifest("1.2.0", "1.0.0");
	feed.downloadResult = InstallerDownload::ChecksumMismatch;
	Check();

	auto result = InstallerDownload::Ok;
	controller.DownloadUpdate([](int) {}, [&](InstallerDownload r) { result = r; });
	CHECK(result == InstallerDownload::ChecksumMismatch);
	CHECK_FALSE(controller.LaunchUpdateInstaller());
}

TEST_CASE_FIXTURE(UpdateFixture, "no update while live")
{
	feed.manifest = Manifest("1.2.0", "1.0.0");
	Check();
	LogIn();
	REQUIRE(controller.OnStreamingStarted().empty());

	CHECK_FALSE(controller.CanInstallUpdate());
	auto result = InstallerDownload::Ok;
	controller.DownloadUpdate([](int) {}, [&](InstallerDownload r) { result = r; });
	CHECK(result == InstallerDownload::Failed);
	CHECK(feed.downloadCount == 0);
}

// --- Backend: go-live flow -----------------------------------------------------

namespace {

struct BackendFixture : Fixture {
	FakeLangLiveApi api;

	BackendFixture()
	{
		controller.SetApi(&api);
		LogIn();
		api.calls.clear();
	}
	~BackendFixture() { controller.SetApi(nullptr); }
};

} // namespace

TEST_CASE_FIXTURE(BackendFixture, "go-live info comes from streaming/info and sets the orientation")
{
	api.info.channels = {{"84", "Chat"}};
	api.info.events = {{"0", "None"}, {"12", "Event"}};
	api.info.title = "Saved title";
	api.info.channelId = "84";
	api.info.orientation = Orientation::Portrait;
	const int before = stateChanges;

	GoLiveInfo info;
	controller.LoadGoLiveInfo([&](ApiError, GoLiveInfo i) { info = std::move(i); });
	CHECK(api.lastToken == "fake-token");
	REQUIRE(info.channels.size() == 1);
	CHECK(info.channels[0].name == "Chat");
	CHECK(info.events.size() == 2);
	CHECK(info.title == "Saved title");
	CHECK(controller.AccountOrientation() == Orientation::Portrait);
	CHECK(stateChanges == before + 1);
}

TEST_CASE_FIXTURE(BackendFixture, "an expired login logs out and is reported once")
{
	std::vector<ApiError::Kind> lost;
	controller.SetLoginLostCallback([&](ApiError::Kind reason) { lost.push_back(reason); });
	api.streamingError = {ApiError::Kind::SessionExpired, "99303", "expired"};

	ApiError error;
	controller.LoadGoLiveInfo([&](ApiError e, GoLiveInfo) { error = e; });
	CHECK(error.kind == ApiError::Kind::SessionExpired);
	CHECK_FALSE(controller.IsLoggedIn());
	CHECK_FALSE(store.session.has_value());
	CHECK(lost == std::vector<ApiError::Kind>{ApiError::Kind::SessionExpired});
}

TEST_CASE_FIXTURE(BackendFixture, "an account no longer allowed to stream from OBS (99302) logs out too")
{
	std::vector<ApiError::Kind> lost;
	controller.SetLoginLostCallback([&](ApiError::Kind reason) { lost.push_back(reason); });
	api.streamingError = {ApiError::Kind::NotEligible, "99302", "not allowed"};

	const StartLiveResult result = testing::StartLive(controller, CompleteRequest());
	CHECK(result.error.kind == ApiError::Kind::NotEligible);
	CHECK_FALSE(controller.IsLoggedIn());
	CHECK_FALSE(store.session.has_value());
	CHECK(lost == std::vector<ApiError::Kind>{ApiError::Kind::NotEligible});
	CHECK(host.startCount == 0);
}

TEST_CASE_FIXTURE(BackendFixture, "a saved login is checked when OBS has loaded")
{
	std::vector<ApiError::Kind> lost;
	controller.SetLoginLostCallback([&](ApiError::Kind reason) { lost.push_back(reason); });

	SUBCASE("still valid: stays logged in")
	{
		controller.CheckSavedLogin();
		CHECK(api.calls == std::vector<std::string>{"info"});
		CHECK(controller.IsLoggedIn());
		CHECK(lost.empty());
	}
	SUBCASE("revoked or expired: logged out at once, and the streamer is told")
	{
		api.streamingError = {ApiError::Kind::SessionExpired, "99303", "expired"};
		controller.CheckSavedLogin();
		CHECK_FALSE(controller.IsLoggedIn());
		CHECK(lost == std::vector<ApiError::Kind>{ApiError::Kind::SessionExpired});
	}
	SUBCASE("no network: the login is kept")
	{
		api.streamingError = {ApiError::Kind::Network, {}, "timeout"};
		controller.CheckSavedLogin();
		CHECK(controller.IsLoggedIn());
		CHECK(lost.empty());
	}
	SUBCASE("a live left open: one streaming/info checks both")
	{
		store.openLiveId = "live-9";
		api.info.live = false;
		controller.CheckSavedLogin();
		CHECK(api.calls == std::vector<std::string>{"info"});
		CHECK(store.openLiveId.empty());
	}
	SUBCASE("not logged in: nothing is sent")
	{
		controller.Logout();
		api.calls.clear();
		controller.CheckSavedLogin();
		CHECK(api.calls.empty());
	}
}

TEST_CASE_FIXTURE(BackendFixture, "Save sends title, push notification, and channel to the backend")
{
	GoLiveRequest request = CompleteRequest();
	request.settings.pushNotification = "Come";
	const SaveSettingsResult result = Save(controller, request);
	CHECK(result.applied);
	CHECK(result.error.ok());
	CHECK(api.lastSettings.title == "Tonight");
	CHECK(api.lastSettings.pushNotification == "Come");
	CHECK(api.lastSettings.channelId == "channel-1");

	api.streamingError = {ApiError::Kind::Network, "", "timeout"};
	CHECK(Save(controller, request).error.kind == ApiError::Kind::Network);
	CHECK(store.stored.title == "Tonight"); // kept locally anyway
}

TEST_CASE_FIXTURE(BackendFixture, "Save without a channel sends the backend's own back, as streaming/setting needs one")
{
	api.info.channelId = "84";
	controller.LoadGoLiveInfo([](ApiError, GoLiveInfo) {});
	GoLiveRequest request = CompleteRequest();
	request.channelId.clear();
	CHECK(Save(controller, request).error.ok());
	CHECK(api.lastSettings.channelId == "84");

	request.channelId = "85";
	Save(controller, request);
	CHECK(api.lastSettings.channelId == "85");
}

TEST_CASE_FIXTURE(BackendFixture, "Start Live opens the live with the uploaded cover and points OBS at its ingest")
{
	controller.UseUploadedCover("https://img.example.com/c.jpg");
	GoLiveRequest request = CompleteRequest();
	request.mainEventId = "12";

	REQUIRE(testing::StartLive(controller, request).started());
	CHECK(api.calls == std::vector<std::string>{"info", "setting", "start"}); // uploaded when chosen
	CHECK(api.lastSettings.title == "Tonight");
	CHECK(api.lastSettings.channelId == "channel-1");
	CHECK(api.lastStart.coverUrl == "https://img.example.com/c.jpg");
	CHECK(api.lastStart.title == "Tonight");
	CHECK(api.lastStart.channelId == "channel-1");
	CHECK(api.lastStart.mainEventId == "12");
	CHECK(host.serverUrl == "rtmp://ingest.example.com/live");
	CHECK(host.streamKey == "fake-key");
	CHECK(host.startCount == 1);
	CHECK_FALSE(controller.IsStartingLive());

	REQUIRE(controller.OnStreamingStarted().empty());
	CHECK(controller.CurrentLiveSource() == LiveSource::Plugin);

	int endedOutside = 0;
	controller.SetLiveEndedOutsideCallback([&]() { ++endedOutside; });
	controller.EndLive();
	CHECK(controller.OnStreamingStopped());
	CHECK(api.calls.back() == "close");
	CHECK(api.lastCloseLiveId == "live-1");
	CHECK(host.streamKey.empty()); // OBS's own Start Streaming cannot reuse it
	CHECK(host.serverUrl == "rtmp://ingest.example.com/live");
	CHECK(endedOutside == 0); // ended from the plugin: no notice
}

TEST_CASE_FIXTURE(BackendFixture, "OBS's Stop Streaming ends the live, clears the key, and tells the streamer")
{
	int endedOutside = 0;
	controller.SetLiveEndedOutsideCallback([&]() { ++endedOutside; });
	REQUIRE(testing::StartLive(controller, CompleteRequest()).started());
	REQUIRE(controller.OnStreamingStarted().empty());

	CHECK(controller.OnStreamingStopped()); // not from the plugin
	CHECK(api.calls.back() == "close");
	CHECK(host.streamKey.empty());
	CHECK(host.clearKeyCount == 1);
	CHECK(endedOutside == 1);
	CHECK(controller.CurrentLiveSource() == LiveSource::None);

	controller.OnStreamingStopped(); // nothing left to end
	CHECK(host.clearKeyCount == 1);
	CHECK(endedOutside == 1);
}

TEST_CASE_FIXTURE(BackendFixture, "the pre-stream check's stop clears the key without the ended notice")
{
	int endedOutside = 0;
	controller.SetLiveEndedOutsideCallback([&]() { ++endedOutside; });
	// Passes before starting, then OBS's actual encoder fails the check.
	REQUIRE(testing::StartLive(controller, CompleteRequest()).started());
	host.state.videoCodec = "hevc";
	REQUIRE_FALSE(controller.OnStreamingStarted().empty());
	controller.OnStreamingStopped();
	CHECK(host.streamKey.empty());
	CHECK(endedOutside == 0);
}

TEST_CASE_FIXTURE(BackendFixture, "a stream key is cleared when OBS exits during a live")
{
	REQUIRE(testing::StartLive(controller, CompleteRequest()).started());
	REQUIRE(controller.OnStreamingStarted().empty());
	controller.OnExit();
	CHECK(host.streamKey.empty());
}

TEST_CASE_FIXTURE(Fixture, "the mock backend gives no key, so OBS's own key is left alone")
{
	host.streamKey = "streamer-own-key";
	LogIn();
	REQUIRE(testing::StartLive(controller, CompleteRequest()).started());
	REQUIRE(controller.OnStreamingStarted().empty());
	controller.OnStreamingStopped();
	CHECK(host.streamKey == "streamer-own-key");
	CHECK(host.clearKeyCount == 0);
}

TEST_CASE_FIXTURE(BackendFixture, "OBS refusing the start right away closes the live and frees the plugin")
{
	int endedOutside = 0;
	controller.SetLiveEndedOutsideCallback([&]() { ++endedOutside; });
	// e.g. the encoder cannot start: OBS shows its error, no frontend event
	host.onStart = [&]() {
		controller.OnStreamStartFailed();
	};

	const StartLiveResult result = testing::StartLive(controller, CompleteRequest());
	CHECK_FALSE(result.started());
	CHECK(result.obsStartFailed);
	CHECK(api.calls.back() == "close");
	CHECK(api.lastCloseLiveId == "live-1");
	CHECK(host.streamKey.empty());
	CHECK(store.openLiveId.empty());
	CHECK(endedOutside == 0); // OBS showed its own error
	CHECK_FALSE(controller.IsStartingLive());
	CHECK_FALSE(controller.LogoutEndsLive());
	CHECK(controller.CanLogout());

	// The plugin can go live again, and OBS's own Start Streaming is not
	// mistaken for a plugin live.
	host.onStart = nullptr;
	REQUIRE(controller.OnStreamingStarted().empty());
	CHECK(controller.CurrentLiveSource() == LiveSource::External);
}

TEST_CASE_FIXTURE(BackendFixture, "OBS failing to start later closes the live too")
{
	REQUIRE(testing::StartLive(controller, CompleteRequest()).started());
	REQUIRE(controller.IsStartingLive() == false);
	REQUIRE(controller.LogoutEndsLive()); // the live is open, the stream not up yet

	controller.OnStreamStartFailed();
	CHECK(api.calls.back() == "close");
	CHECK(host.streamKey.empty());
	CHECK_FALSE(controller.LogoutEndsLive());
	CHECK(testing::StartLive(controller, CompleteRequest()).started());
}

TEST_CASE_FIXTURE(BackendFixture, "a failure signal after the stream started, or for OBS's own stream, is ignored")
{
	REQUIRE(testing::StartLive(controller, CompleteRequest()).started());
	REQUIRE(controller.OnStreamingStarted().empty());
	const size_t calls = api.calls.size();
	controller.OnStreamStartFailed(); // OBS also emits the signal on a normal stop
	CHECK(api.calls.size() == calls);
	CHECK(controller.CurrentLiveSource() == LiveSource::Plugin);
	CHECK(host.streamKey == "fake-key");
}

namespace {

int Uploads(const FakeLangLiveApi &api)
{
	return static_cast<int>(std::count(api.calls.begin(), api.calls.end(), "upload"));
}

// Starts a live with `request` and ends it again.
void LiveOnce(PluginController &controller, const GoLiveRequest &request)
{
	REQUIRE(testing::StartLive(controller, request).started());
	REQUIRE(controller.OnStreamingStarted().empty());
	controller.EndLive();
	controller.OnStreamingStopped();
}

} // namespace

namespace {

// UploadCover for a backend that answers right away.
std::pair<ApiError, std::string> UploadNow(PluginController &controller, const std::string &path)
{
	std::pair<ApiError, std::string> result{{ApiError::Kind::Other, {}, "no answer"}, {}};
	controller.UploadCover(path,
			       [&](ApiError error, std::string url) { result = {std::move(error), std::move(url)}; });
	return result;
}

// The cover the go-live form shows (LoadGoLiveInfo).
std::string ShownCover(PluginController &controller)
{
	std::string url = "no answer";
	controller.LoadGoLiveInfo([&](ApiError, GoLiveInfo info) { url = info.coverUrl; });
	return url;
}

constexpr const char *kRoomCover = "https://img.example.com/room.jpg";
constexpr const char *kWebsiteCover = "https://img.example.com/website.jpg";
constexpr const char *kUploadedCover = "https://img.example.com/c.jpg"; // FakeLangLiveApi::coverImageUrl

} // namespace

TEST_CASE_FIXTURE(BackendFixture, "the form shows the room's cover, and a live starts with it")
{
	api.info.coverUrl = kRoomCover;
	CHECK(ShownCover(controller) == kRoomCover);
	LiveOnce(controller, CompleteRequest());
	CHECK(api.lastStart.coverUrl == kRoomCover);
	CHECK(Uploads(api) == 0);

	api.info.coverUrl.clear(); // no cover at all
	CHECK(ShownCover(controller).empty());
}

TEST_CASE_FIXTURE(BackendFixture, "a cover is uploaded when chosen, and used until the room's cover changes")
{
	api.info.coverUrl = kRoomCover;
	REQUIRE(ShownCover(controller) == kRoomCover);

	const auto [error, url] = UploadNow(controller, "cover-upload.jpg");
	REQUIRE(error.ok());
	CHECK(url == kUploadedCover);
	CHECK(api.lastUploadFilename == "cover.jpg");
	CHECK(store.stored.uploadedCoverUrl.empty()); // only once the dialog uses it
	controller.UseUploadedCover(url);
	CHECK(store.stored.uploadedCoverUrl == kUploadedCover);
	CHECK(store.stored.coverBaselineUrl == kRoomCover);

	// The room still has its old cover (it takes ours with streaming/start):
	// ours is newer. Save uploads nothing and keeps it.
	CHECK(ShownCover(controller) == kUploadedCover);
	api.calls.clear();
	CHECK(Save(controller, CompleteRequest()).error.ok());
	CHECK(api.calls == std::vector<std::string>{"setting"});
	CHECK(ShownCover(controller) == kUploadedCover);

	// Changed on the website since: the room's wins, ours is dropped.
	api.info.coverUrl = kWebsiteCover;
	CHECK(ShownCover(controller) == kWebsiteCover);
	CHECK(store.stored.uploadedCoverUrl.empty());
	LiveOnce(controller, CompleteRequest());
	CHECK(api.lastStart.coverUrl == kWebsiteCover);
}

TEST_CASE_FIXTURE(BackendFixture, "an uploaded cover goes live, and then the room has it")
{
	api.info.coverUrl = kRoomCover;
	REQUIRE(ShownCover(controller) == kRoomCover);
	controller.UseUploadedCover(UploadNow(controller, "cover-upload.jpg").second);

	LiveOnce(controller, CompleteRequest());
	CHECK(api.lastStart.coverUrl == kUploadedCover);
	CHECK(Uploads(api) == 1); // when chosen, not again for the live

	api.info.coverUrl = kUploadedCover; // the backend took it
	CHECK(ShownCover(controller) == kUploadedCover);
	CHECK(store.stored.uploadedCoverUrl.empty()); // the room's now
}

TEST_CASE_FIXTURE(BackendFixture, "an uploaded cover outlives OBS until the room's cover changes")
{
	api.info.coverUrl = kRoomCover;
	REQUIRE(ShownCover(controller) == kRoomCover);
	controller.UseUploadedCover(UploadNow(controller, "cover-upload.jpg").second);

	PluginController restarted{host, store};
	restarted.SetApi(&api);
	CHECK(ShownCover(restarted) == kUploadedCover);
	restarted.SetApi(nullptr);
}

TEST_CASE_FIXTURE(BackendFixture, "a refused or failed cover upload changes nothing")
{
	api.info.coverUrl = kRoomCover;
	REQUIRE(ShownCover(controller) == kRoomCover);

	api.coverUploadError = {ApiError::Kind::Other, "-1", "daily upload limit"};
	auto result = UploadNow(controller, "cover-upload.jpg");
	CHECK(result.first.message == "daily upload limit"); // shown to the streamer
	CHECK(Uploads(api) == 0);

	api.coverUploadError = {};
	api.putError = {ApiError::Kind::Network, "", "HTTP 403"};
	result = UploadNow(controller, "cover-upload.jpg");
	CHECK(result.first.kind == ApiError::Kind::Network);
	CHECK(result.second.empty());

	CHECK(store.stored.uploadedCoverUrl.empty());
	CHECK(ShownCover(controller) == kRoomCover);
}

TEST_CASE_FIXTURE(Fixture, "a cover is not uploaded when logged out")
{
	const auto [error, url] = UploadNow(controller, "cover-upload.jpg");
	CHECK(error.kind == ApiError::Kind::SessionExpired);
	CHECK(url.empty());
}

TEST_CASE_FIXTURE(BackendFixture, "a new login drops an upload: it was the other account's")
{
	api.info.coverUrl = kRoomCover;
	REQUIRE(ShownCover(controller) == kRoomCover);
	controller.UseUploadedCover(UploadNow(controller, "cover-upload.jpg").second);

	controller.Logout();
	LogIn();
	CHECK(store.stored.uploadedCoverUrl.empty());
	CHECK(store.stored.coverBaselineUrl.empty());
	api.info.coverUrl = kWebsiteCover; // this account's room
	CHECK(ShownCover(controller) == kWebsiteCover);
}

TEST_CASE_FIXTURE(BackendFixture, "the cover image is downloaded to show, nothing when it cannot be")
{
	api.imageBytes = "image bytes";
	std::string bytes = "no answer";
	controller.LoadCoverImage(kRoomCover, [&](std::string b) { bytes = std::move(b); });
	CHECK(bytes == "image bytes");
	CHECK(api.lastImageUrl == kRoomCover);

	api.imageError = {ApiError::Kind::Network, "", "HTTP 404"};
	controller.LoadCoverImage(kRoomCover, [&](std::string b) { bytes = std::move(b); });
	CHECK(bytes.empty());

	api.calls.clear();
	controller.LoadCoverImage("", [&](std::string b) { bytes = std::move(b); });
	CHECK(bytes.empty());
	CHECK(api.calls.empty()); // no cover, no request
}

TEST_CASE_FIXTURE(BackendFixture, "a refused start leaves OBS alone")
{
	api.streamingError = {ApiError::Kind::WebLiveActive, "99304", "live on the web"};
	const StartLiveResult result = testing::StartLive(controller, CompleteRequest());
	CHECK(result.error.kind == ApiError::Kind::WebLiveActive);
	CHECK(host.startCount == 0);
	CHECK(host.streamKey.empty());
	CHECK_FALSE(controller.IsStartingLive());

	controller.OnStreamingStopped();
	CHECK(api.calls.back() == "setting"); // refused before start; nothing to close
}

TEST_CASE_FIXTURE(BackendFixture, "a live from the website blocks Start Live before the pre-stream check")
{
	// The website live's orientation, not the account's: the pre-stream
	// check would fail on it.
	api.info.live = true;
	api.info.platform = StreamingInfo::Platform::Web;
	api.info.orientation = Orientation::Landscape;
	host.state.outputWidth = 720;
	host.state.outputHeight = 1280;
	const StartLiveResult result = testing::StartLive(controller, CompleteRequest());
	CHECK(result.error.kind == ApiError::Kind::WebLiveActive);
	CHECK(result.violations.empty());
	CHECK(api.calls == std::vector<std::string>{"info"});
	CHECK(host.startCount == 0);
	CHECK_FALSE(controller.IsStartingLive());

	// Ended on the website: goes live.
	api.info.live = false;
	api.info.platform = StreamingInfo::Platform::None;
	api.info.orientation = Orientation::Portrait;
	api.calls.clear();
	CHECK(testing::StartLive(controller, CompleteRequest()).started());
	CHECK(api.calls == std::vector<std::string>{"info", "setting", "start"});
	CHECK(controller.AccountOrientation() == Orientation::Portrait);
}

TEST_CASE_FIXTURE(BackendFixture, "a form the backend refuses to save is not started")
{
	api.streamingError = {ApiError::Kind::Other, "-1", "sensitive word in the title"};
	const StartLiveResult result = testing::StartLive(controller, CompleteRequest());
	CHECK(result.error.message == "sensitive word in the title");
	CHECK(api.calls == std::vector<std::string>{"info", "setting"});
	CHECK(host.startCount == 0);
	CHECK_FALSE(controller.IsStartingLive());
}

TEST_CASE_FIXTURE(BackendFixture, "a live the pre-stream check stopped is closed on the backend")
{
	host.simpleOutputMode = false;
	REQUIRE(testing::StartLive(controller, CompleteRequest()).started());
	host.state.keyframeIntervalSeconds = 0; // OBS's encoder differs from what the profile said

	CHECK_FALSE(controller.OnStreamingStarted().empty());
	CHECK_FALSE(controller.OnStreamingStopped());
	CHECK(api.calls.back() == "close");
	CHECK(api.lastCloseLiveId == "live-1");
}

TEST_CASE_FIXTURE(BackendFixture, "the log has the live_id and why the pre-stream check stopped a stream")
{
	std::vector<std::string> log;
	controller.SetLogCallback(
		[&](PluginController::LogLevel, const std::string &message) { log.push_back(message); });
	host.simpleOutputMode = false;
	REQUIRE(testing::StartLive(controller, CompleteRequest()).started());
	host.state.keyframeIntervalSeconds = 0;
	CHECK_FALSE(controller.OnStreamingStarted().empty());
	controller.OnStreamingStopped();

	const auto has = [&](const std::string &text) {
		return std::any_of(log.begin(), log.end(),
				   [&](const std::string &line) { return line.find(text) != std::string::npos; });
	};
	CHECK(has("live opened on the backend, live_id 'live-1'"));
	CHECK(has("keyframe_interval_zero"));
	CHECK(has("keyint 0 s"));
	CHECK(has("closing live_id 'live-1'"));
	CHECK_FALSE(has("fake-key")); // never the stream key
}

TEST_CASE_FIXTURE(BackendFixture, "a stream started from OBS with a website key is not closed by the plugin")
{
	REQUIRE(controller.OnStreamingStarted().empty());
	CHECK_FALSE(controller.OnStreamingStopped());
	CHECK(api.calls.empty());
}

// --- Pre-stream check before the backend ----------------------------------------

TEST_CASE_FIXTURE(BackendFixture, "settings that fail the check stop go-live before the backend")
{
	host.state.videoCodec = "hevc";
	host.state.audioCodec = "opus";
	const StartLiveResult result = testing::StartLive(controller, CompleteRequest());
	CHECK_FALSE(result.started());
	CHECK(result.violations ==
	      std::vector<Violation>{Violation::UnsupportedVideoCodec, Violation::UnsupportedAudioCodec});
	CHECK(api.calls == std::vector<std::string>{"info"}); // nothing opened or saved
	CHECK(host.startCount == 0);
	CHECK_FALSE(controller.IsStartingLive());
}

TEST_CASE_FIXTURE(BackendFixture, "a canvas of another aspect ratio goes live; applying never changes it")
{
	host.state.canvasWidth = 1440; // 4:3 on a landscape account: the streamer's choice
	host.state.canvasHeight = 1080;
	host.state.videoCodec = "hevc";
	const StartLiveResult result = testing::StartLive(controller, CompleteRequest());
	CHECK(result.violations == std::vector<Violation>{Violation::UnsupportedVideoCodec});
	CHECK(api.calls == std::vector<std::string>{"info"});

	std::optional<StartLiveResult> retried;
	controller.ApplySettingsAndStart(Scenario::General, [&](StartLiveResult r) { retried = r; });
	REQUIRE(retried.has_value());
	CHECK(retried->started());
	CHECK(host.state.canvasWidth == 1440);
	CHECK(host.state.canvasHeight == 1080);
}

TEST_CASE_FIXTURE(BackendFixture, "apply and go live switches to H.264 / AAC and starts the same live")
{
	host.state.videoCodec = "av1";
	GoLiveRequest request = CompleteRequest();
	request.mainEventId = "12";
	REQUIRE_FALSE(testing::StartLive(controller, request).violations.empty());

	std::optional<StartLiveResult> result;
	REQUIRE(controller.ApplySettingsAndStart(Scenario::HighQuality, [&](StartLiveResult r) { result = r; }));
	REQUIRE(result.has_value());
	CHECK(result->started());
	CHECK(host.state.videoCodec == "h264");
	CHECK(host.state.outputWidth == 1920);
	CHECK(api.calls == std::vector<std::string>{"info", "info", "setting", "start"});
	CHECK(api.lastStart.mainEventId == "12");
	CHECK(host.startCount == 1);
	CHECK(controller.Settings().scenario == Scenario::HighQuality);

	controller.OnStreamingStarting();
	CHECK(host.profileCodecsCount == 1); // OBS's encoders follow the switched profile
	CHECK(controller.OnStreamingStarted().empty());
	CHECK(controller.CurrentLiveSource() == LiveSource::Plugin);
}

TEST_CASE_FIXTURE(BackendFixture, "apply and go live does not start when OBS refuses the settings")
{
	host.state.videoCodec = "hevc";
	REQUIRE_FALSE(testing::StartLive(controller, CompleteRequest()).violations.empty());
	host.applySucceeds = false;

	bool called = false;
	CHECK_FALSE(controller.ApplySettingsAndStart(Scenario::General, [&](StartLiveResult) { called = true; }));
	CHECK_FALSE(called);
	CHECK(host.startCount == 0);
	CHECK(api.calls == std::vector<std::string>{"info"}); // from the first Start Live only
}

TEST_CASE_FIXTURE(Fixture, "only checked streams get the profile's codecs while starting")
{
	controller.OnStreamingStarting(); // logged out
	CHECK(host.profileCodecsCount == 0);

	LogIn();
	controller.OnStreamingStarting(); // OBS's own Start Streaming
	CHECK(host.profileCodecsCount == 0);

	REQUIRE(testing::StartLive(controller, CompleteRequest()).started());
	controller.OnStreamingStarting();
	CHECK(host.profileCodecsCount == 1);
}

// --- A live left open ------------------------------------------------------------

TEST_CASE_FIXTURE(BackendFixture, "the open live_id is recorded, and forgotten once closed")
{
	REQUIRE(testing::StartLive(controller, CompleteRequest()).started());
	CHECK(store.openLiveId == "live-1");
	REQUIRE(controller.OnStreamingStarted().empty());

	controller.EndLive();
	controller.OnStreamingStopped();
	CHECK(api.lastCloseLiveId == "live-1");
	CHECK(store.openLiveId.empty());
}

TEST_CASE_FIXTURE(BackendFixture, "a close that could not be sent is kept for the next session")
{
	REQUIRE(testing::StartLive(controller, CompleteRequest()).started());
	REQUIRE(controller.OnStreamingStarted().empty());
	api.streamingError = {ApiError::Kind::Network, "", "timeout"};
	controller.EndLive();
	controller.OnStreamingStopped();
	CHECK(store.openLiveId == "live-1");

	// The next session tries again; an answer that retrying cannot change
	// ends it.
	api.streamingError = {};
	api.info.live = true;
	api.info.platform = StreamingInfo::Platform::Plugin;
	api.info.liveId = "live-1";
	controller.RecoverUnclosedLive();
	CHECK(store.openLiveId.empty());
}

TEST_CASE_FIXTURE(BackendFixture, "a close the backend refuses is not retried")
{
	REQUIRE(testing::StartLive(controller, CompleteRequest()).started());
	REQUIRE(controller.OnStreamingStarted().empty());
	api.streamingError = {ApiError::Kind::Other, "-2", "not your live"};
	controller.EndLive();
	controller.OnStreamingStopped();
	CHECK(store.openLiveId.empty());
}

TEST_CASE_FIXTURE(BackendFixture, "quitting OBS while live closes the live before the plugin unloads")
{
	REQUIRE(testing::StartLive(controller, CompleteRequest()).started());
	REQUIRE(controller.OnStreamingStarted().empty());

	controller.OnExit();
	CHECK(api.calls.back() == "close_now");
	CHECK(api.lastCloseLiveId == "live-1");
	CHECK(api.lastCloseTimeoutSeconds > 0);
	CHECK(api.lastCloseTimeoutSeconds <= 5);
	CHECK(store.openLiveId.empty());

	// OBS may still report the stream stopping: nothing is closed twice.
	controller.OnStreamingStopped();
	CHECK(api.calls.back() == "close_now");
}

TEST_CASE_FIXTURE(BackendFixture, "quitting OBS without a live sends nothing")
{
	controller.OnExit();
	CHECK(std::count(api.calls.begin(), api.calls.end(), "close_now") == 0);
}

TEST_CASE_FIXTURE(BackendFixture, "a live the last session left open is closed when OBS starts")
{
	store.openLiveId = "live-9";
	api.info.live = true;
	api.info.platform = StreamingInfo::Platform::Plugin;
	api.info.liveId = "live-9";

	controller.RecoverUnclosedLive();
	CHECK(api.calls == std::vector<std::string>{"info", "close"});
	CHECK(api.lastCloseLiveId == "live-9");
	CHECK(store.openLiveId.empty());
}

TEST_CASE_FIXTURE(BackendFixture, "another live is never closed: other id, the website, or none")
{
	SUBCASE("another live_id (another computer)")
	{
		api.info.live = true;
		api.info.platform = StreamingInfo::Platform::Plugin;
		api.info.liveId = "live-other";
	}
	SUBCASE("a live from the website")
	{
		api.info.live = true;
		api.info.platform = StreamingInfo::Platform::Web;
		api.info.liveId = "live-9";
	}
	SUBCASE("closed already")
	{
		api.info.live = false;
	}
	store.openLiveId = "live-9";
	controller.RecoverUnclosedLive();
	CHECK(std::count(api.calls.begin(), api.calls.end(), "close") == 0);
	CHECK(store.openLiveId.empty());
}

TEST_CASE_FIXTURE(BackendFixture, "nothing recorded, nothing checked")
{
	controller.RecoverUnclosedLive();
	CHECK(api.calls.empty());
}

// --- A live left open on the backend (e.g. the close failed offline) -----------------

TEST_CASE_FIXTURE(BackendFixture, "opening Go Live closes a plugin live this OBS is not streaming")
{
	api.info.live = true;
	api.info.platform = StreamingInfo::Platform::Plugin;
	api.info.liveId = "live-7"; // not recorded here: closed all the same (PM's decision)
	api.info.title = "Saved title";

	bool answered = false;
	GoLiveInfo info;
	controller.LoadGoLiveInfo([&](ApiError error, GoLiveInfo i) {
		CHECK(error.ok());
		answered = true;
		info = std::move(i);
	});
	CHECK(answered);
	CHECK(info.title == "Saved title");
	CHECK(api.calls == std::vector<std::string>{"info", "close"});
	CHECK(api.lastCloseLiveId == "live-7");
	CHECK(store.openLiveId.empty());
}

TEST_CASE_FIXTURE(BackendFixture, "opening Go Live shows the form even if that close fails")
{
	store.openLiveId = "live-7";
	api.info.live = true;
	api.info.platform = StreamingInfo::Platform::Plugin;
	api.info.liveId = "live-7";
	api.closeError = {ApiError::Kind::Network, {}, "offline"};

	bool answered = false;
	controller.LoadGoLiveInfo([&](ApiError error, GoLiveInfo) {
		CHECK(error.ok());
		answered = true;
	});
	CHECK(answered);
	CHECK(api.calls == std::vector<std::string>{"info", "close"});
	CHECK(store.openLiveId == "live-7"); // not sent: kept for the next try
}

TEST_CASE_FIXTURE(BackendFixture, "opening Go Live leaves the website's live and this OBS's own live alone")
{
	SUBCASE("a live from the website")
	{
		api.info.live = true;
		api.info.platform = StreamingInfo::Platform::Web;
		api.info.liveId = "live-web";
	}
	SUBCASE("this OBS's live")
	{
		REQUIRE(testing::StartLive(controller, CompleteRequest()).started());
		REQUIRE(controller.OnStreamingStarted().empty());
		api.info.live = true;
		api.info.platform = StreamingInfo::Platform::Plugin;
		api.info.liveId = api.target.liveId;
	}
	api.calls.clear();
	controller.LoadGoLiveInfo([](ApiError, GoLiveInfo) {});
	CHECK(api.calls == std::vector<std::string>{"info"});
}

TEST_CASE_FIXTURE(BackendFixture, "Start Live closes a plugin live still open, then goes live")
{
	api.info.live = true;
	api.info.platform = StreamingInfo::Platform::Plugin;
	api.info.liveId = "live-7";
	const StartLiveResult result = testing::StartLive(controller, CompleteRequest());
	CHECK(result.started());
	REQUIRE(api.calls.size() >= 2);
	CHECK(api.calls[0] == "info");
	CHECK(api.calls[1] == "close");
	CHECK(api.lastCloseLiveId == "live-7");
	CHECK(host.startCount == 1);
}

TEST_CASE_FIXTURE(BackendFixture, "Start Live does not go live when that close fails")
{
	api.info.live = true;
	api.info.platform = StreamingInfo::Platform::Plugin;
	api.info.liveId = "live-7";
	api.closeError = {ApiError::Kind::Network, {}, "offline"};
	const StartLiveResult result = testing::StartLive(controller, CompleteRequest());
	CHECK(result.error.kind == ApiError::Kind::Network);
	CHECK(api.calls == std::vector<std::string>{"info", "close"});
	CHECK(host.startCount == 0);
	CHECK_FALSE(controller.IsStartingLive());
}

// --- Logging out while live ---------------------------------------------------------

TEST_CASE_FIXTURE(BackendFixture, "logging out during a plugin live ends it: stream stopped, live closed")
{
	REQUIRE(testing::StartLive(controller, CompleteRequest()).started());
	REQUIRE(controller.OnStreamingStarted().empty());
	CHECK(controller.LogoutEndsLive());
	CHECK_FALSE(controller.CanLogout()); // the UI disables logout; called anyway, it ends the live

	controller.Logout();
	CHECK_FALSE(controller.IsLoggedIn());
	CHECK(host.stopCount == 1);
	CHECK(api.calls.back() == "logout"); // after the close, which needed the token
	CHECK(std::count(api.calls.begin(), api.calls.end(), "close") == 1);
	CHECK(api.lastCloseLiveId == "live-1");
	CHECK(api.lastToken == "fake-token"); // sent before the token was forgotten
	CHECK(store.openLiveId.empty());

	// OBS then reports the stream stopped: nothing is closed twice.
	controller.OnStreamingStopped();
	CHECK(std::count(api.calls.begin(), api.calls.end(), "close") == 1);
	CHECK(controller.CurrentLiveSource() == LiveSource::None);
}

TEST_CASE_FIXTURE(BackendFixture, "logging out with a live open revokes the token only after the close")
{
	REQUIRE(testing::StartLive(controller, CompleteRequest()).started());
	REQUIRE(controller.OnStreamingStarted().empty());
	api.calls.clear();
	controller.Logout();
	CHECK(api.calls == std::vector<std::string>{"close", "logout"});
	CHECK(api.lastToken == "fake-token");
}

TEST_CASE_FIXTURE(BackendFixture, "a failed logout call still logs out locally")
{
	api.logoutError = {ApiError::Kind::Network, {}, "timeout"};
	controller.Logout();
	CHECK_FALSE(controller.IsLoggedIn());
	CHECK_FALSE(store.session.has_value());
	CHECK(api.calls.back() == "logout");
}

TEST_CASE_FIXTURE(BackendFixture, "an expired login is forgotten without a logout call")
{
	api.streamingError = {ApiError::Kind::SessionExpired, "99303", "expired"};
	controller.LoadGoLiveInfo([](ApiError, GoLiveInfo) {});
	CHECK_FALSE(controller.IsLoggedIn());
	CHECK(std::count(api.calls.begin(), api.calls.end(), "logout") == 0);
}

TEST_CASE_FIXTURE(BackendFixture, "logging out leaves a stream started with OBS's own Start Streaming alone")
{
	host.state = ValidState(GetStreamSpec(Scenario::General, Orientation::Landscape));
	controller.OnStreamingStarted(); // not from the plugin
	REQUIRE(controller.CurrentLiveSource() == LiveSource::External);
	CHECK_FALSE(controller.LogoutEndsLive());
	CHECK(controller.CanLogout());

	controller.Logout();
	CHECK(host.stopCount == 0);
	CHECK(std::count(api.calls.begin(), api.calls.end(), "close") == 0);
}

TEST_CASE_FIXTURE(BackendFixture, "an expired login does not end the live; it is closed after the next login")
{
	REQUIRE(testing::StartLive(controller, CompleteRequest()).started());
	REQUIRE(controller.OnStreamingStarted().empty());

	api.streamingError = {ApiError::Kind::SessionExpired, "99303", "expired"};
	controller.LoadGoLiveInfo([](ApiError, GoLiveInfo) {});
	CHECK_FALSE(controller.IsLoggedIn());
	CHECK(host.stopCount == 0);
	CHECK(store.openLiveId == "live-1");

	// The stream stops while logged out: no token to close the live with.
	controller.OnStreamingStopped();
	CHECK(std::count(api.calls.begin(), api.calls.end(), "close") == 0);
	CHECK(store.openLiveId == "live-1");

	api.streamingError = {};
	api.info.live = true;
	api.info.platform = StreamingInfo::Platform::Plugin;
	api.info.liveId = "live-1";
	api.calls.clear();
	LogIn();
	CHECK(api.calls == std::vector<std::string>{"login", "info", "close"});
	CHECK(api.lastCloseLiveId == "live-1");
	CHECK(store.openLiveId.empty());
}

TEST_CASE_FIXTURE(BackendFixture, "a live ended with an expired login logs out, and is closed after the next login")
{
	std::vector<ApiError::Kind> lost;
	controller.SetLoginLostCallback([&](ApiError::Kind reason) { lost.push_back(reason); });
	REQUIRE(testing::StartLive(controller, CompleteRequest()).started());
	REQUIRE(controller.OnStreamingStarted().empty());

	api.streamingError = {ApiError::Kind::SessionExpired, "99303", "expired"};
	controller.EndLive();
	controller.OnStreamingStopped();
	CHECK(api.calls.back() == "close");
	CHECK_FALSE(controller.IsLoggedIn());
	CHECK_FALSE(store.session.has_value());
	CHECK(lost == std::vector<ApiError::Kind>{ApiError::Kind::SessionExpired});
	CHECK(store.openLiveId == "live-1"); // kept for the next login

	api.streamingError = {};
	api.info.live = true;
	api.info.platform = StreamingInfo::Platform::Plugin;
	api.info.liveId = "live-1";
	api.calls.clear();
	LogIn();
	CHECK(api.calls == std::vector<std::string>{"login", "info", "close"});
	CHECK(api.lastCloseLiveId == "live-1");
	CHECK(store.openLiveId.empty());
}

TEST_CASE_FIXTURE(BackendFixture, "an expired login on Save, cover upload, or Start Live logs out, not live")
{
	std::vector<ApiError::Kind> lost;
	controller.SetLoginLostCallback([&](ApiError::Kind reason) { lost.push_back(reason); });
	const ApiError expired{ApiError::Kind::SessionExpired, "99303", "expired"};
	GoLiveRequest request = CompleteRequest();

	SUBCASE("Save: streaming/setting")
	{
		api.streamingError = expired;
		CHECK(testing::Save(controller, request).error.kind == ApiError::Kind::SessionExpired);
	}
	SUBCASE("Upload Cover: upload/image")
	{
		api.coverUploadError = expired;
		CHECK(UploadNow(controller, "cover-upload.jpg").first.kind == ApiError::Kind::SessionExpired);
		CHECK(std::count(api.calls.begin(), api.calls.end(), "upload") == 0);
	}
	SUBCASE("Start Live: streaming/start")
	{
		api.startError = expired;
		CHECK(testing::StartLive(controller, request).error.kind == ApiError::Kind::SessionExpired);
		CHECK(api.calls.back() == "start");
	}
	CHECK_FALSE(controller.IsLoggedIn());
	CHECK_FALSE(store.session.has_value());
	CHECK(lost == std::vector<ApiError::Kind>{ApiError::Kind::SessionExpired});
	CHECK(host.startCount == 0);
	CHECK(host.streamKey.empty());
	CHECK(store.openLiveId.empty());
}

TEST_CASE_FIXTURE(BackendFixture, "replies that all say the login expired are reported once")
{
	std::vector<ApiError::Kind> lost;
	controller.SetLoginLostCallback([&](ApiError::Kind reason) { lost.push_back(reason); });

	// Both sent before either answers, e.g. the saved-login check and the
	// go-live dialog's info.
	api.holdInfo = true;
	controller.CheckSavedLogin();
	controller.LoadGoLiveInfo([](ApiError, GoLiveInfo) {});
	REQUIRE(api.heldInfo.size() == 2);

	api.streamingError = {ApiError::Kind::SessionExpired, "99303", "expired"};
	api.AnswerHeldInfo();
	CHECK_FALSE(controller.IsLoggedIn());
	CHECK(lost == std::vector<ApiError::Kind>{ApiError::Kind::SessionExpired});
}

TEST_CASE_FIXTURE(BackendFixture, "quitting OBS with an expired login keeps the live for the next session")
{
	REQUIRE(testing::StartLive(controller, CompleteRequest()).started());
	REQUIRE(controller.OnStreamingStarted().empty());

	api.streamingError = {ApiError::Kind::SessionExpired, "99303", "expired"};
	controller.OnExit();
	CHECK(api.calls.back() == "close_now");
	CHECK(store.openLiveId == "live-1");
}
