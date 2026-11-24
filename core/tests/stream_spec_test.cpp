#include "langlive/obsplugin/core/stream_spec.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <string>

using namespace langlive::obsplugin::core;

namespace {

void CheckSpec(const StreamSpec &spec, uint32_t width, uint32_t height, int fps, int bitrateKbps)
{
	CHECK(spec.width == width);
	CHECK(spec.height == height);
	CHECK(spec.fps == fps);
	CHECK(spec.bitrateKbps == bitrateKbps);
}

} // namespace

TEST_CASE("general scenario is 720p, 25 fps, 3000 kbps")
{
	CheckSpec(GetStreamSpec(Scenario::General, Orientation::Landscape), 1280, 720, 25, 3000);
	CheckSpec(GetStreamSpec(Scenario::General, Orientation::Portrait), 720, 1280, 25, 3000);
}

TEST_CASE("high-quality scenario is 1080p, 30 fps, 6000 kbps")
{
	CheckSpec(GetStreamSpec(Scenario::HighQuality, Orientation::Landscape), 1920, 1080, 30, 6000);
	CheckSpec(GetStreamSpec(Scenario::HighQuality, Orientation::Portrait), 1080, 1920, 30, 6000);
}

TEST_CASE("shared settings")
{
	CHECK(kKeyframeIntervalSeconds == 2);
	CHECK(std::string(kVideoCodec) == "h264");
	CHECK(std::string(kAudioCodec) == "aac");
}
