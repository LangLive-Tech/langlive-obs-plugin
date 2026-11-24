#include "obs/obs-settings-store.hpp"

#include "obs/secure-store.hpp"

#include <obs-module.h>
#include <util/platform.h>

#include <plugin-support.h>

#include <string>

namespace langlive::obsplugin::obs {

namespace {

constexpr const char *kFileName = "go-live.json";
// The login: the nickname in session.json, the token in the system's secure
// storage (see secure-store.hpp). Versions up to 0.1.4 kept the token in
// session.json too; LoadSession moves it.
constexpr const char *kSessionFileName = "session.json";
constexpr const char *kTokenSecretName = "session-token";
constexpr const char *kOpenLiveFileName = "open-live.json";

// QA builds keep their files in a folder of their own, as they keep their
// own token (see secure-store.hpp): a QA login, open live, or uploaded cover
// never reaches a production build on the same computer, nor the other way
// round. qa-override.json stays in the plugin's config folder.
#if LANGLIVE_QA_BUILD
constexpr const char *kConfigDir = "qa/";
#else
constexpr const char *kConfigDir = "";
#endif

std::string ModuleConfigPath(const char *fileName)
{
	char *path = obs_module_config_path((std::string(kConfigDir) + fileName).c_str());
	if (!path)
		return {};
	std::string result = path;
	bfree(path);
	return result;
}

// obs_module_config_path() only builds the path; the directory does not
// exist on a fresh install.
void EnsureConfigDir()
{
	char *dir = obs_module_config_path(kConfigDir);
	if (dir) {
		os_mkdirs(dir);
		bfree(dir);
	}
}

void SaveNickname(const std::string &path, const std::string &nickname)
{
	EnsureConfigDir();
	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "nickname", nickname.c_str());
	if (!obs_data_save_json_safe(data, path.c_str(), "tmp", "bak"))
		obs_log(LOG_WARNING, "failed to save the login");
	obs_data_release(data);
	// The backup may still hold a token from an older version.
	os_unlink((path + ".bak").c_str());
}

} // namespace

GoLiveSettings ObsSettingsStore::Load() const
{
	GoLiveSettings settings;

	const std::string path = ModuleConfigPath(kFileName);
	if (path.empty())
		return settings;

	obs_data_t *data = obs_data_create_from_json_file_safe(path.c_str(), "bak");
	if (!data)
		return settings;

	settings.uploadedCoverUrl = obs_data_get_string(data, "uploaded_cover_url");
	settings.coverBaselineUrl = obs_data_get_string(data, "cover_baseline_url");
	settings.title = obs_data_get_string(data, "title");
	settings.pushNotification = obs_data_get_string(data, "push_notification");
	settings.scenario = obs_data_get_bool(data, "high_quality") ? core::Scenario::HighQuality
								    : core::Scenario::General;
	obs_data_release(data);
	return settings;
}

void ObsSettingsStore::Save(const GoLiveSettings &settings)
{
	const std::string path = ModuleConfigPath(kFileName);
	if (path.empty())
		return;

	EnsureConfigDir();

	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "uploaded_cover_url", settings.uploadedCoverUrl.c_str());
	obs_data_set_string(data, "cover_baseline_url", settings.coverBaselineUrl.c_str());
	obs_data_set_string(data, "title", settings.title.c_str());
	obs_data_set_string(data, "push_notification", settings.pushNotification.c_str());
	obs_data_set_bool(data, "high_quality", settings.scenario == core::Scenario::HighQuality);
	if (!obs_data_save_json_safe(data, path.c_str(), "tmp", "bak"))
		obs_log(LOG_WARNING, "failed to save %s", path.c_str());
	obs_data_release(data);
}

std::optional<SavedSession> ObsSettingsStore::LoadSession() const
{
	const std::string path = ModuleConfigPath(kSessionFileName);
	obs_data_t *data = path.empty() ? nullptr : obs_data_create_from_json_file_safe(path.c_str(), "bak");
	if (!data)
		return std::nullopt;
	SavedSession session{obs_data_get_string(data, "token"), obs_data_get_string(data, "nickname")};
	obs_data_release(data);

	if (!session.token.empty()) {
		// Saved by an older version, in plain text: move it.
		if (SaveSecret(kTokenSecretName, session.token)) {
			SaveNickname(path, session.nickname);
			obs_log(LOG_INFO, "login moved to the system's secure storage");
		} else {
			obs_log(LOG_WARNING, "cannot move the login to the system's secure storage");
		}
		return session;
	}

	std::optional<std::string> token = LoadSecret(kTokenSecretName);
	if (!token || token->empty()) {
		obs_log(LOG_INFO, "no saved login in the system's secure storage");
		return std::nullopt;
	}
	obs_log(LOG_INFO, "login restored from the system's secure storage");
	session.token = std::move(*token);
	return session;
}

void ObsSettingsStore::SaveSession(const SavedSession &session)
{
	const std::string path = ModuleConfigPath(kSessionFileName);
	if (path.empty())
		return;
	// Never in a file: without secure storage the login lasts until OBS
	// exits, and the streamer logs in again next time.
	if (!SaveSecret(kTokenSecretName, session.token))
		obs_log(LOG_WARNING, "cannot save the login to the system's secure storage; it lasts until OBS exits");
	SaveNickname(path, session.nickname);
}

void ObsSettingsStore::ClearSession()
{
	if (!DeleteSecret(kTokenSecretName))
		obs_log(LOG_WARNING, "cannot remove the login from the system's secure storage");
	const std::string path = ModuleConfigPath(kSessionFileName);
	if (path.empty())
		return;
	os_unlink(path.c_str());
	os_unlink((path + ".bak").c_str());
}

std::string ObsSettingsStore::LoadOpenLiveId() const
{
	const std::string path = ModuleConfigPath(kOpenLiveFileName);
	obs_data_t *data = path.empty() ? nullptr : obs_data_create_from_json_file_safe(path.c_str(), "bak");
	if (!data)
		return {};
	std::string liveId = obs_data_get_string(data, "live_id");
	obs_data_release(data);
	return liveId;
}

void ObsSettingsStore::SaveOpenLiveId(const std::string &liveId)
{
	const std::string path = ModuleConfigPath(kOpenLiveFileName);
	if (path.empty())
		return;
	if (liveId.empty()) {
		os_unlink(path.c_str());
		os_unlink((path + ".bak").c_str());
		return;
	}
	EnsureConfigDir();
	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "live_id", liveId.c_str());
	if (!obs_data_save_json_safe(data, path.c_str(), "tmp", "bak"))
		obs_log(LOG_WARNING, "could not save %s", path.c_str());
	obs_data_release(data);
}

std::string ObsSettingsStore::CoverImagePath() const
{
	EnsureConfigDir();
	return ModuleConfigPath("cover-upload.jpg");
}

} // namespace langlive::obsplugin::obs
