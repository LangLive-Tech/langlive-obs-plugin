#pragma once

#include <string>

namespace langlive::obsplugin::core {

// Best-effort removal of secrets from diagnostic text before it is
// exported: stream keys, passwords, tokens, and signed RTMP query
// parameters. Every match is replaced with "[REDACTED]".
std::string RedactSensitiveFields(const std::string &content);

} // namespace langlive::obsplugin::core
