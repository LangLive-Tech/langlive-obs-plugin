#include "app/plugin-controller.hpp"

#include "langlive/obsplugin/core/phone.hpp"
#include "langlive/obsplugin/core/update.hpp"
#include "langlive/obsplugin/core/validation.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace langlive::obsplugin {

namespace {

// qrcode/create's documented lifetime; the API does not return it.
constexpr int kQrLoginExpirySeconds = 300;
constexpr int kSmsResendSeconds = 90;
constexpr int kExitCloseTimeoutSeconds = 3;

bool IsValidSha256(const std::string &hex)
{
	return hex.size() == 64 && std::all_of(hex.begin(), hex.end(), [](unsigned char c) {
		       return std::isdigit(c) != 0 || (c >= 'a' && c <= 'f');
	       });
}

// A failed qrcode/create or qrcode/verify, for the login dialog.
QrLoginUpdate QrLoginFailure(const ApiError &error)
{
	QrLoginUpdate update;
	update.message = error.message;
	switch (error.kind) {
	case ApiError::Kind::NotEligible:
		update.result = QrLoginUpdate::Result::NotEligible;
		break;
	case ApiError::Kind::Network:
	case ApiError::Kind::InvalidResponse:
		update.result = QrLoginUpdate::Result::NetworkError;
		break;
	default:
		update.result = QrLoginUpdate::Result::Failed;
		break;
	}
	return update;
}

} // namespace

PluginController::PluginController(StreamingHost &host, SettingsStore &store)
	: host_(host),
	  store_(store),
	  settings_(store.Load())
{
	if (const std::optional<SavedSession> session = store_.LoadSession(); session && !session->token.empty()) {
		loggedIn_ = true;
		token_ = session->token;
		nickname_ = session->nickname;
	}
}

void PluginController::RequestSmsCode(const std::string &countryCode, const std::string &phoneNumber,
				      std::function<void(SmsCodeRequest)> done)
{
	if (updateRequired_) {
		done({SmsCodeRequest::Result::UpdateRequired, 0, {}});
		return;
	}
	if (!core::IsValidMobileNumber(countryCode, phoneNumber)) {
		done({SmsCodeRequest::Result::InvalidPhoneNumber, 0, {}});
		return;
	}

	// The product spec's 90 s; the API does not return a resend interval.
	api_->SendSmsCode(core::FormatCellphone(countryCode, phoneNumber), [done = std::move(done)](ApiError error) {
		SmsCodeRequest request;
		request.message = error.message;
		switch (error.kind) {
		case ApiError::Kind::None:
			request.result = SmsCodeRequest::Result::Sent;
			request.resendAfterSeconds = kSmsResendSeconds;
			break;
		case ApiError::Kind::SmsLimitReached:
			request.result = SmsCodeRequest::Result::DailyLimitReached;
			break;
		case ApiError::Kind::TooFrequent:
			request.result = SmsCodeRequest::Result::TooFrequent;
			break;
		case ApiError::Kind::Network:
		case ApiError::Kind::InvalidResponse:
			request.result = SmsCodeRequest::Result::NetworkError;
			break;
		default:
			request.result = SmsCodeRequest::Result::Failed;
			break;
		}
		done(std::move(request));
	});
}

void PluginController::LoginWithSmsCode(const std::string &countryCode, const std::string &phoneNumber,
					const std::string &code, std::function<void(SmsLoginResponse)> done)
{
	if (updateRequired_) {
		done({SmsLoginResponse::Result::UpdateRequired, {}});
		return;
	}

	// The number and code are not checked here: the backend decides (and
	// the mock backend accepts anything, so login is quick to test).
	api_->LoginWithSmsCode(core::FormatCellphone(countryCode, phoneNumber), code,
			       [this, done = std::move(done)](ApiError error, std::string token, std::string nickname) {
				       SmsLoginResponse response;
				       response.message = error.message;
				       switch (error.kind) {
				       case ApiError::Kind::None:
					       response.result = SmsLoginResponse::Result::Success;
					       CompleteLogin(token, nickname);
					       break;
				       case ApiError::Kind::NotEligible:
					       response.result = SmsLoginResponse::Result::NotEligible;
					       break;
				       case ApiError::Kind::Network:
				       case ApiError::Kind::InvalidResponse:
					       response.result = SmsLoginResponse::Result::NetworkError;
					       break;
				       default:
					       response.result = SmsLoginResponse::Result::Failed;
					       break;
				       }
				       const bool success = response.result == SmsLoginResponse::Result::Success;
				       done(std::move(response));
				       if (success)
					       RecoverUnclosedLive();
			       });
}

