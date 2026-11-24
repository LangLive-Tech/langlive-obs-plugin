#include "app/langlive-api.hpp"

namespace langlive::obsplugin {

ApiError::Kind ApiErrorKindFromRetCode(const std::string &retCode)
{
	if (retCode == "0")
		return ApiError::Kind::None;
	if (retCode == "99300")
		return ApiError::Kind::TooFrequent;
	if (retCode == "99301")
		return ApiError::Kind::SmsLimitReached;
	if (retCode == "99302")
		return ApiError::Kind::NotEligible;
	if (retCode == "99303")
		return ApiError::Kind::SessionExpired;
	if (retCode == "99304")
		return ApiError::Kind::WebLiveActive;
	return ApiError::Kind::Other;
}

std::string ApiLocale(const std::string &obsLocale)
{
	if (obsLocale == "zh-TW" || obsLocale == "zh-CN")
		return obsLocale;
	return "en-US";
}

} // namespace langlive::obsplugin
