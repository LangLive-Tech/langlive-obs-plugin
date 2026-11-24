#include "langlive/obsplugin/core/redaction.hpp"

#include <regex>

namespace langlive::obsplugin::core {

namespace {

constexpr const char *kRedacted = "[REDACTED]";

// A field name ending in one of these is secret, in any case and with any
// prefix: key, stream_key, StreamKey, token, RefreshToken (OBS's basic.ini
// for Twitch / YouTube / Restream), bearer_token (WHIP), client_secret, ...
// Names that only start with one (keyint_sec) are left alone.
constexpr const char *kSecretNames = "[A-Za-z0-9_.-]*(?:key|token|secret|password|passwd|bearer|authorization)";

// RTMP URL query parameters that carry signatures.
constexpr const char *kSecretParams = "push_args|txsecret|txtime|sign|signature|token|auth_key";

} // namespace

std::string RedactSensitiveFields(const std::string &content)
{
	// "name": "value" (JSON)
	// The value may hold escaped quotes (\").
	static const std::regex jsonField(std::string("(\"") + kSecretNames + "\"\\s*:\\s*\")(?:[^\"\\\\]|\\\\.)*(\")",
					  std::regex::icase);
	// name=value (INI lines)
	static const std::regex iniField(std::string("(^|\\n)(\\s*") + kSecretNames + "\\s*=\\s*)[^\\r\\n]*",
					 std::regex::icase);
	// ?name=value or &name=value (URLs)
	static const std::regex urlParam(std::string("([?&](?:") + kSecretParams + ")=)[^&\\s\"']*", std::regex::icase);
	// Authorization: Bearer <token>
	static const std::regex bearer("(bearer\\s+)[A-Za-z0-9._~+/=-]+", std::regex::icase);

	std::string result = std::regex_replace(content, jsonField, std::string("$1") + kRedacted + "$2");
	result = std::regex_replace(result, iniField, std::string("$1$2") + kRedacted);
	result = std::regex_replace(result, urlParam, std::string("$1") + kRedacted);
	result = std::regex_replace(result, bearer, std::string("$1") + kRedacted);
	return result;
}

} // namespace langlive::obsplugin::core