void PluginController::CompleteLogin(const std::string &token, const std::string &nickname)
{
	loggedIn_ = true;
	token_ = token;
	nickname_ = nickname;
	// TODO: the orientation comes from streaming/info once the go-live
	// dialog loads it.
	orientation_ = core::Orientation::Landscape;
	store_.SaveSession({token_, nickname_});
	// The upload belongs to the account that made it; this may be another.
	// So does the room's cover.
	serverCoverUrl_.clear();
	serverCoverKnown_ = false;
	serverChannelId_.clear();
	if (!settings_.uploadedCoverUrl.empty() || !settings_.coverBaselineUrl.empty()) {
		settings_.uploadedCoverUrl.clear();
		settings_.coverBaselineUrl.clear();
		store_.Save(settings_);
	}
	NotifyStateChanged();
}

bool PluginController::HandleSessionError(const ApiError &error)
{
	if (!error.endsLogin() || !loggedIn_)
		return false;
	Log(LogLevel::Warning, "the backend ended the login (ret_code '" + error.code + "'): logged out");
	ForgetLogin();
	if (loginLost_)
		loginLost_(error.kind);
	return true;
}

void PluginController::StartQrLogin(std::function<void(QrLoginUpdate)> done)
{
	qrCode_.reset();
	if (updateRequired_) {
		done({QrLoginUpdate::Result::UpdateRequired, {}, 0, {}});
		return;
	}
	api_->CreateQrLogin([this, done = std::move(done)](ApiError error, QrLoginCode code) {
		if (!error.ok()) {
			done(QrLoginFailure(error));
			return;
		}
		qrCode_ = code;
		done({QrLoginUpdate::Result::Waiting, code.content, kQrLoginExpirySeconds, {}});
	});
}

void PluginController::CheckQrLogin(std::function<void(QrLoginUpdate)> done)
{
	if (!qrCode_ || loggedIn_) {
		done({QrLoginUpdate::Result::Expired, {}, 0, {}});
		return;
	}
	api_->CheckQrLogin(*qrCode_, [this, uuid = qrCode_->uuid, done = std::move(done)](ApiError error,
											  QrLoginState state) {
		// A newer code replaced this one meanwhile: only a login still counts.
		const bool current = qrCode_ && qrCode_->uuid == uuid;
		if (!error.ok()) {
			if (current && error.kind != ApiError::Kind::Network)
				qrCode_.reset(); // refused: this code is done
			done(current ? QrLoginFailure(error)
				     : QrLoginUpdate{QrLoginUpdate::Result::Waiting, {}, 0, {}});
			return;
		}
		switch (state.status) {
		case QrLoginState::Status::Waiting:
			done({QrLoginUpdate::Result::Waiting, {}, 0, {}});
			return;
		case QrLoginState::Status::Scanned:
			done({current ? QrLoginUpdate::Result::Scanned : QrLoginUpdate::Result::Waiting, {}, 0, {}});
			return;
		case QrLoginState::Status::Expired:
			if (current)
				qrCode_.reset();
			done({current ? QrLoginUpdate::Result::Expired : QrLoginUpdate::Result::Waiting, {}, 0, {}});
			return;
		case QrLoginState::Status::LoggedIn:
			break;
		}
		qrCode_.reset();
		if (loggedIn_) { // logged in another way meanwhile
			done({QrLoginUpdate::Result::Expired, {}, 0, {}});
			return;
		}
		Log(LogLevel::Info, "logged in with a QR code");
		CompleteLogin(state.token, state.nickname);
		done({QrLoginUpdate::Result::LoggedIn, {}, 0, {}});
		RecoverUnclosedLive();
	});
}

