#pragma once

#include "app/langlive-api.hpp"

#include <QByteArray>
#include <QJsonObject>

// Parsing and building the LangLive API's JSON, kept apart from the network
// code so it can be tested against the documented examples.

namespace langlive::obsplugin::obs::api_json {

// Reads the common envelope {ret_code, ret_msg, data}. On success `data`
// receives the data object (empty if there is none).
ApiError ParseEnvelope(const QByteArray &body, QJsonObject *data);

// A whole HTTP exchange: the envelope when the body has one (an error status
// may carry it too). Without one, a transport error or an HTTP error status
// (e.g. a gateway's 502 / 503 HTML page) is Network, a temporary failure;
// only a 2xx answer that is not the envelope is InvalidResponse.
ApiError ParseResponse(long httpStatus, const std::string &transportError, const QByteArray &body, QJsonObject *data);

// The data object of each response. Return false if a required field is
// missing.
bool ParseLogin(const QJsonObject &data, const QString &retMsg, std::string *token, std::string *nickname);
// False without stream_info: the live state would be unknown, not "not live".
bool ParseStreamingInfo(const QJsonObject &data, StreamingInfo *info);
bool ParseStreamTarget(const QJsonObject &data, StreamTarget *target);
bool ParseCoverUpload(const QJsonObject &data, CoverUpload *upload);
bool ParseQrLoginCode(const QJsonObject &data, QrLoginCode *code);
// qrcode/verify answers its states with ret_codes: 1802 / 1801 waiting, 0
// logged in, -1 expired. Returns true with `state` for those; false for an
// error, left in `envelope` (a login without a token becomes
// InvalidResponse).
bool ParseQrLoginState(ApiError *envelope, const QJsonObject &data, QrLoginState *state);
// 1801 and 1802: qrcode/verify is still waiting.
bool IsQrLoginPending(const std::string &retCode);

// Request bodies.
QJsonObject StartStreamingBody(const StartStreamingRequest &request);
QJsonObject StreamingSettingsBody(const StreamingSettings &settings);

} // namespace langlive::obsplugin::obs::api_json
