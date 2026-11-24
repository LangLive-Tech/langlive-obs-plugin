#pragma once

#include "app/ports.hpp"

namespace langlive::obsplugin::obs {

// StreamingHost backed by libobs and the OBS frontend API.
class ObsStreamingHost final : public StreamingHost {
public:
	std::string StreamingServerUrl() const override;
	core::StreamState ActiveStreamState() const override;
	core::StreamState ConfiguredStreamState() const override;
	bool ApplySpec(const core::StreamSpec &spec) override;
	bool UseProfileCodecs() override;
	bool SetSimpleModeKeyframeInterval(int seconds) override;
	void SetStreamTarget(const std::string &serverUrl, const std::string &streamKey) override;
	void ClearStreamKey() override;
	void StartStreaming() override;
	void StopStreaming() override;
};

} // namespace langlive::obsplugin::obs