void PluginController::Logout()
{
	const std::string token = loggedIn_ ? token_ : std::string();
	if (loggedIn_ && backendLiveOpen_) {
		// Closed here, while the token is still known; the stream stopping
		// below then has nothing left to close. The token is revoked only
		// after the close, which needs it.
		const std::string liveId = std::exchange(backendLiveId_, {});
		backendLiveOpen_ = false;
		Log(LogLevel::Info, "logging out: closing live_id '" + liveId + "' on the backend");
		closingLiveId_ = liveId;
		api_->CloseStreaming(token, liveId, [this, liveId, token](ApiError error) {
			if (closingLiveId_ == liveId)
				closingLiveId_.clear();
			FinishClose(liveId, error);
			RevokeToken(token);
		});
	} else if (!token.empty()) {
		RevokeToken(token);
	}
	if (liveSource_ == LiveSource::Plugin) {
		endRequestedByPlugin_ = true;
		host_.StopStreaming();
	}
	ForgetLogin();
}

void PluginController::RevokeToken(const std::string &token)
{
	api_->Logout(token, [this](ApiError error) {
		// 99303: revoked or expired already. Either way the token is gone
		// here, and the backend's expire by themselves.
		if (!error.ok() && error.kind != ApiError::Kind::SessionExpired)
			Log(LogLevel::Warning, "logout on the backend failed (ret_code '" + error.code +
						       "'); the token "
						       "expires by itself");
	});
}

void PluginController::ForgetLogin()
{
	// Logout revokes the token on the backend; an expired login (99303)
	// needs nothing more.
	loggedIn_ = false;
	token_.clear();
	store_.ClearSession();
	nickname_.clear();
	orientation_ = core::Orientation::Landscape;
	NotifyStateChanged();
}

void PluginController::StoreForm(const GoLiveRequest &request)
{
	GoLiveSettings form = request.settings;
	form.uploadedCoverUrl = settings_.uploadedCoverUrl;
	form.coverBaselineUrl = settings_.coverBaselineUrl;
	settings_ = std::move(form);
	store_.Save(settings_);
}

void PluginController::SaveSettings(const GoLiveRequest &request, std::function<void(SaveSettingsResult)> done)
{
	StoreForm(request);
	SaveSettingsResult result;
	result.applied = host_.ApplySpec(core::GetStreamSpec(settings_.scenario, orientation_));

	if (!loggedIn_) {
		done(result);
		return;
	}
	// streaming/setting has no cover: covers are uploaded when chosen
	// (UploadCover). It refuses a missing channel (ret_code 19).
	const std::string &channel = request.channelId.empty() ? serverChannelId_ : request.channelId;
	api_->SaveStreamingSettings(token_, {request.settings.title, request.settings.pushNotification, channel},
				    [this, result, done = std::move(done)](ApiError error) mutable {
					    HandleSessionError(error);
					    result.error = std::move(error);
					    done(std::move(result));
				    });
}

void PluginController::UploadCover(const std::string &path, std::function<void(ApiError, std::string)> done)
{
	if (!loggedIn_) {
		done({ApiError::Kind::SessionExpired, {}, "not logged in"}, {});
		return;
	}
	// The cropped cover goes to S3 through a pre-signed URL. The form saves
	// covers as JPEG.
	api_->RequestCoverUpload(
		token_, "cover.jpg", [this, path, done = std::move(done)](ApiError error, CoverUpload upload) mutable {
			if (HandleSessionError(error) || !error.ok() || !loggedIn_) {
				Log(LogLevel::Warning, "cover upload refused (ret_code '" + error.code + "')");
				done(std::move(error), {});
				return;
			}
			api_->UploadCover(upload, path,
					  [this, imageUrl = upload.imageUrl,
					   done = std::move(done)](ApiError uploadError) mutable {
						  if (!uploadError.ok() || !loggedIn_) {
							  Log(LogLevel::Warning, "cover upload failed");
							  done(std::move(uploadError), {});
							  return;
						  }
						  Log(LogLevel::Info, "cover uploaded");
						  done({}, imageUrl);
					  });
		});
}

void PluginController::UseUploadedCover(const std::string &url)
{
	settings_.uploadedCoverUrl = url;
	// Unknown (streaming/info has not answered): the next answer differs
	// unless the room has no cover, and drops the upload.
	settings_.coverBaselineUrl = serverCoverUrl_;
	store_.Save(settings_);
}

