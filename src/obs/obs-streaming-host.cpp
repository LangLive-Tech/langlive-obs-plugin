#include "obs/obs-streaming-host.hpp"

#include <obs-frontend-api.h>
#include <obs.h>
#include <util/config-file.h>

#include <plugin-support.h>

#include <cctype>
#include <cstring>
#include <string>

namespace langlive::obsplugin::obs {

namespace {

std::string ProfileFilePath(const char *fileName)
{
	char *profilePath = obs_frontend_get_current_profile_path();
	if (!profilePath)
		return {};
	std::string path = std::string(profilePath) + "/" + fileName;
	bfree(profilePath);
	return path;
}

// The encoders the plugin switches to when the profile uses another codec.
// x264 is available on every platform; hardware H.264 encoders are kept.
constexpr const char *kX264EncoderId = "obs_x264";
constexpr const char *kSimpleX264 = "x264";
constexpr const char *kSimpleAac = "aac";

bool EncoderTypeExists(const char *id)
{
	const char *type = nullptr;
	for (size_t i = 0; obs_enum_encoder_types(i, &type); ++i)
		if (type && std::strcmp(type, id) == 0)
			return true;
	return false;
}

// The AAC encoder OBS itself picks by default.
const char *AacEncoderId()
{
	for (const char *id : {"CoreAudio_AAC", "libfdk_aac"})
		if (EncoderTypeExists(id))
			return id;
	return "ffmpeg_aac";
}

std::string CodecOfEncoderId(const char *id)
{
	const char *codec = id && *id ? obs_get_encoder_codec(id) : nullptr;
	return codec ? codec : "";
}

std::string CodecOfEncoder(obs_encoder_t *encoder)
{
	const char *codec = encoder ? obs_encoder_get_codec(encoder) : nullptr;
	return codec ? codec : "";
}

// Simple output mode stores a preset name ("x264", "nvenc_hevc",
// "apple_h264", ...) instead of an encoder id; every name without "hevc"
// or "av1" is an H.264 encoder.
std::string SimpleVideoCodec(const char *name)
{
	const std::string value = name ? name : "";
	if (value.find("hevc") != std::string::npos)
		return "hevc";
	if (value.find("av1") != std::string::npos)
		return "av1";
	return core::kVideoCodec;
}

std::string SimpleAudioCodec(const char *name)
{
	return name && std::strcmp(name, "opus") == 0 ? "opus" : core::kAudioCodec;
}

bool IsAdvancedOutputMode(config_t *config)
{
	const char *mode = config_get_string(config, "Output", "Mode");
	return mode && std::strcmp(mode, "Advanced") == 0;
}

// Advanced output mode keeps the streaming encoder settings in
// streamEncoder.json; OBS reads it when it sets up the encoder for the
// next "Start Streaming". "keyint_sec" and "bitrate" are understood by
// every first-party H.264 encoder (x264, NVENC, QSV, AMF, VideoToolbox).
// With `newEncoder` the file is started over, because the old encoder's
// settings do not apply to x264.
bool WriteAdvancedEncoderSettings(int keyframeIntervalSeconds, int bitrateKbps, bool newEncoder)
{
	const std::string path = ProfileFilePath("streamEncoder.json");
	if (path.empty())
		return false;

	obs_data_t *data = newEncoder ? nullptr : obs_data_create_from_json_file_safe(path.c_str(), "bak");
	if (!data)
		data = obs_data_create();

	// Also for the streamer's own encoder: a quality-based rate control
	// (CRF, CQP) ignores "bitrate". Every first-party H.264 encoder takes
	// "CBR"; VideoToolbox falls back to ABR where it has no CBR.
	obs_data_set_string(data, "rate_control", "CBR");
	obs_data_set_int(data, "keyint_sec", keyframeIntervalSeconds);
	obs_data_set_int(data, "bitrate", bitrateKbps);
	const bool saved = obs_data_save_json_safe(data, path.c_str(), "tmp", "bak");
	obs_data_release(data);
	return saved;
}

// Rate controls that aim at a quality instead of a bitrate: x264 / VideoToolbox
// CRF, NVENC / QSV / AMF CQP, QSV ICQ, NVENC lossless.
bool IsQualityRateControl(obs_data_t *settings)
{
	std::string value = obs_data_get_string(settings, "rate_control");
	for (char &c : value)
		c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	for (const char *quality : {"CRF", "CQP", "ICQ", "LA_ICQ", "LOSSLESS"})
		if (value == quality)
			return true;
	return false;
}

// The advanced mode stream encoder's settings: its defaults with
// streamEncoder.json on top, as OBS applies them.
obs_data_t *AdvancedEncoderSettings(const char *encoderId)
{
	obs_data_t *settings = encoderId && *encoderId ? obs_encoder_defaults(encoderId) : nullptr;
	if (!settings)
		settings = obs_data_create();
	const std::string path = ProfileFilePath("streamEncoder.json");
	if (obs_data_t *saved = path.empty() ? nullptr : obs_data_create_from_json_file_safe(path.c_str(), "bak")) {
		obs_data_apply(settings, saved);
		obs_data_release(saved);
	}
	return settings;
}

} // namespace

std::string ObsStreamingHost::StreamingServerUrl() const
{
	// Borrowed reference: obs_frontend_get_streaming_service() does not
	// add a reference.
	obs_service_t *service = obs_frontend_get_streaming_service();
	if (!service)
		return {};

	obs_data_t *settings = obs_service_get_settings(service);
	const char *server = obs_data_get_string(settings, "server");
	std::string url = server ? server : "";
	obs_data_release(settings);
	return url;
}

core::StreamState ObsStreamingHost::ActiveStreamState() const
{
	core::StreamState state;

	obs_video_info ovi;
	if (obs_get_video_info(&ovi)) {
		state.canvasWidth = ovi.base_width;
		state.canvasHeight = ovi.base_height;
		state.outputWidth = ovi.output_width;
		state.outputHeight = ovi.output_height;
		state.fps = ovi.fps_den > 0 ? static_cast<double>(ovi.fps_num) / ovi.fps_den : 0.0;
	}

	obs_output_t *output = obs_frontend_get_streaming_output();
	if (!output)
		return state;

	if (obs_encoder_t *video = obs_output_get_video_encoder(output)) {
		const char *codec = obs_encoder_get_codec(video);
		state.videoCodec = codec ? codec : "";

		obs_data_t *settings = obs_encoder_get_settings(video);
		state.keyframeIntervalSeconds = static_cast<int>(obs_data_get_int(settings, "keyint_sec"));
		state.bitrateKbps = static_cast<int>(obs_data_get_int(settings, "bitrate"));
		state.bitrateUnbounded = IsQualityRateControl(settings);
		obs_data_release(settings);
	}

	if (obs_encoder_t *audio = obs_output_get_audio_encoder(output, 0)) {
		const char *codec = obs_encoder_get_codec(audio);
		state.audioCodec = codec ? codec : "";
	}

	obs_output_release(output);
	return state;
}

core::StreamState ObsStreamingHost::ConfiguredStreamState() const
{
	core::StreamState state;

	obs_video_info ovi;
	if (obs_get_video_info(&ovi)) {
		state.canvasWidth = ovi.base_width;
		state.canvasHeight = ovi.base_height;
		state.outputWidth = ovi.output_width;
		state.outputHeight = ovi.output_height;
		state.fps = ovi.fps_den > 0 ? static_cast<double>(ovi.fps_num) / ovi.fps_den : 0.0;
	}

	config_t *config = obs_frontend_get_profile_config();
	if (!config)
		return state;

	if (IsAdvancedOutputMode(config)) {
		const char *encoder = config_get_string(config, "AdvOut", "Encoder");
		state.videoCodec = CodecOfEncoderId(encoder);
		state.audioCodec = CodecOfEncoderId(config_get_string(config, "AdvOut", "AudioEncoder"));
		obs_data_t *settings = AdvancedEncoderSettings(encoder);
		state.keyframeIntervalSeconds = static_cast<int>(obs_data_get_int(settings, "keyint_sec"));
		state.bitrateKbps = static_cast<int>(obs_data_get_int(settings, "bitrate"));
		state.bitrateUnbounded = IsQualityRateControl(settings);
		obs_data_release(settings);
	} else {
		state.videoCodec = SimpleVideoCodec(config_get_string(config, "SimpleOutput", "StreamEncoder"));
		state.audioCodec = SimpleAudioCodec(config_get_string(config, "SimpleOutput", "StreamAudioEncoder"));
		state.keyframeIntervalSeconds = core::kKeyframeIntervalSeconds;
		state.bitrateKbps = static_cast<int>(config_get_uint(config, "SimpleOutput", "VBitrate"));
	}
	return state;
}

bool ObsStreamingHost::ApplySpec(const core::StreamSpec &spec)
{
	// OBS cannot change video settings while an output is running, and a
	// stream that is stopping may still be active.
	if (obs_frontend_streaming_active() || obs_frontend_recording_active() || obs_frontend_virtualcam_active()) {
		obs_log(LOG_WARNING, "ApplySpec: an output is active");
		return false;
	}

	config_t *config = obs_frontend_get_profile_config();
	if (!config) {
		obs_log(LOG_WARNING, "ApplySpec: no profile config");
		return false;
	}

	// Only the output: the canvas holds the streamer's scene layout, and
	// its aspect ratio is the streamer's choice (agreed with the PM).
	config_set_uint(config, "Video", "OutputCX", spec.width);
	config_set_uint(config, "Video", "OutputCY", spec.height);
	config_set_uint(config, "Video", "FPSType", 1); // integer FPS
	config_set_uint(config, "Video", "FPSInt", static_cast<uint64_t>(spec.fps));

	const bool advanced = IsAdvancedOutputMode(config);
	bool encoderSaved = true;
	if (advanced) {
		const bool newVideoEncoder = CodecOfEncoderId(config_get_string(config, "AdvOut", "Encoder")) !=
					     core::kVideoCodec;
		if (newVideoEncoder)
			config_set_string(config, "AdvOut", "Encoder", kX264EncoderId);
		if (CodecOfEncoderId(config_get_string(config, "AdvOut", "AudioEncoder")) != core::kAudioCodec)
			config_set_string(config, "AdvOut", "AudioEncoder", AacEncoderId());
		encoderSaved =
			WriteAdvancedEncoderSettings(core::kKeyframeIntervalSeconds, spec.bitrateKbps, newVideoEncoder);
	} else {
		if (SimpleVideoCodec(config_get_string(config, "SimpleOutput", "StreamEncoder")) != core::kVideoCodec)
			config_set_string(config, "SimpleOutput", "StreamEncoder", kSimpleX264);
		if (SimpleAudioCodec(config_get_string(config, "SimpleOutput", "StreamAudioEncoder")) !=
		    core::kAudioCodec)
			config_set_string(config, "SimpleOutput", "StreamAudioEncoder", kSimpleAac);
		// Simple output mode has no keyframe interval setting; the plugin
		// sets it when a stream starts (SetSimpleModeKeyframeInterval).
		config_set_uint(config, "SimpleOutput", "VBitrate", static_cast<uint64_t>(spec.bitrateKbps));
	}

	config_save_safe(config, "tmp", "bak");
	obs_frontend_reset_video();

	obs_video_info ovi;
	if (!obs_get_video_info(&ovi) || ovi.output_width != spec.width || ovi.output_height != spec.height) {
		obs_log(LOG_WARNING, "ApplySpec: OBS did not accept the new resolution (is an output active?)");
		return false;
	}

	obs_log(LOG_INFO, "applied %ux%u @ %d fps, %d kbps (%s output mode)", spec.width, spec.height, spec.fps,
		spec.bitrateKbps, advanced ? "advanced" : "simple");
	return encoderSaved;
}

bool ObsStreamingHost::UseProfileCodecs()
{
	obs_output_t *output = obs_frontend_get_streaming_output();
	if (!output)
		return false;

	const core::StreamState profile = ConfiguredStreamState();
	bool replaced = false;

	// The output takes its own reference to a new encoder, and releases it
	// when OBS sets up its own encoders for a later stream.
	obs_encoder_t *video = obs_output_get_video_encoder(output);
	if (video && profile.videoCodec == core::kVideoCodec && CodecOfEncoder(video) != core::kVideoCodec) {
		obs_data_t *settings = obs_data_create();
		obs_data_set_string(settings, "rate_control", "CBR");
		obs_data_set_int(settings, "bitrate", profile.bitrateKbps);
		obs_data_set_int(settings, "keyint_sec", core::kKeyframeIntervalSeconds);
		obs_encoder_t *x264 =
			obs_video_encoder_create(kX264EncoderId, "langlive_video_stream", settings, nullptr);
		obs_data_release(settings);
		if (x264) {
			obs_encoder_set_video(x264, obs_get_video());
			if (obs_encoder_scaling_enabled(video))
				obs_encoder_set_scaled_size(x264, obs_encoder_get_width(video),
							    obs_encoder_get_height(video));
			obs_output_set_video_encoder(output, x264);
			obs_encoder_release(x264);
			obs_log(LOG_INFO, "stream video encoder %s replaced by x264 for this stream",
				CodecOfEncoder(video).c_str());
			replaced = true;
		} else {
			obs_log(LOG_WARNING, "cannot create the x264 encoder");
		}
	}

	obs_encoder_t *audio = obs_output_get_audio_encoder(output, 0);
	if (audio && profile.audioCodec == core::kAudioCodec && CodecOfEncoder(audio) != core::kAudioCodec) {
		obs_data_t *current = obs_encoder_get_settings(audio);
		obs_data_t *settings = obs_data_create();
		if (const long long bitrate = obs_data_get_int(current, "bitrate"); bitrate > 0)
			obs_data_set_int(settings, "bitrate", bitrate);
		obs_data_release(current);
		const char *aacId = AacEncoderId();
		obs_encoder_t *aac = obs_audio_encoder_create(aacId, "langlive_audio_stream", settings,
							      obs_encoder_get_mixer_index(audio), nullptr);
		obs_data_release(settings);
		if (aac) {
			obs_encoder_set_audio(aac, obs_get_audio());
			obs_output_set_audio_encoder(output, aac, 0);
			obs_encoder_release(aac);
			obs_log(LOG_INFO, "stream audio encoder %s replaced by %s for this stream",
				CodecOfEncoder(audio).c_str(), aacId);
			replaced = true;
		} else {
			obs_log(LOG_WARNING, "cannot create the %s encoder", aacId);
		}
	}

	obs_output_release(output);
	return replaced;
}

bool ObsStreamingHost::SetSimpleModeKeyframeInterval(int seconds)
{
	obs_output_t *output = obs_frontend_get_streaming_output();
	if (!output)
		return false;

	bool changed = false;
	obs_encoder_t *video = obs_output_get_video_encoder(output);
	// The encoder OBS created tells the output mode actually in use; the
	// profile's "Mode" can differ until OBS recreates its outputs.
	const char *name = video ? obs_encoder_get_name(video) : nullptr;
	const bool simpleMode = name && std::strcmp(name, "simple_video_stream") == 0;
	// An active encoder is shared with a running recording (simple mode
	// "same as stream"); its keyframe interval cannot change now, and the
	// pre-stream check reports it instead.
	if (simpleMode && !obs_encoder_active(video)) {
		obs_data_t *settings = obs_encoder_get_settings(video);
		const long long current = obs_data_get_int(settings, "keyint_sec");
		if (current != seconds) {
			obs_log(LOG_INFO,
				"simple output mode: keyframe interval %lld s (0 = auto); set to %d s for this stream",
				current, seconds);
			obs_data_t *update = obs_data_create();
			obs_data_set_int(update, "keyint_sec", seconds);
			obs_encoder_update(video, update);
			obs_data_release(update);
			changed = true;
		}
		obs_data_release(settings);
	}
	obs_output_release(output);

	return changed;
}

void ObsStreamingHost::SetStreamTarget(const std::string &serverUrl, const std::string &streamKey)
{
	// A custom RTMP service with the backend's ingest and key, saved to the
	// profile's service.json like a change in OBS's Settings > Stream.
	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "server", serverUrl.c_str());
	obs_data_set_string(settings, "key", streamKey.c_str());
	obs_service_t *service = obs_service_create("rtmp_custom", "langlive_service", settings, nullptr);
	obs_data_release(settings);
	if (!service) {
		obs_log(LOG_WARNING, "SetStreamTarget: cannot create the streaming service");
		return;
	}
	obs_frontend_set_streaming_service(service);
	obs_frontend_save_streaming_service();
	obs_service_release(service);
	// The stream key is a secret: log the server only.
	obs_log(LOG_INFO, "streaming to %s", serverUrl.c_str());
}

void ObsStreamingHost::ClearStreamKey()
{
	obs_service_t *service = obs_frontend_get_streaming_service(); // borrowed
	if (!service)
		return;
	obs_data_t *settings = obs_service_get_settings(service);
	obs_data_set_string(settings, "key", "");
	obs_service_update(service, settings);
	obs_data_release(settings);
	obs_frontend_save_streaming_service();
	obs_log(LOG_INFO, "stream key cleared");
}

void ObsStreamingHost::StartStreaming()
{
	obs_frontend_streaming_start();
}

void ObsStreamingHost::StopStreaming()
{
	obs_frontend_streaming_stop();
}

} // namespace langlive::obsplugin::obs
