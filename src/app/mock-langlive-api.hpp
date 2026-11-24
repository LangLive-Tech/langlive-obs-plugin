#pragma once

#include "app/langlive-api.hpp"

#include <map>
#include <string>

namespace langlive::obsplugin {

// Stands in for the LangLive backend in builds without an API URL (local
// development, UI tests). Follows the documented behavior without a
// network: every code logs in, at most 10 codes per number per Taiwan day,
// fixed channels and events, and no stream target (OBS keeps the server and
// key it has). A QR code is "scanned" at its third check (answered once, as
// the backend does) and confirmed at its fifth, so the QR login can be tried
// without the app. Answers right away.
class MockLangLiveApi : public LangLiveApi {
public:
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
	// Any token this mock issued, also in an earlier OBS session (the
	// plugin keeps the login across restarts).
	static bool ValidToken(const std::string &token) { return token.rfind(kTokenPrefix, 0) == 0; }
	static constexpr const char *kTokenPrefix = "mock-token-";

	std::map<std::string, int> smsSentToday_;
	long long smsDay_ = -1;
	StreamingSettings saved_;
	bool live_ = false;
	int qrCodesCreated_ = 0;
	std::map<std::string, int> qrChecks_; // by uuid
};

} // namespace langlive::obsplugin