std::string PluginController::EffectiveCoverUrl() const
{
	if (settings_.uploadedCoverUrl.empty())
		return serverCoverUrl_;
	if (!serverCoverKnown_ || settings_.coverBaselineUrl == serverCoverUrl_)
		return settings_.uploadedCoverUrl;
	return serverCoverUrl_;
}

void PluginController::LoadCoverImage(const std::string &url, std::function<void(std::string)> done)
{
	if (url.empty()) {
		done({});
		return;
	}
	api_->DownloadImage(url, [done = std::move(done)](ApiError error, std::string bytes) {
		done(error.ok() ? std::move(bytes) : std::string());
	});
}

void PluginController::LoadGoLiveInfo(std::function<void(ApiError, GoLiveInfo)> done)
{
	if (!loggedIn_) {
		done({ApiError::Kind::SessionExpired, {}, "not logged in"}, {});
		return;
	}
	api_->FetchStreamingInfo(token_, [this, done = std::move(done)](ApiError error, StreamingInfo info) {
		if (HandleSessionError(error) || !error.ok()) {
			done(std::move(error), {});
			return;
		}

		serverCoverUrl_ = info.coverUrl;
		serverCoverKnown_ = true;
		serverChannelId_ = info.channelId;
		if (!settings_.uploadedCoverUrl.empty() && settings_.coverBaselineUrl != serverCoverUrl_) {
			// Changed on the website, or this upload went live: the room's
			// cover is the newer one.
			Log(LogLevel::Info,
			    "the room's cover changed on the backend; it replaces the one uploaded here");
			settings_.uploadedCoverUrl.clear();
			settings_.coverBaselineUrl.clear();
			store_.Save(settings_);
		}
		if (orientation_ != info.orientation) {
			orientation_ = info.orientation;
			NotifyStateChanged();
		}

		GoLiveInfo result;
		for (const ApiChoice &choice : info.channels)
			result.channels.push_back({choice.id, choice.name});
		for (const ApiChoice &choice : info.events)
			result.events.push_back({choice.id, choice.name});
		result.title = info.title;
		result.pushNotification = info.pushNotification;
		result.coverUrl = EffectiveCoverUrl();
		if (!IsStalePluginLive(info)) {
			done({}, std::move(result));
			return;
		}
		// The form waits for it, so Start Live is not pressed meanwhile. If it
		// fails the form shows anyway; StartLive tries again.
		CloseStaleLive(info.liveId,
			       [this, result = std::move(result), done = std::move(done)](ApiError closeError) mutable {
				       if (!loggedIn_) {
					       done(std::move(closeError), {});
					       return;
				       }
				       done({}, std::move(result));
			       });
	});
}

std::vector<GoLiveFieldError> PluginController::ValidateGoLive(const GoLiveRequest &request) const
{
	std::vector<GoLiveFieldError> errors;
	if (request.settings.title.empty())
		errors.push_back(GoLiveFieldError::MissingTitle);
	if (request.channelId.empty())
		errors.push_back(GoLiveFieldError::MissingChannel);
	return errors;
}

void PluginController::StartLive(const GoLiveRequest &request, std::function<void(StartLiveResult)> done)
{
	StartLiveResult result;
	result.missingFields = ValidateGoLive(request);
	if (!result.missingFields.empty()) {
		done(std::move(result));
		return;
	}
	if (!loggedIn_ || liveSource_ != LiveSource::None || startingLive_ || backendLiveOpen_) {
		result.error = {ApiError::Kind::Other, {}, "cannot start a live now"};
		done(std::move(result));
		return;
	}

	// A live from the website blocks this one (streaming/start would refuse
	// it with 99304), and while it runs streaming/info reports its
	// orientation, not the account's: ask first, so the pre-stream check
	// neither runs nor judges the settings by that orientation.
	startingLive_ = true;
	NotifyStateChanged();
	api_->FetchStreamingInfo(token_, [this, request, done = std::move(done)](ApiError error,
										 StreamingInfo info) mutable {
		startingLive_ = false;
		if (HandleSessionError(error) || !loggedIn_) {
			FinishStart(std::move(error), std::move(done));
			return;
		}
		if (error.ok() && info.live && info.platform == StreamingInfo::Platform::Web) {
			Log(LogLevel::Info, "a live from the website is running; not going live");
			FinishStart({ApiError::Kind::WebLiveActive, {}, "a live from the website is running"},
				    std::move(done));
			return;
		}
		// Not live: the orientation is the account's.
		if (error.ok() && !info.live && orientation_ != info.orientation)
			orientation_ = info.orientation;
		if (error.ok() && IsStalePluginLive(info)) {
			// Not closed when the dialog opened (e.g. still offline then).
			startingLive_ = true;
			NotifyStateChanged();
			CloseStaleLive(info.liveId,
				       [this, request, done = std::move(done)](ApiError closeError) mutable {
					       if (!closeError.ok() || !loggedIn_) {
						       FinishStart(std::move(closeError), std::move(done));
						       return;
					       }
					       startingLive_ = false;
					       StartCheckedLive(request, std::move(done));
				       });
			return;
		}
		NotifyStateChanged();
		// Other errors: streaming/start reports them.
		StartCheckedLive(request, std::move(done));
	});
}

