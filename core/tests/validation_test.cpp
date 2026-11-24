#include "langlive/obsplugin/core/validation.hpp"

#include <doctest/doctest.h>

#include <utility>

using namespace langlive::obsplugin::core;
using Violations = std::vector<Violation>;

namespace {

StreamState ValidState(uint32_t width, uint32_t height, double fps, int bitrateKbps)
{
	StreamState state;
	state.outputWidth = width;
	state.outputHeight = height;
	state.fps = fps;
	state.videoCodec = "h264";
	state.audioCodec = "aac";
	state.keyframeIntervalSeconds = 2;
	state.bitrateKbps = bitrateKbps;
	return state;
}

} // namespace

TEST_CASE("valid settings pass for every scenario and orientation")
{
	CHECK(Validate(ValidState(1280, 720, 25, 3000), Orientation::Landscape).empty());
	CHECK(Validate(ValidState(1920, 1080, 30, 6000), Orientation::Landscape).empty());
	CHECK(Validate(ValidState(720, 1280, 25, 3000), Orientation::Portrait).empty());
	CHECK(Validate(ValidState(1080, 1920, 30, 6000), Orientation::Portrait).empty());
}

TEST_CASE("lower fps and bitrate than the spec are allowed")
{
	CHECK(Validate(ValidState(1280, 720, 24, 2500), Orientation::Landscape).empty());
	CHECK(Validate(ValidState(1920, 1080, 29.97, 4500), Orientation::Landscape).empty());
}

TEST_CASE("codecs")
{
	StreamState state = ValidState(1280, 720, 25, 3000);

	state.videoCodec = "hevc";
	CHECK(Validate(state, Orientation::Landscape) == Violations{Violation::UnsupportedVideoCodec});

	state.videoCodec = "h264";
	state.audioCodec = "opus";
	CHECK(Validate(state, Orientation::Landscape) == Violations{Violation::UnsupportedAudioCodec});
}

TEST_CASE("keyframe interval")
{
	StreamState state = ValidState(1280, 720, 25, 3000);

	SUBCASE("1 second is allowed")
	{
		state.keyframeIntervalSeconds = 1;
		CHECK(Validate(state, Orientation::Landscape).empty());
	}
	SUBCASE("longer than 2 seconds")
	{
		state.keyframeIntervalSeconds = 5;
		CHECK(Validate(state, Orientation::Landscape) == Violations{Violation::KeyframeIntervalTooLong});
	}
	SUBCASE("0 (auto)")
	{
		state.keyframeIntervalSeconds = 0;
		CHECK(Validate(state, Orientation::Landscape) == Violations{Violation::KeyframeIntervalZero});
	}
}

TEST_CASE("resolution must match the account orientation")
{
	CHECK(Validate(ValidState(720, 1280, 25, 3000), Orientation::Landscape) ==
	      Violations{Violation::LandscapeResolutionMismatch});
	CHECK(Validate(ValidState(1280, 720, 25, 3000), Orientation::Portrait) ==
	      Violations{Violation::PortraitResolutionMismatch});
	CHECK(Validate(ValidState(2560, 1440, 30, 6000), Orientation::Landscape) ==
	      Violations{Violation::LandscapeResolutionMismatch});
	CHECK(Validate(ValidState(854, 480, 25, 1000), Orientation::Landscape) ==
	      Violations{Violation::LandscapeResolutionMismatch});
}

TEST_CASE("fps limit depends on resolution")
{
	CHECK(Validate(ValidState(1280, 720, 30, 3000), Orientation::Landscape) ==
	      Violations{Violation::FpsTooHigh720p});
	CHECK(Validate(ValidState(1920, 1080, 60, 6000), Orientation::Landscape) ==
	      Violations{Violation::FpsTooHigh1080p});
}

TEST_CASE("bitrate limit depends on resolution")
{
	CHECK(Validate(ValidState(1280, 720, 25, 3001), Orientation::Landscape) ==
	      Violations{Violation::BitrateTooHigh720p});
	CHECK(Validate(ValidState(1920, 1080, 30, 8000), Orientation::Landscape) ==
	      Violations{Violation::BitrateTooHigh1080p});
}

