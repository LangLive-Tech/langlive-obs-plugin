#pragma once

#include "langlive/obsplugin/core/stream_spec.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace langlive::obsplugin::core {

// Pre-stream check results. The enumerator order is the order in which
// violations are reported to the streamer.
enum class Violation {
	UnsupportedVideoCodec,       // video codec is not H.264
	UnsupportedAudioCodec,       // audio codec is not AAC
	KeyframeIntervalTooLong,     // keyframe interval > 2 s
	KeyframeIntervalZero,        // keyframe interval = 0 (auto)
	PortraitResolutionMismatch,  // portrait account, not 720x1280 / 1080x1920
	LandscapeResolutionMismatch, // landscape account, not 1280x720 / 1920x1080
	FpsTooHigh720p,              // 720p output above 25 fps
	FpsTooHigh1080p,             // 1080p output above 30 fps
	BitrateTooHigh720p,          // 720p output above 3000 kbps, or not limited
	BitrateTooHigh1080p,         // 1080p output above 6000 kbps, or not limited
};

// Encoder and canvas state, gathered by the caller from OBS.
struct StreamState {
	// The canvas (OBS's base resolution), for logs only: its aspect ratio
	// is the streamer's choice (agreed with the PM). 0: unknown.
	uint32_t canvasWidth = 0;
	uint32_t canvasHeight = 0;
	uint32_t outputWidth = 0;
	uint32_t outputHeight = 0;
	double fps = 0.0;
	std::string videoCodec; // e.g. "h264", "hevc", "av1"
	std::string audioCodec; // e.g. "aac", "opus"
	int keyframeIntervalSeconds = 0;
	int bitrateKbps = 0;
	// The encoder's rate control aims at a quality (CRF, CQP, ...), not a
	// bitrate: bitrateKbps is not a limit then, so the bitrate check fails.
	bool bitrateUnbounded = false;
};

// Returns every violation, in reporting order. Empty means the stream may
// start.
//
// FPS and bitrate limits depend on the output resolution, so they are only
// checked when the resolution is valid. When it is not, applying the
// recommended settings for a scenario fixes fps and bitrate as well.
std::vector<Violation> Validate(const StreamState &state, Orientation orientation);

// The scenario whose resolution matches the output for this orientation,
// or nullopt if the resolution matches none.
std::optional<Scenario> MatchScenario(const StreamState &state, Orientation orientation);

bool IsResolutionViolation(Violation violation);
bool HasResolutionViolation(const std::vector<Violation> &violations);

// For logs: "canvas 1920x1080, output 1920x1080 30 fps, h264/aac, keyint 2 s,
// 6000 kbps" and
// "keyframe_interval_zero, bitrate_too_high_720p".
std::string DescribeStreamState(const StreamState &state);
std::string DescribeViolations(const std::vector<Violation> &violations);

} // namespace langlive::obsplugin::core
