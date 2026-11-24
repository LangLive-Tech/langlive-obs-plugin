#pragma once

#include <cstdint>

namespace langlive::obsplugin::core {

// Streaming scenario chosen by the streamer. Each scenario maps to one
// encoder spec per orientation (see GetStreamSpec).
enum class Scenario {
	General,     // Chat / general streaming (recommended default)
	HighQuality, // Performance / high-quality streaming
};

// The streamer's account orientation, configured by LangLive operations.
enum class Orientation {
	Landscape,
	Portrait,
};

struct StreamSpec {
	uint32_t width = 0;
	uint32_t height = 0;
	int fps = 0;
	int bitrateKbps = 0;
};

// Shared by every scenario.
inline constexpr int kKeyframeIntervalSeconds = 2;
inline constexpr const char *kVideoCodec = "h264";
inline constexpr const char *kAudioCodec = "aac";

StreamSpec GetStreamSpec(Scenario scenario, Orientation orientation);

} // namespace langlive::obsplugin::core
