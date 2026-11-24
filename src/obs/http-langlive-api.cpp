#include "obs/http-langlive-api.hpp"

#include "langlive/obsplugin/core/update.hpp"
#include "obs/curl-http.hpp"
#include "obs/langlive-api-json.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <utility>

namespace langlive::obsplugin::obs {

namespace {

constexpr long kStallTimeoutSeconds = 15; // abort after 15 s without data
// Abort an API call after this long in all, even one that trickles data.
constexpr long kTotalTimeoutSeconds = 20;
constexpr size_t kMaxResponseBytes = 1024 * 1024;
// A cover is a JPEG of a few hundred KB; leave room for larger ones.
constexpr size_t kMaxImageBytes = 10 * 1024 * 1024;

// TODO: confirm with the backend; the documentation gives the routes only.
constexpr const char *kSendSmsCodePath = "/v6/obs_plugin/sms/send_code";
constexpr const char *kSmsLoginPath = "/v6/obs_plugin/sms/login";
constexpr const char *kStreamingInfoPath = "/v6/obs_plugin/streaming/info";
constexpr const char *kStreamingSettingPath = "/v6/obs_plugin/streaming/setting";
constexpr const char *kStreamingStartPath = "/v6/obs_plugin/streaming/start";
constexpr const char *kStreamingClosePath = "/v6/obs_plugin/streaming/close";
constexpr const char *kLogoutPath = "/v6/obs_plugin/logout";
constexpr const char *kUploadImagePath = "/v6/obs_plugin/upload/image";
// Documented as POST, but the backend answers 405 to POST: GET.
constexpr const char *kQrCreatePath = "/v6/obs_plugin/qrcode/create";
constexpr const char *kQrVerifyPath = "/v6/obs_plugin/qrcode/verify";

ApiError Failure(ApiError::Kind kind, const QString &message)
{
	return {kind, {}, message.toStdString()};
}

HttpRequest MakeRequest(const char *method, std::string url, const std::string &userAgent)
{
	HttpRequest request;
	request.method = method;
	request.url = std::move(url);
	request.headers.push_back("User-Agent: " + userAgent);
	request.stallTimeoutSeconds = kStallTimeoutSeconds;
	request.maxResponseBytes = kMaxResponseBytes;
	request.followRedirects = false; // see HttpRequest::followRedirects
	return request;
}

} // namespace

HttpLangLiveApi::HttpLangLiveApi(std::string baseUrl, std::string userAgent, std::string locale)
	: baseUrl_(std::move(baseUrl)),
	  userAgent_(std::move(userAgent)),
	  locale_(std::move(locale)),
	  http_(std::make_unique<CurlHttp>())
{
	while (!baseUrl_.empty() && baseUrl_.back() == '/')
		baseUrl_.pop_back();
}

HttpLangLiveApi::~HttpLangLiveApi() = default;

std::optional<HttpRequest> HttpLangLiveApi::BuildRequest(const char *path, const std::string &token,
							 const QJsonObject *body) const
{
	const std::string url = baseUrl_ + path;
	if (!core::IsAllowedUpdateUrl(url)) {
		obs_log(LOG_WARNING, "LangLive API URL must use HTTPS");
		return std::nullopt;
	}

	HttpRequest request = MakeRequest(body ? "POST" : "GET", url, userAgent_);
	request.totalTimeoutSeconds = kTotalTimeoutSeconds;
	request.headers.push_back("Content-Type: application/json");
	if (!token.empty())
		request.headers.push_back("X-Authorization-Token: " + token);
	if (!locale_.empty())
		request.headers.push_back("X-LOCAL: " + locale_);
	if (body)
		request.body = QJsonDocument(*body).toJson(QJsonDocument::Compact).toStdString();
	return request;
}

ApiError HttpLangLiveApi::Finish(const char *path, qint64 elapsedMs, const HttpResponse &response, QJsonObject *data,
				 bool poll)
{
	const int status = static_cast<int>(response.status);

	const ApiError result = api_json::ParseResponse(response.status, response.error,
							QByteArray::fromStdString(response.body), data);

	// Only the path, status and ret_code: requests and responses carry
	// tokens, stream keys and phone numbers. A poll waits quietly (every 2 s
	// for up to 5 minutes).
	if (poll && api_json::IsQrLoginPending(result.code))
		return result;
	if (result.ok())
		obs_log(LOG_INFO, "LangLive API %s: HTTP %d, ret_code '0' (%lld ms)", path, status,
			static_cast<long long>(elapsedMs));
	else
		obs_log(LOG_WARNING, "LangLive API %s failed: HTTP %d, ret_code '%s', %s (%lld ms)", path, status,
			result.code.c_str(), result.message.c_str(), static_cast<long long>(elapsedMs));
	return result;
}

void HttpLangLiveApi::Call(const char *path, const std::string &token, const QJsonObject *body,
			   std::function<void(ApiError, QJsonObject)> done, bool poll)
{
	std::optional<HttpRequest> request = BuildRequest(path, token, body);
	if (!request) {
		done(Failure(ApiError::Kind::Network, QStringLiteral("API URL must use HTTPS")), {});
		return;
	}

	QElapsedTimer timer;
	timer.start();
	http_->Send(std::move(*request), [path, timer, poll, done = std::move(done)](HttpResponse response) {
		QJsonObject data;
		ApiError result = Finish(path, timer.elapsed(), response, &data, poll);
		done(std::move(result), std::move(data));
	});
}

void HttpLangLiveApi::SendSmsCode(const std::string &cellphone, std::function<void(ApiError)> done)
{
	const QJsonObject body{{QStringLiteral("cellphone"), QString::fromStdString(cellphone)}};
	Call(kSendSmsCodePath, {}, &body, [done = std::move(done)](ApiError result, QJsonObject) { done(result); });
}

void HttpLangLiveApi::LoginWithSmsCode(const std::string &cellphone, const std::string &code,
				       std::function<void(ApiError, std::string, std::string)> done)
{
	const QJsonObject body{{QStringLiteral("cellphone"), QString::fromStdString(cellphone)},
			       {QStringLiteral("token"), QString::fromStdString(code)}};
	Call(kSmsLoginPath, {}, &body, [done = std::move(done)](ApiError result, QJsonObject data) {
		std::string token;
		std::string nickname;
		if (result.ok() &&
		    !api_json::ParseLogin(data, QString::fromStdString(result.message), &token, &nickname))
			result =
				Failure(ApiError::Kind::InvalidResponse, QStringLiteral("login response has no token"));
		done(result, token, nickname);
	});
}

void HttpLangLiveApi::FetchStreamingInfo(const std::string &token, std::function<void(ApiError, StreamingInfo)> done)
{
	Call(kStreamingInfoPath, token, nullptr, [done = std::move(done)](ApiError result, QJsonObject data) {
		StreamingInfo info;
		if (result.ok() && !api_json::ParseStreamingInfo(data, &info)) {
			obs_log(LOG_WARNING, "streaming/info: ret_code '0' without stream_info");
			result = Failure(ApiError::Kind::InvalidResponse, QStringLiteral("no stream_info"));
		}
		done(result, result.ok() ? info : StreamingInfo{});
	});
}

void HttpLangLiveApi::SaveStreamingSettings(const std::string &token, const StreamingSettings &settings,
					    std::function<void(ApiError)> done)
{
	const QJsonObject body = api_json::StreamingSettingsBody(settings);
	Call(kStreamingSettingPath, token, &body,
	     [done = std::move(done)](ApiError result, QJsonObject) { done(result); });
}

void HttpLangLiveApi::RequestCoverUpload(const std::string &token, const std::string &filename,
					 std::function<void(ApiError, CoverUpload)> done)
{
	const QJsonObject body{{QStringLiteral("filename"), QString::fromStdString(filename)}};
	Call(kUploadImagePath, token, &body, [done = std::move(done)](ApiError result, QJsonObject data) {
		CoverUpload upload;
		if (result.ok() && !api_json::ParseCoverUpload(data, &upload))
			result = Failure(ApiError::Kind::InvalidResponse, QStringLiteral("no upload URL"));
		done(result, upload);
	});
}

void HttpLangLiveApi::UploadCover(const CoverUpload &upload, const std::string &imagePath,
				  std::function<void(ApiError)> done)
{
	if (!core::IsAllowedUpdateUrl(upload.uploadUrl)) {
		done(Failure(ApiError::Kind::Network, QStringLiteral("upload URL must use HTTPS")));
		return;
	}
	QFile file(QString::fromStdString(imagePath));
	if (!file.open(QIODevice::ReadOnly)) {
		done(Failure(ApiError::Kind::Other, QStringLiteral("cannot read the cover image")));
		return;
	}

	// A pre-signed URL carries its credentials; no token is sent to S3. The
	// headers come from the backend; without them S3 would store the image
	// with curl's default form content type.
	HttpRequest request = MakeRequest("PUT", upload.uploadUrl, userAgent_);
	for (const auto &[name, value] : upload.headers)
		request.headers.push_back(name + ": " + value);
	request.body = file.readAll().toStdString();
	http_->Send(std::move(request), [done = std::move(done)](HttpResponse response) {
		if (!response.ok() || response.status < 200 || response.status >= 300) {
			// The URL's query string holds the signature; do not log it.
			obs_log(LOG_WARNING, "cover upload failed: HTTP %ld %s", response.status,
				response.error.c_str());
			done(Failure(ApiError::Kind::Network, response.ok()
								      ? QStringLiteral("HTTP %1").arg(response.status)
								      : QString::fromStdString(response.error)));
			return;
		}
		done({});
	});
}

void HttpLangLiveApi::DownloadImage(const std::string &url, std::function<void(ApiError, std::string)> done)
{
	if (!core::IsAllowedUpdateUrl(url)) {
		done(Failure(ApiError::Kind::Network, QStringLiteral("image URL must use HTTPS")), {});
		return;
	}
	// No token: the image is public (a CDN may redirect, within HTTPS).
	HttpRequest request = MakeRequest("GET", url, userAgent_);
	request.followRedirects = true;
	request.maxResponseBytes = kMaxImageBytes;
	http_->Send(std::move(request), [done = std::move(done)](HttpResponse response) {
		if (!response.ok() || response.status != 200) {
			obs_log(LOG_WARNING, "cover image download failed: HTTP %ld %s", response.status,
				response.error.c_str());
			done(Failure(ApiError::Kind::Network, response.ok()
								      ? QStringLiteral("HTTP %1").arg(response.status)
								      : QString::fromStdString(response.error)),
			     {});
			return;
		}
		done({}, std::move(response.body));
	});
}

void HttpLangLiveApi::StartStreaming(const std::string &token, const StartStreamingRequest &request,
				     std::function<void(ApiError, StreamTarget)> done)
{
	const QJsonObject body = api_json::StartStreamingBody(request);
	Call(kStreamingStartPath, token, &body, [done = std::move(done)](ApiError result, QJsonObject data) {
		StreamTarget target;
		if (result.ok() && !api_json::ParseStreamTarget(data, &target)) {
			// The backend has answered ret_code 0 with only an error in
			// ret_msg; log it, since the generic message hides it.
			obs_log(LOG_WARNING, "streaming/start: ret_code '0' without a stream target, ret_msg: %s",
				result.message.c_str());
			result = Failure(ApiError::Kind::InvalidResponse, QStringLiteral("no stream target"));
		}
		done(result, target);
	});
}

ApiError HttpLangLiveApi::CloseStreamingNow(const std::string &token, const std::string &liveId, int timeoutSeconds)
{
	const QJsonObject body{{QStringLiteral("live_id"), QString::fromStdString(liveId)}};
	std::optional<HttpRequest> request = BuildRequest(kStreamingClosePath, token, &body);
	if (!request)
		return Failure(ApiError::Kind::Network, QStringLiteral("API URL must use HTTPS"));
	request->totalTimeoutSeconds = timeoutSeconds;

	QElapsedTimer timer;
	timer.start();
	const HttpResponse response = http_->SendNow(*request);
	QJsonObject data;
	return Finish(kStreamingClosePath, timer.elapsed(), response, &data, false);
}

void HttpLangLiveApi::CloseStreaming(const std::string &token, const std::string &liveId,
				     std::function<void(ApiError)> done)
{
	const QJsonObject body{{QStringLiteral("live_id"), QString::fromStdString(liveId)}};
	Call(kStreamingClosePath, token, &body,
	     [done = std::move(done)](ApiError result, QJsonObject) { done(result); });
}

void HttpLangLiveApi::Logout(const std::string &token, std::function<void(ApiError)> done)
{
	const QJsonObject body; // POST without parameters
	Call(kLogoutPath, token, &body, [done = std::move(done)](ApiError result, QJsonObject) { done(result); });
}

void HttpLangLiveApi::CreateQrLogin(std::function<void(ApiError, QrLoginCode)> done)
{
	Call(kQrCreatePath, {}, nullptr, [done = std::move(done)](ApiError result, QJsonObject data) {
		QrLoginCode code;
		if (result.ok() && !api_json::ParseQrLoginCode(data, &code))
			result = Failure(ApiError::Kind::InvalidResponse, QStringLiteral("no QR code"));
		done(result, code);
	});
}

void HttpLangLiveApi::CheckQrLogin(const QrLoginCode &code, std::function<void(ApiError, QrLoginState)> done)
{
	const QJsonObject body{{QStringLiteral("uuid"), QString::fromStdString(code.uuid)},
			       {QStringLiteral("token"), QString::fromStdString(code.token)}};
	Call(
		kQrVerifyPath, {}, &body,
		[done = std::move(done)](ApiError result, QJsonObject data) {
			QrLoginState state;
			if (api_json::ParseQrLoginState(&result, data, &state))
				done({}, state);
			else
				done(result, {});
		},
		true);
}

} // namespace langlive::obsplugin::obs