void PluginController::StartCheckedLive(const GoLiveRequest &request, std::function<void(StartLiveResult)> done)
{
	StartLiveResult result;
	// Checked before the backend opens the live, so a live is never opened
	// and closed right away for settings the streamer can fix first. The
	// check runs again when the stream starts (OnStreamingStarted).
	const core::StreamState configured = host_.ConfiguredStreamState();
	result.violations = core::Validate(configured, orientation_);
	if (!result.violations.empty()) {
		Log(LogLevel::Warning, "pre-stream check failed before going live (" + OrientationName() +
					       "; profile: " + core::DescribeStreamState(configured) +
					       "): " + core::DescribeViolations(result.violations));
		retryLive_ = request;
		done(std::move(result));
		return;
	}

	StoreForm(request);
	lastLiveRequest_ = request;
	startingLive_ = true;
	NotifyStateChanged();

	// streaming/start does not keep the form for the next live; saving it
	// first does, as the Save button would.
	api_->SaveStreamingSettings(
		token_, {request.settings.title, request.settings.pushNotification, request.channelId},
		[this, request, done = std::move(done)](ApiError error) mutable {
			if (HandleSessionError(error) || !error.ok() || !loggedIn_) {
				Log(LogLevel::Warning,
				    "saving the go-live form failed (ret_code '" + error.code + "'); not going live");
				FinishStart(std::move(error), std::move(done));
				return;
			}
			StartWithCover(request, EffectiveCoverUrl(), std::move(done));
		});
}

void PluginController::StartWithCover(const GoLiveRequest &request, std::string coverUrl,
				      std::function<void(StartLiveResult)> done)
{
	StartStreamingRequest start{std::move(coverUrl), request.settings.title, request.settings.pushNotification,
				    request.channelId,   request.mainEventId,    request.subEventId};
	api_->StartStreaming(token_, start,
			     [this, token = token_, start, done = std::move(done)](ApiError error,
										   StreamTarget target) mutable {
				     if (error.ok() && !loggedIn_ && !target.liveId.empty()) {
					     // Logged out while the backend was opening the live: close it.
					     Log(LogLevel::Info, "logged out while going live: closing live_id '" +
									 target.liveId + "' on the backend");
					     api_->CloseStreaming(token, target.liveId,
								  [this, liveId = target.liveId](ApiError closeError) {
									  FinishClose(liveId, closeError);
								  });
				     }
				     if (HandleSessionError(error) || !error.ok() || !loggedIn_) {
					     FinishStart(std::move(error), std::move(done));
					     return;
				     }

				     backendLiveOpen_ = true;
				     backendLiveId_ = target.liveId;
				     // The room has the cover now: the next streaming/info shows it.
				     if (!start.coverUrl.empty())
					     serverCoverUrl_ = start.coverUrl;
				     if (!target.liveId.empty())
					     store_.SaveOpenLiveId(target.liveId);
				     Log(target.liveId.empty() ? LogLevel::Warning : LogLevel::Info,
					 "live opened on the backend, live_id '" + target.liveId + "'");
				     // The mock backend returns no target: OBS keeps its configured
				     // server, so local testing streams where it always did.
				     if (!target.serverUrl.empty()) {
					     host_.SetStreamTarget(target.serverUrl, target.streamKey);
					     streamKeySet_ = true;
				     }
				     startRequestedByPlugin_ = true;
				     host_.StartStreaming();
				     // OBS refuses some starts right away (OnStreamStartFailed has
				     // run by now): nothing is streaming.
				     if (!startRequestedByPlugin_ && liveSource_ == LiveSource::None) {
					     FinishStart({}, std::move(done), true);
					     return;
				     }
				     FinishStart({}, std::move(done));
			     });
}

