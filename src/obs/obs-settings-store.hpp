#pragma once

#include "app/ports.hpp"

namespace langlive::obsplugin::obs {

// Stores GoLiveSettings as JSON in the plugin's OBS config directory (its qa/
// folder in QA builds), and the login: its nickname there too, its token in
// the system's secure storage.
class ObsSettingsStore final : public SettingsStore {
public:
	GoLiveSettings Load() const override;
	void Save(const GoLiveSettings &settings) override;
	std::optional<SavedSession> LoadSession() const override;
	void SaveSession(const SavedSession &session) override;
	void ClearSession() override;
	std::string LoadOpenLiveId() const override;
	void SaveOpenLiveId(const std::string &liveId) override;
	std::string CoverImagePath() const override;
};

} // namespace langlive::obsplugin::obs
