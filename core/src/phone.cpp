#include "langlive/obsplugin/core/phone.hpp"

#include <algorithm>
#include <cctype>

namespace langlive::obsplugin::core {

namespace {

bool AllDigits(const std::string &text)
{
	return std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

struct MobileRule {
	const char *countryCode;
	size_t minDigits;
	size_t maxDigits;
	const char *leadingDigits; // first digit of a mobile number, any if empty
};

// National mobile numbers, without the trunk 0.
constexpr MobileRule kMobileRules[] = {
	{"+886", 9, 9, "9"},        // Taiwan: 9xx xxx xxx
	{"+852", 8, 8, "45679"},    // Hong Kong
	{"+853", 8, 8, "6"},        // Macau
	{"+86", 11, 11, "1"},       // mainland China
	{"+60", 9, 10, "1"},        // Malaysia: 1x xxx xxxx(x)
	{"+65", 8, 8, "89"},        // Singapore
	{"+81", 10, 10, "789"},     // Japan: 70 / 80 / 90 xxxx xxxx
	{"+82", 9, 10, "1"},        // South Korea: 10 xxxx xxxx (older 1x xxx xxxx)
	{"+66", 9, 9, "689"},       // Thailand: 6x / 8x / 9x xxx xxxx
	{"+855", 8, 9, "13456789"}, // Cambodia: xx xxx xxx(x)
};

} // namespace

std::string NationalNumber(const std::string &number)
{
	if (!AllDigits(number))
		return {};
	return !number.empty() && number[0] == '0' ? number.substr(1) : number;
}

bool IsValidMobileNumber(const std::string &countryCode, const std::string &number)
{
	const std::string national = NationalNumber(number);
	for (const MobileRule &rule : kMobileRules) {
		if (countryCode != rule.countryCode)
			continue;
		const std::string leading = rule.leadingDigits;
		return national.size() >= rule.minDigits && national.size() <= rule.maxDigits &&
		       (leading.empty() || leading.find(national[0]) != std::string::npos);
	}
	return national.size() >= 6 && national.size() <= 15;
}

std::string FormatCellphone(const std::string &countryCode, const std::string &number)
{
	std::string cellphone;
	for (const char c : countryCode)
		if (c >= '0' && c <= '9')
			cellphone += c;
	return cellphone + NationalNumber(number);
}

} // namespace langlive::obsplugin::core
