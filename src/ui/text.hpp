#pragma once

#include "langlive/obsplugin/core/validation.hpp"

#include <obs-module.h>

#include <QString>

namespace langlive::obsplugin::ui {

// Localized string from data/locale/*.ini.
inline QString Text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

// Message shown to the streamer for a pre-stream check violation.
QString ViolationText(core::Violation violation);

} // namespace langlive::obsplugin::ui
