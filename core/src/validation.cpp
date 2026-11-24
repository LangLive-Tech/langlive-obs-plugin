#include "langlive/obsplugin/core/validation.hpp"

#include <algorithm>
#include <cstdio>

namespace langlive::obsplugin::core {

namespace {

// Tolerates fractional frame rates such as 29.97.
constexpr double kFpsTolerance = 0.01;

} // namespace

std::optional<Scenario> MatchScenario(const StreamState &state, Orientation orientation)
{
	for (Scenario scenario : {Scenario::General, Scenario::HighQuality}) {
		const StreamSpec spec = GetStreamSpec(scenario, orientation);
		if (state.outputWidth == spec.width && state.outputHeight == spec.height)
			return scenario;
	}
	return std::nullopt;
}

std::vector<Violation> Validate(const StreamState &state, Orientation orientation)
{
	std::vector<Violation> violations;

	if (state.videoCodec != kVideoCodec)
		violations.push_back(Violation::UnsupportedVideoCodec);

	if (state.audioCodec != kAudioCodec)
		violations.push_back(Violation::UnsupportedAudioCodec);

	if (state.keyframeIntervalSeconds > kKeyframeIntervalSeconds)
		violations.push_back(Violation::KeyframeIntervalTooLong);
	else if (state.keyframeIntervalSeconds <= 0)
		violations.push_back(Violation::KeyframeIntervalZero);

	const std::optional<Scenario> scenario = MatchScenario(state, orientation);
	if (!scenario) {
		violations.push_back(orientation == Orientation::Portrait ? Violation::PortraitResolutionMismatch
									  : Violation::LandscapeResolutionMismatch);
		return violations;
	}

	const StreamSpec spec = GetStreamSpec(*scenario, orientation);
	const bool is720p = *scenario == Scenario::General;

	if (state.fps > spec.fps + kFpsTolerance)
		violations.push_back(is720p ? Violation::FpsTooHigh720p : Violation::FpsTooHigh1080p);

	if (state.bitrateUnbounded || state.bitrateKbps > spec.bitrateKbps)
		violations.push_back(is720p ? Violation::BitrateTooHigh720p : Violation::BitrateTooHigh1080p);

	return violations;
}

bool IsResolutionViolation(Violation violation)
{
	return violation == Violation::PortraitResolutionMismatch ||
	       violation == Violation::LandscapeResolutionMismatch;
}

bool HasResolutionViolation(const std::vector<Violation> &violations)
{
	return std::any_of(violations.begin(), violations.end(), IsResolutionViolation);
}

namespace {

const char *ViolationName(Violation violation)
{
	switch (violation) {
	case Violation::UnsupportedVideoCodec:
		return "unsupported_video_codec";
	case Violation::UnsupportedAudioCodec:
		return "unsupported_audio_codec";
	case Violation::KeyframeIntervalTooLong:
		return "keyframe_interval_too_long";
	case Violation::KeyframeIntervalZero:
		return "keyframe_interval_zero";
	case Violation::PortraitResolutionMismatch:
		return "portrait_resolution_mismatch";
	case Violation::LandscapeResolutionMismatch:
		return "landscape_resolution_mismatch";
	case Violation::FpsTooHigh720p:
		return "fps_too_high_720p";
	case Violation::FpsTooHigh1080p:
		return "fps_too_high_1080p";
	case Violation::BitrateTooHigh720p:
		return "bitrate_too_high_720p";
	case Violation::BitrateTooHigh1080p:
		return "bitrate_too_high_1080p";
	}
	return "unknown";
}

} // namespace

std::string DescribeStreamState(const StreamState &state)
{
	char text[192];
	std::snprintf(text, sizeof(text), "canvas %ux%u, output %ux%u %.4g fps, %s/%s, keyint %d s, %d kbps",
		      state.canvasWidth, state.canvasHeight, state.outputWidth, state.outputHeight, state.fps,
		      state.videoCodec.empty() ? "?" : state.videoCodec.c_str(),
		      state.audioCodec.empty() ? "?" : state.audioCodec.c_str(), state.keyframeIntervalSeconds,
		      state.bitrateKbps);
	return state.bitrateUnbounded ? std::string(text) + " (quality-based rate control)" : std::string(text);
}

std::string DescribeViolations(const std::vector<Violation> &violations)
{
	std::string text;
	for (Violation violation : violations) {
		if (!text.empty())
			text += ", ";
		text += ViolationName(violation);
	}
	return text;
}

} // namespace langlive::obsplugin::core