TEST_CASE("all violations are reported in order")
{
	StreamState state = ValidState(1280, 720, 60, 8000);
	state.videoCodec = "av1";
	state.audioCodec = "opus";
	state.keyframeIntervalSeconds = 10;

	CHECK(Validate(state, Orientation::Landscape) ==
	      Violations{Violation::UnsupportedVideoCodec, Violation::UnsupportedAudioCodec,
			 Violation::KeyframeIntervalTooLong, Violation::FpsTooHigh720p, Violation::BitrateTooHigh720p});
}

TEST_CASE("fps and bitrate are not reported when the resolution is invalid")
{
	StreamState state = ValidState(2560, 1440, 60, 20000);
	state.keyframeIntervalSeconds = 0;

	const Violations violations = Validate(state, Orientation::Landscape);
	CHECK(violations == Violations{Violation::KeyframeIntervalZero, Violation::LandscapeResolutionMismatch});
	CHECK(HasResolutionViolation(violations));
}

TEST_CASE("HasResolutionViolation")
{
	CHECK_FALSE(HasResolutionViolation({}));
	CHECK_FALSE(HasResolutionViolation({Violation::UnsupportedVideoCodec, Violation::FpsTooHigh720p}));
	CHECK(HasResolutionViolation({Violation::PortraitResolutionMismatch}));
}

TEST_CASE("MatchScenario")
{
	CHECK(MatchScenario(ValidState(1280, 720, 60, 9000), Orientation::Landscape) == Scenario::General);
	CHECK(MatchScenario(ValidState(1080, 1920, 60, 9000), Orientation::Portrait) == Scenario::HighQuality);
	CHECK_FALSE(MatchScenario(ValidState(1920, 1080, 30, 6000), Orientation::Portrait).has_value());
}

TEST_CASE("stream state and violations are described for logs")
{
	StreamState state;
	state.outputWidth = 1920;
	state.outputHeight = 1080;
	state.fps = 29.97;
	state.videoCodec = "hevc";
	state.keyframeIntervalSeconds = 0;
	state.bitrateKbps = 8000;
	CHECK(DescribeStreamState(state) == "canvas 0x0, output 1920x1080 29.97 fps, hevc/?, keyint 0 s, 8000 kbps");
	CHECK(DescribeViolations({Violation::UnsupportedVideoCodec, Violation::BitrateTooHigh1080p}) ==
	      "unsupported_video_codec, bitrate_too_high_1080p");
	CHECK(DescribeViolations({}).empty());
}

TEST_CASE("canvas: any aspect ratio is the streamer's choice and is not checked")
{
	StreamState portrait = ValidState(720, 1280, 25, 3000);
	portrait.canvasWidth = 1920; // landscape canvas on a portrait account
	portrait.canvasHeight = 1080;
	CHECK(Validate(portrait, Orientation::Portrait).empty());

	StreamState landscape = ValidState(1280, 720, 25, 3000);
	landscape.canvasWidth = 1440; // 4:3
	landscape.canvasHeight = 1080;
	CHECK(Validate(landscape, Orientation::Landscape).empty());
}

TEST_CASE("bitrate: a quality-based rate control is not limited, whatever its bitrate setting")
{
	StreamState state = ValidState(1280, 720, 25, 2500);
	REQUIRE(Validate(state, Orientation::Landscape).empty());
	state.bitrateUnbounded = true; // e.g. x264 CRF in advanced output mode
	CHECK(Validate(state, Orientation::Landscape) == Violations{Violation::BitrateTooHigh720p});
	CHECK(DescribeStreamState(state).find("quality-based rate control") != std::string::npos);

	StreamState hd = ValidState(1920, 1080, 30, 6000);
	hd.bitrateUnbounded = true;
	CHECK(Validate(hd, Orientation::Landscape) == Violations{Violation::BitrateTooHigh1080p});
}