void PluginController::FinishStart(ApiError error, std::function<void(StartLiveResult)> done, bool obsStartFailed)
{
	startingLive_ = false;
	NotifyStateChanged();
	StartLiveResult result;
	result.error = std::move(error);
	result.obsStartFailed = obsStartFailed;
	done(std::move(result));
}

void PluginController::EndLive()
{
	if (liveSource_ != LiveSource::Plugin)
		return;
	endRequestedByPlugin_ = true;
	host_.StopStreaming();
}

core::Scenario PluginController::CurrentScenario() const
{
	return core::MatchScenario(host_.ActiveStreamState(), orientation_).value_or(settings_.scenario);
}

bool PluginController::ApplyRecommendedSettings(core::Scenario scenario)
{
	settings_.scenario = scenario;
	store_.Save(settings_);
	return host_.ApplySpec(core::GetStreamSpec(scenario, orientation_));
}

bool PluginController::ApplySettingsAndStart(core::Scenario scenario, std::function<void(StartLiveResult)> done)
{
	if (!retryLive_ || !ApplyRecommendedSettings(scenario)) {
		Log(LogLevel::Warning, "could not apply the recommended settings");
		return false;
	}

	Log(LogLevel::Info, "applied the recommended settings; going live again");
	GoLiveRequest request = *std::exchange(retryLive_, std::nullopt);
	request.settings.scenario = scenario;
	StartLive(request, std::move(done));
	return true;
}

void PluginController::OnStreamingStarting()
{
	// Streams started with OBS's own Start Streaming are left alone: the
	// plugin cannot tell whether they go to LangLive.
	if (loggedIn_ && startRequestedByPlugin_) {
		host_.UseProfileCodecs();
		host_.SetSimpleModeKeyframeInterval(core::kKeyframeIntervalSeconds);
	}
}

std::vector<core::Violation> PluginController::OnStreamingStarted()
{
	const bool byPlugin = std::exchange(startRequestedByPlugin_, false);

	if (loggedIn_ && byPlugin) {
		const core::StreamState active = host_.ActiveStreamState();
		std::vector<core::Violation> violations = core::Validate(active, orientation_);
		if (!violations.empty()) {
			Log(LogLevel::Warning, "pre-stream check stopped the stream (" + OrientationName() +
						       "; encoder: " + core::DescribeStreamState(active) +
						       "): " + core::DescribeViolations(violations));
			retryLive_ = lastLiveRequest_;
			host_.StopStreaming();
			return violations;
		}
	}

	liveSource_ = byPlugin ? LiveSource::Plugin : LiveSource::External;
	Log(LogLevel::Info, byPlugin ? "live started from the plugin" : "live started from OBS");
	NotifyStateChanged();
	return {};
}

void PluginController::OnStreamStartFailed()
{
	if (!startRequestedByPlugin_)
		return; // not the plugin's stream, or it started already
	Log(LogLevel::Warning, "OBS could not start the stream: ending the live");
	OnStreamingStopped();
}

void PluginController::OnStreamReconnecting()
{
	// TODO: tell the backend how long OBS will keep retrying, or agree on a
	// grace period, so the live room is kept while reconnecting.
	if (liveSource_ == LiveSource::None || reconnecting_)
		return;
	reconnecting_ = true;
	NotifyStateChanged();
}

void PluginController::OnStreamReconnected()
{
	if (!reconnecting_)
		return;
	reconnecting_ = false;
	NotifyStateChanged();
}

