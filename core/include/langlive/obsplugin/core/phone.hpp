#pragma once

#include <string>

namespace langlive::obsplugin::core {

// The number without the trunk prefix: one leading 0, which is dialled only
// within the country (0912345678 -> 912345678). Empty if `number` holds
// anything but digits.
std::string NationalNumber(const std::string &number);

// True if `number` (with or without its trunk 0) is a complete mobile
// number for the country code: the right length and leading digit for the
// login dialog's countries (+886, +86, +852, +853, +65, +60, +81, +82, +66,
// +855);
// 6 to 15 digits for any other.
bool IsValidMobileNumber(const std::string &countryCode, const std::string &number);

// The `cellphone` field of the LangLive API: the country code's digits and
// the national number, e.g. 886912345678 for +886 0912345678. The backend
// refuses a '+', and finds no account for a number without its country code.
std::string FormatCellphone(const std::string &countryCode, const std::string &number);

} // namespace langlive::obsplugin::core
