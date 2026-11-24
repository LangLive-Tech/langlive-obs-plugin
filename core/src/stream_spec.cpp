#include "langlive/obsplugin/core/stream_spec.hpp"

namespace langlive::obsplugin::core {

StreamSpec GetStreamSpec(Scenario scenario, Orientation orientation)
{
	const bool landscape = orientation == Orientation::Landscape;

	switch (scenario) {
	case Scenario::HighQuality:
		return landscape ? StreamSpec{1920, 1080, 30, 6000} : StreamSpec{1080, 1920, 30, 6000};
	case Scenario::General:
	default:
		return landscape ? StreamSpec{1280, 720, 25, 3000} : StreamSpec{720, 1280, 25, 3000};
	}
}

} // namespace langlive::obsplugin::core
