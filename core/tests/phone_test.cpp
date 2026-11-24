#include "langlive/obsplugin/core/phone.hpp"

#include <doctest/doctest.h>

using namespace langlive::obsplugin::core;

TEST_CASE("national number: one trunk 0 removed, digits only")
{
	CHECK(NationalNumber("0912345678") == "912345678");
	CHECK(NationalNumber("912345678") == "912345678");
	CHECK(NationalNumber("0912-345-678").empty());
	CHECK(NationalNumber("").empty());
}

TEST_CASE("mobile numbers: complete and valid for the country")
{
	CHECK(IsValidMobileNumber("+886", "0912345678"));
	CHECK(IsValidMobileNumber("+886", "912345678"));
	CHECK_FALSE(IsValidMobileNumber("+886", "091234567"));   // one digit short
	CHECK_FALSE(IsValidMobileNumber("+886", "09123456789")); // one digit too many
	CHECK_FALSE(IsValidMobileNumber("+886", "0223456789"));  // a landline
	CHECK_FALSE(IsValidMobileNumber("+886", ""));

	CHECK(IsValidMobileNumber("+852", "91234567"));
	CHECK(IsValidMobileNumber("+853", "61234567"));
	CHECK(IsValidMobileNumber("+86", "13812345678"));
	CHECK(IsValidMobileNumber("+60", "0123456789"));
	CHECK(IsValidMobileNumber("+60", "01123456789"));
	CHECK(IsValidMobileNumber("+65", "81234567"));
	CHECK(IsValidMobileNumber("+81", "09012345678"));
	CHECK(IsValidMobileNumber("+82", "01012345678"));
	CHECK(IsValidMobileNumber("+82", "1012345678"));
	CHECK_FALSE(IsValidMobileNumber("+82", "0212345678")); // a Seoul landline
	CHECK(IsValidMobileNumber("+66", "0812345678"));
	CHECK(IsValidMobileNumber("+66", "912345678"));
	CHECK_FALSE(IsValidMobileNumber("+66", "021234567")); // a Bangkok landline
	CHECK(IsValidMobileNumber("+855", "012345678"));
	CHECK(IsValidMobileNumber("+855", "0961234567"));
	CHECK_FALSE(IsValidMobileNumber("+855", "1234567"));

	CHECK(IsValidMobileNumber("+44", "07912345678")); // another country: 6 to 15 digits
	CHECK_FALSE(IsValidMobileNumber("+44", "12345"));
}

TEST_CASE("cellphone: country code digits and the national number")
{
	CHECK(FormatCellphone("+886", "912345678") == "886912345678");
	CHECK(FormatCellphone("+886", "0912345678") == "886912345678");
	CHECK(FormatCellphone("+852", "91234567") == "85291234567");
	CHECK(FormatCellphone("+82", "01012345678") == "821012345678");
	CHECK(FormatCellphone("+855", "012345678") == "85512345678");
}