bool PluginController::OnStreamingStopped()
{
	startRequestedByPlugin_ = false;
	reconnecting_ = false;
	const bool endedByPlugin = std::exchange(endRequestedByPlugin_, false);

	// The next live gets a new key from streaming/start; OBS's own Start
	// Streaming must not reuse this one.
	if (std::exchange(streamKeySet_, false))
		host_.ClearStreamKey();

	// Also when the stream never started (connection failed, or stopped by
	// the pre-stream check): the backend opened the live already.
	// Website stream keys are not ours to end.
	if (std::exchange(backendLiveOpen_, false) && loggedIn_) {
		const std::string liveId = std::exchange(backendLiveId_, {});
		Log(LogLevel::Info, "closing live_id '" + liveId + "' on the backend");
		closingLiveId_ = liveId;
		api_->CloseStreaming(token_, liveId, [this, liveId](ApiError error) {
			if (closingLiveId_ == liveId)
				closingLiveId_.clear();
			FinishClose(liveId, error);
			HandleSessionError(error);
		});
	}

	if (liveSource_ == LiveSource::None)
		return false;

	const bool pluginLive = liveSource_ == LiveSource::Plugin;
	liveSource_ = LiveSource::None;
	NotifyStateChanged();
	if (pluginLive && !endedByPlugin) {
		Log(LogLevel::Info, "the stream stopped outside the plugin: live ended");
		if (liveEndedOutside_)
			liveEndedOutside_();
	}
	return pluginLive;
}

bool PluginController::IsStalePluginLive(const StreamingInfo &info) const
{
	return info.live && info.platform == StreamingInfo::Platform::Plugin && !info.liveId.empty() &&
	       !backendLiveOpen_ && !startingLive_ && liveSource_ == LiveSource::None && closingLiveId_ != info.liveId;
}

void PluginController::CloseStaleLive(const std::string &liveId, std::function<void(ApiError)> done)
{
	Log(LogLevel::Info,
	    "closing live_id '" + liveId + "': open on the backend from the plugin, not streamed by this OBS");
	closingLiveId_ = liveId;
	api_->CloseStreaming(token_, liveId, [this, liveId, done = std::move(done)](ApiError error) {
		if (closingLiveId_ == liveId)
			closingLiveId_.clear();
		FinishClose(liveId, error);
		HandleSessionError(error);
		done(std::move(error));
	});
}

void PluginController::FinishClose(const std::string &liveId, const ApiError &error)
{
	if (error.ok()) {
		store_.SaveOpenLiveId({});
		return;
	}
	Log(LogLevel::Warning,
	    "could not close live_id '" + liveId + "': ret_code '" + error.code + "', " + error.message);
	// Not sent, or the login expired: the next OBS session tries again.
	// Any other answer (not this account's live, not an OBS live, already
	// closed) will not change by retrying.
	if (error.kind != ApiError::Kind::Network && error.kind != ApiError::Kind::SessionExpired)
		store_.SaveOpenLiveId({});
}

void PluginController::OnExit()
{
	if (std::exchange(streamKeySet_, false))
		host_.ClearStreamKey();
	const std::string liveId = backendLiveOpen_ ? backendLiveId_ : closingLiveId_;
	backendLiveOpen_ = false;
	backendLiveId_.clear();
	closingLiveId_.clear();
	if (liveId.empty() || !loggedIn_)
		return;

	Log(LogLevel::Info, "OBS is exiting: closing live_id '" + liveId + "' on the backend");
	const ApiError error = api_->CloseStreamingNow(token_, liveId, kExitCloseTimeoutSeconds);
	FinishClose(liveId, error);
	if (!error.ok())
		Log(LogLevel::Warning, "live_id '" + liveId + "' is closed the next time OBS starts");
}

void PluginController::CheckSavedLogin()
{
	if (!loggedIn_)
		return;
	if (!store_.LoadOpenLiveId().empty()) {
		RecoverUnclosedLive(); // checks the login with the same call
		return;
	}
	api_->FetchStreamingInfo(token_, [this](ApiError error, StreamingInfo) {
		if (HandleSessionError(error))
			return;
		if (!error.ok())
			Log(LogLevel::Warning,
			    "could not check the saved login: ret_code '" + error.code + "', " + error.message);
	});
}

