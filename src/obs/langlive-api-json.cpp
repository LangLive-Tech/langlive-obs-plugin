#include "obs/langlive-api-json.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

namespace langlive::obsplugin::obs::api_json {

namespace {

// The documentation mixes strings and numbers for codes and ids
// ("ret_code": "0", "topic_id": "84" vs 84); accept both. The backend sends
// ret_code as a string and ids as numbers.
QString StringOrNumber(const QJsonValue &value)
{
	if (value.isString())
		return value.toString();
	if (value.isDouble())
		return QString::number(value.toInteger());
	return {};
}

std::string Text(const QJsonObject &object, const char *key)
{
	return StringOrNumber(object.value(QLatin1String(key))).toStdString();
}

std::vector<ApiChoice> Choices(const QJsonValue &list, const char *idKey, const char *nameKey)
{
	std::vector<ApiChoice> choices;
	const QJsonArray items = list.toArray();
	for (const QJsonValue item : items) {
		const QJsonObject object = item.toObject();
		ApiChoice choice{Text(object, idKey), Text(object, nameKey)};
		if (!choice.id.empty())
			choices.push_back(std::move(choice));
	}
	return choices;
}

// Ids sent as numbers, as the API documents; empty means "not set".
QJsonValue NumberOrNull(const std::string &id)
{
	if (id.empty())
		return QJsonValue(QJsonValue::Null);
	bool ok = false;
	const qint64 number = QString::fromStdString(id).toLongLong(&ok);
	return ok ? QJsonValue(number) : QJsonValue(QString::fromStdString(id));
}

void SetIfNotEmpty(QJsonObject &object, const char *key, const std::string &value, bool asNumber)
{
	if (!value.empty())
		object.insert(QLatin1String(key),
			      asNumber ? NumberOrNull(value) : QJsonValue(QString::fromStdString(value)));
}

bool IsSafeHeaderValue(const std::string &value)
{
	for (const unsigned char c : value)
		if (c < 0x20 || c == 0x7f)
			return false;
	return true;
}

} // namespace

ApiError ParseEnvelope(const QByteArray &body, QJsonObject *data)
{
	QJsonParseError error;
	const QJsonDocument document = QJsonDocument::fromJson(body, &error);
	if (error.error != QJsonParseError::NoError || !document.isObject())
		return {ApiError::Kind::InvalidResponse, {}, "response is not a JSON object"};

	const QJsonObject root = document.object();
	const QString code = StringOrNumber(root.value(QStringLiteral("ret_code")));
	if (code.isEmpty())
		return {ApiError::Kind::InvalidResponse, {}, "response has no ret_code"};

	ApiError result{ApiErrorKindFromRetCode(code.toStdString()), code.toStdString(),
			root.value(QStringLiteral("ret_msg")).toString().toStdString()};
	if (data)
		*data = root.value(QStringLiteral("data")).toObject();
	return result;
}

bool ParseLogin(const QJsonObject &data, const QString &retMsg, std::string *token, std::string *nickname)
{
	*token = Text(data, "authorization_token");
	// TODO: ask the backend for a nickname field. Until then it is taken
	// from ret_msg, "[nickname], logged in" in Chinese.
	*nickname = Text(data, "nickname");
	if (nickname->empty()) {
		QString name = retMsg;
		// The Chinese for ", logged in", escaped to keep the source ASCII.
		const int suffix = static_cast<int>(name.indexOf(QStringView(u"\uFF0C\u767B\u5165\u6210\u529F")));
		if (suffix >= 0)
			name.truncate(suffix);
		*nickname = name.trimmed().toStdString();
	}
	return !token->empty();
}

ApiError ParseResponse(long httpStatus, const std::string &transportError, const QByteArray &body, QJsonObject *data)
{
	ApiError result = ParseEnvelope(body, data);
	if (result.kind != ApiError::Kind::InvalidResponse)
		return result;
	if (!transportError.empty())
		return {ApiError::Kind::Network, {}, transportError};
	if (httpStatus < 200 || httpStatus >= 300)
		return {ApiError::Kind::Network, {}, "HTTP " + std::to_string(httpStatus)};
	return result;
}

bool ParseStreamingInfo(const QJsonObject &data, StreamingInfo *out)
{
	const QJsonValue streamValue = data.value(QStringLiteral("stream_info"));
	if (!streamValue.isObject())
		return false;
	StreamingInfo &info = *out;
	info = {};
	info.coverUrl = Text(data, "liveimg");
	info.title = Text(data, "title");
	info.pushNotification = Text(data, "push_text");
	info.channelId = Text(data, "topic_id");
	info.channels = Choices(data.value(QStringLiteral("topic_list")), "topic_id", "topic_name");
	info.mainEventId = Text(data, "act1");
	info.subEventId = Text(data, "act2");
	info.events = Choices(data.value(QStringLiteral("act_list")), "tab_id", "name");

	// Numbers or strings, like every other field.
	const QJsonObject stream = streamValue.toObject();
	info.orientation = StringOrNumber(stream.value(QStringLiteral("direction"))) == QLatin1String("1")
				   ? core::Orientation::Landscape
				   : core::Orientation::Portrait;
	info.live = StringOrNumber(stream.value(QStringLiteral("status"))) == QLatin1String("1");
	const QString platform = StringOrNumber(stream.value(QStringLiteral("platform")));
	if (platform == QLatin1String("OBS_PLUGIN") || platform == QLatin1String("2"))
		info.platform = StreamingInfo::Platform::Plugin;
	else if (platform == QLatin1String("OBS_WEB") || platform == QLatin1String("1"))
		info.platform = StreamingInfo::Platform::Web;
	info.liveId = Text(stream, "live_id");
	return true;
}

bool ParseStreamTarget(const QJsonObject &data, StreamTarget *target)
{
	target->serverUrl = Text(data, "push_url");
	target->streamKey = Text(data, "push_code");
	target->liveId = Text(data.value(QStringLiteral("stream_info")).toObject(), "live_id");
	return !target->serverUrl.empty() && !target->streamKey.empty();
}

bool ParseCoverUpload(const QJsonObject &data, CoverUpload *upload)
{
	*upload = {};
	upload->uploadUrl = Text(data, "uploadUrl");
	upload->imageUrl = Text(data, "playUrl");

	// Only the documented headers, and only values that cannot split the
	// header line. Without a Content-Type, S3 would store curl's default
	// (a form type) and the image would not show as one.
	const QJsonObject headers = data.value(QStringLiteral("header")).toObject();
	for (const char *name : {"Content-Type", "Cache-Control"}) {
		const std::string value = Text(headers, name);
		if (value.empty())
			continue;
		if (!IsSafeHeaderValue(value))
			return false;
		upload->headers.emplace_back(name, value);
	}
	if (upload->headers.empty() || upload->headers.front().first != "Content-Type")
		upload->headers.insert(upload->headers.begin(), {"Content-Type", "image/jpeg"});
	return !upload->uploadUrl.empty() && !upload->imageUrl.empty();
}

bool ParseQrLoginCode(const QJsonObject &data, QrLoginCode *code)
{
	code->uuid = Text(data, "uuid");
	code->token = Text(data, "token");
	code->content = Text(data, "qrcode_data");
	return !code->uuid.empty() && !code->token.empty() && !code->content.empty();
}

bool IsQrLoginPending(const std::string &retCode)
{
	return retCode == "1801" || retCode == "1802";
}

bool ParseQrLoginState(ApiError *envelope, const QJsonObject &data, QrLoginState *state)
{
	*state = {};
	const std::string &code = envelope->code;
	if (code == "1802") {
		state->status = QrLoginState::Status::Waiting;
	} else if (code == "1801") {
		state->status = QrLoginState::Status::Scanned;
	} else if (code == "-1") {
		state->status = QrLoginState::Status::Expired;
	} else if (envelope->ok()) {
		state->status = QrLoginState::Status::LoggedIn;
		if (!ParseLogin(data, QString::fromStdString(envelope->message), &state->token, &state->nickname)) {
			*envelope = {ApiError::Kind::InvalidResponse, envelope->code, "login response has no token"};
			return false;
		}
	} else {
		return false;
	}
	return true;
}

QJsonObject StartStreamingBody(const StartStreamingRequest &request)
{
	QJsonObject body{
		{QStringLiteral("liveimg"), QString::fromStdString(request.coverUrl)},
		{QStringLiteral("title"), QString::fromStdString(request.title)},
		{QStringLiteral("topic_id"), NumberOrNull(request.channelId)},
		// Optional, but streaming/start used to fail with ret_code 24
		// without it; always sent, empty for no notification.
		{QStringLiteral("push_text"), QString::fromStdString(request.pushNotification)},
	};
	SetIfNotEmpty(body, "act1", request.mainEventId, true);
	SetIfNotEmpty(body, "act2", request.subEventId, true);
	return body;
}

QJsonObject StreamingSettingsBody(const StreamingSettings &settings)
{
	QJsonObject body{
		{QStringLiteral("title"), QString::fromStdString(settings.title)},
		{QStringLiteral("topic_id"), NumberOrNull(settings.channelId)},
		// Always sent, so clearing the notification clears it on the backend.
		{QStringLiteral("push_text"), QString::fromStdString(settings.pushNotification)},
	};
	return body;
}

} // namespace langlive::obsplugin::obs::api_json
