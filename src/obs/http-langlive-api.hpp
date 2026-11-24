#pragma once

#include "app/langlive-api.hpp"

#include "obs/curl-http.hpp"

#include <QtGlobal>

#include <memory>
#include <optional>
#include <string>

class QJsonObject;

namespace langlive::obsplugin::obs {

// The LangLive API over HTTPS (libcurl, see CurlHttp). `baseUrl` is the API domain
// ({DOMAIN} in the API documentation), set at build time; `locale` is the
// X-LOCAL header (see ApiLocale). Must be used on
// the main thread; callbacks run there.
class HttpLangLiveApi final : public LangLiveApi {
public:
	HttpLangLiveApi(std::string baseUrl, std::string userAgent, std::string locale);
	~HttpLangLiveApi() override;

	void SendSmsCode(const std::string &cellphone, std::function<void(ApiError)> done) override;
	void LoginWithSmsCode(const std::string &cellphone, const std::string &code,
			      std::function<void(ApiError, std::string, std::string)> done) override;
	void FetchStreamingInfo(const std::string &token, std::function<void(ApiError, StreamingInfo)> done) override;
	void SaveStreamingSettings(const std::string &token, const StreamingSettings &settings,
				   std::function<void(ApiError)> done) override;
	void RequestCoverUpload(const std::string &token, const std::string &filename,
				std::function<void(ApiError, CoverUpload)> done) override;
	void DownloadImage(const std::string &url, std::function<void(ApiError, std::string)> done) override;
	void UploadCover(const CoverUpload &upload, const std::string &imagePath,
			 std::function<void(ApiError)> done) override;
	void StartStreaming(const std::string &token, const StartStreamingRequest &request,
			    std::function<void(ApiError, StreamTarget)> done) override;
	void CloseStreaming(const std::string &token, const std::string &liveId,
			    std::function<void(ApiError)> done) override;
	ApiError CloseStreamingNow(const std::string &token, const std::string &liveId, int timeoutSeconds) override;
	void Logout(const std::string &token, std::function<void(ApiError)> done) override;
	void CreateQrLogin(std::function<void(ApiError, QrLoginCode)> done) override;
	void CheckQrLogin(const QrLoginCode &code, std::function<void(ApiError, QrLoginState)> done) override;

private:
	// GET (no body) or POST (JSON body) to {baseUrl}{path}; nullopt if the
	// URL is not HTTPS.
	std::optional<HttpRequest> BuildRequest(const char *path, const std::string &token,
						const QJsonObject *body) const;
	// The envelope's result and its data object, logged; for a poll, its
	// waiting answers are not.
	static ApiError Finish(const char *path, qint64 elapsedMs, const HttpResponse &response, QJsonObject *data,
			       bool poll);
	// Sends GET (no body) or POST (JSON body) to {baseUrl}{path} and calls
	// `done` with the envelope's result and its data object.
	void Call(const char *path, const std::string &token, const QJsonObject *body,
		  std::function<void(ApiError, QJsonObject)> done, bool poll = false);

	std::string baseUrl_;
	std::string userAgent_;
	std::string locale_;
	// Last: destroyed first, so no callback runs into a destroyed member.
	std::unique_ptr<CurlHttp> http_;
};

} // namespace langlive::obsplugin::obs