void PluginController::RecoverUnclosedLive()
{
	const std::string stored = store_.LoadOpenLiveId();
	if (stored.empty() || !loggedIn_ || backendLiveOpen_ || startingLive_ || liveSource_ != LiveSource::None)
		return;

	api_->FetchStreamingInfo(token_, [this, stored](ApiError error, StreamingInfo info) {
		if (HandleSessionError(error))
			return; // kept: tried again after the next login
		if (!error.ok()) {
			Log(LogLevel::Warning, "could not check live_id '" + stored + "' left open: ret_code '" +
						       error.code + "', " + error.message);
			return;
		}
		// streaming/start answers a repeated start with the open live, so a
		// live started meanwhile may be this one: it is ours again.
		if (backendLiveOpen_ || startingLive_)
			return;
		if (!info.live || info.platform != StreamingInfo::Platform::Plugin || info.liveId != stored) {
			Log(LogLevel::Info, "live_id '" + stored + "' from the last OBS session is closed already");
			store_.SaveOpenLiveId({});
			return;
		}
		Log(LogLevel::Info, "closing live_id '" + stored + "', left open by the last OBS session");
		api_->CloseStreaming(token_, stored, [this, stored](ApiError closeError) {
			FinishClose(stored, closeError);
			HandleSessionError(closeError);
		});
	});
}

void PluginController::SetUpdateFeed(UpdateFeed *feed, std::string currentVersion)
{
	updateFeed_ = feed;
	currentVersion_ = std::move(currentVersion);
}

void PluginController::CheckForUpdates(std::function<void(UpdateCheck)> done)
{
	const std::optional<core::Version> current = core::ParseVersion(currentVersion_);
	if (!updateFeed_ || !current) {
		done({});
		return;
	}

	updateFeed_->FetchManifest([this, current = *current,
				    done = std::move(done)](std::optional<UpdateManifest> manifest) {
		UpdateCheck check;
		const std::optional<core::Version> latest = manifest ? core::ParseVersion(manifest->version)
								     : std::nullopt;
		const std::optional<core::Version> minSupported =
			manifest ? core::ParseVersion(manifest->minSupportedVersion) : std::nullopt;
		if (!latest || !minSupported || !IsValidSha256(manifest->installerSha256) ||
		    !core::IsAllowedUpdateUrl(manifest->installerUrl)) {
			done(check); // Failed
			return;
		}

		check.latestVersion = manifest->version;
		switch (core::EvaluateUpdate(current, *latest, *minSupported)) {
		case core::UpdateStatus::UpToDate:
			check.result = UpdateCheck::Result::UpToDate;
			break;
		case core::UpdateStatus::Available:
			check.result = UpdateCheck::Result::Available;
			break;
		case core::UpdateStatus::Required:
			check.result = UpdateCheck::Result::Required;
			break;
		}

		const bool wasRequired = std::exchange(updateRequired_, check.result == UpdateCheck::Result::Required);
		pendingUpdate_.reset();
		downloadedInstaller_.clear();
		if (check.result != UpdateCheck::Result::UpToDate)
			pendingUpdate_ = std::move(manifest);
		if (wasRequired != updateRequired_)
			NotifyStateChanged();
		done(check);
	});
}

void PluginController::DownloadUpdate(std::function<void(int percent)> progress,
				      std::function<void(InstallerDownload)> done)
{
	if (!updateFeed_ || !CanInstallUpdate()) {
		done(InstallerDownload::Failed);
		return;
	}

	const int generation = ++downloadGeneration_;
	downloadedInstaller_.clear();
	updateFeed_->DownloadInstaller(
		pendingUpdate_->installerUrl, pendingUpdate_->installerSha256, std::move(progress),
		[this, generation, done = std::move(done)](InstallerDownload result, std::string path) {
			if (generation != downloadGeneration_)
				return; // canceled, or replaced by a newer download
			if (result == InstallerDownload::Ok && path.empty())
				result = InstallerDownload::Failed;
			downloadedInstaller_ = result == InstallerDownload::Ok ? std::move(path) : std::string();
			done(result);
		});
}

void PluginController::CancelUpdateDownload()
{
	++downloadGeneration_;
	if (updateFeed_)
		updateFeed_->CancelDownload();
}

bool PluginController::LaunchUpdateInstaller()
{
	if (!updateFeed_ || downloadedInstaller_.empty() || liveSource_ != LiveSource::None)
		return false;
	return updateFeed_->LaunchInstaller(downloadedInstaller_);
}

void PluginController::SetStateChangedCallback(std::function<void()> callback)
{
	stateChanged_ = std::move(callback);
}

void PluginController::NotifyStateChanged()
{
	if (stateChanged_)
		stateChanged_();
}

} // namespace langlive::obsplugin
