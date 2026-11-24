#pragma once

#include "langlive/obsplugin/core/stream_spec.hpp"
#include "langlive/obsplugin/core/validation.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace langlive::obsplugin {

// Go-live form values kept between sessions: cover, title, push
// notification, and scenario. Events and channel are not stored, as
// required by the spec.
struct GoLiveSettings {
	// A cover uploaded here that the room does not have yet: the backend
	// takes it only with the next streaming/start. coverBaselineUrl is the
	// room's cover (streaming/info) when it was uploaded: once the room's
	// cover differs (changed on the website, or this one went live), the
	// room's wins and the upload is dropped. Set only by
	// PluginController::UseUploadedCover (the form's values are ignored);
	// cleared on login, since an upload belongs to its account.
	std::string uploadedCoverUrl;
	std::string coverBaselineUrl;
	std::string title;
	std::string pushNotification;
	core::Scenario scenario = core::Scenario::General;
};

// One entry of a backend-provided list (channels, events).
struct ChoiceOption {
	std::string id;
	std::string name;
};

// What the backend has for the go-live form: choices, and the title and push
// notification saved with Save (streaming/info). The channel the backend
// keeps is left out: the form does not remember it (spec 4.3).
struct GoLiveInfo {
	std::vector<ChoiceOption> channels;
	std::vector<ChoiceOption> events;
	std::string title;
	std::string pushNotification;
	// The cover the next live starts with, to show: one uploaded here and
	// not live yet, or else the room's (which the website can change too).
	// Empty if none.
	std::string coverUrl;
};

// Everything the go-live form submits.
struct GoLiveRequest {
	GoLiveSettings settings; // the cover is not taken from it (see GoLiveSettings)
	std::string channelId;   // required
	std::string mainEventId; // optional
	std::string subEventId;  // optional
};

// What the application layer needs from OBS. Implemented in src/obs.
class StreamingHost {
public:
	virtual ~StreamingHost() = default;

	// Server URL of the configured streaming service, or empty.
	virtual std::string StreamingServerUrl() const = 0;

	// Encoder and canvas state of the active streaming output.
	virtual core::StreamState ActiveStreamState() const = 0;

	// What the next stream will use according to the current profile, read
	// before any stream starts. In OBS's simple output mode the keyframe
	// interval is reported as the spec's, because the plugin sets it while
	// a checked stream starts (SetSimpleModeKeyframeInterval).
	virtual core::StreamState ConfiguredStreamState() const = 0;

	// Writes resolution, fps, keyframe interval, and bitrate into the
	// current profile, and switches the stream encoders to x264 and AAC if
	// the profile uses another video or audio codec (hardware H.264 is
	// kept). Returns false if OBS rejected the change.
	virtual bool ApplySpec(const core::StreamSpec &spec) = 0;

	// Called while a stream is starting, before SetSimpleModeKeyframeInterval.
	// OBS creates its encoders only at startup or when its settings are
	// saved, so after ApplySpec switched the codec, the stream output still
	// has the old encoders. Gives it new ones that match the profile, for
	// this stream; OBS uses the profile's own from its next restart. Returns
	// true if it replaced an encoder.
	virtual bool UseProfileCodecs() = 0;

	// Called while a stream is starting, after OBS configured the stream
	// encoder and before it starts. In OBS's simple output mode, which has
	// no keyframe interval setting, sets it for this stream only. Does
	// nothing in advanced mode: the streamer's own setting is checked and
	// fixed through the pre-stream check instead. Returns true if it
	// changed the interval.
	virtual bool SetSimpleModeKeyframeInterval(int seconds) = 0;

	// Points OBS's streaming service at the ingest URL and stream key the
	// backend returned for this live, and saves it like OBS's own settings.
	virtual void SetStreamTarget(const std::string &serverUrl, const std::string &streamKey) = 0;
	// Empties the stream key of OBS's streaming service, keeping the server:
	// once a plugin live ends, OBS's own Start Streaming cannot push to
	// LangLive without going through the plugin again.
	virtual void ClearStreamKey() = 0;

	virtual void StartStreaming() = 0;
	virtual void StopStreaming() = 0;
};

// A login kept across OBS sessions: the backend's token (valid for 7
// days) and the account's nickname.
struct SavedSession {
	std::string token;
	std::string nickname;
};

// Local persistence for GoLiveSettings and the login. Implemented in src/obs.
class SettingsStore {
public:
	virtual ~SettingsStore() = default;

	virtual GoLiveSettings Load() const = 0;
	virtual void Save(const GoLiveSettings &settings) = 0;

	virtual std::optional<SavedSession> LoadSession() const = 0;
	virtual void SaveSession(const SavedSession &session) = 0;
	virtual void ClearSession() = 0;

	// The live this plugin opened on the backend and has not closed yet,
	// kept across OBS sessions so a live left open by a crash or a forced
	// quit is closed the next time. Empty when none; saving "" clears it.
	virtual std::string LoadOpenLiveId() const = 0;
	virtual void SaveOpenLiveId(const std::string &liveId) = 0;

	// Where a cropped cover is written for its upload (removed afterwards).
	virtual std::string CoverImagePath() const = 0;
};

// The update manifest's entry for this platform.
struct UpdateManifest {
	std::string version;             // latest version
	std::string minSupportedVersion; // older versions must update before logging in
	std::string installerUrl;
	std::string installerSha256; // lowercase hex
};

// How an installer download ended.
enum class InstallerDownload {
	Ok,
	Failed,           // no file: network, server, or refused URL
	ChecksumMismatch, // downloaded, but not the file the manifest names
};

// Reads the update manifest and downloads installers. Implemented in
// src/obs. Callbacks run on the main thread.
class UpdateFeed {
public:
	virtual ~UpdateFeed() = default;

	// Downloads and parses the manifest; nullopt on any failure.
	virtual void FetchManifest(std::function<void(std::optional<UpdateManifest>)> done) = 0;

	// Downloads the installer and checks its SHA-256. Calls `done` with the
	// result and, when Ok, the local file path.
	virtual void DownloadInstaller(const std::string &url, const std::string &sha256,
				       std::function<void(int percent)> progress,
				       std::function<void(InstallerDownload, std::string path)> done) = 0;

	// Aborts the installer download in progress, if any: its `done` reports
	// Failed and the partial file is removed.
	virtual void CancelDownload() = 0;

	// Opens the downloaded installer. Returns false if it could not start.
	virtual bool LaunchInstaller(const std::string &path) = 0;
};

} // namespace langlive::obsplugin
