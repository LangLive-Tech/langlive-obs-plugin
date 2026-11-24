#include "ui/text.hpp"

namespace langlive::obsplugin::ui {

QString ViolationText(core::Violation violation)
{
	using core::Violation;

	switch (violation) {
	case Violation::UnsupportedVideoCodec:
		return Text("Preflight.VideoCodec");
	case Violation::UnsupportedAudioCodec:
		return Text("Preflight.AudioCodec");
	case Violation::KeyframeIntervalTooLong:
		return Text("Preflight.KeyframeTooLong");
	case Violation::KeyframeIntervalZero:
		return Text("Preflight.KeyframeZero");
	case Violation::PortraitResolutionMismatch:
		return Text("Preflight.ResolutionPortrait");
	case Violation::LandscapeResolutionMismatch:
		return Text("Preflight.ResolutionLandscape");
	case Violation::FpsTooHigh720p:
		return Text("Preflight.Fps720p");
	case Violation::FpsTooHigh1080p:
		return Text("Preflight.Fps1080p");
	case Violation::BitrateTooHigh720p:
		return Text("Preflight.Bitrate720p");
	case Violation::BitrateTooHigh1080p:
		return Text("Preflight.Bitrate1080p");
	}
	return {};
}

} // namespace langlive::obsplugin::ui
