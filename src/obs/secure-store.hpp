#pragma once

#include <optional>
#include <string>

namespace langlive::obsplugin::obs {

// Secrets in the operating system's per-user secure storage: a generic
// password in the login keychain (macOS, service "com.langlive.obs-plugin")
// or a generic credential in Credential Manager (Windows, target
// "LangLive OBS Plugin/<name>"). QA builds (LANGLIVE_QA_BUILD) use
// "com.langlive.obs-plugin.qa" and "LangLive OBS Plugin (QA)/<name>"
// instead. Both are per user and need no admin rights. On macOS the item belongs to OBS, which created it: if OBS's code
// signature changes, macOS asks the user once before letting OBS read it.
//
// `name` tells secrets apart (e.g. "session-token").

// False if the secret could not be stored (e.g. the user denied access).
bool SaveSecret(const std::string &name, const std::string &secret);
// nullopt if there is none, or it cannot be read.
std::optional<std::string> LoadSecret(const std::string &name);
// True if it is gone (or never existed).
bool DeleteSecret(const std::string &name);

} // namespace langlive::obsplugin::obs
