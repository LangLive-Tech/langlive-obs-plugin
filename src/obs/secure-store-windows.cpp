#include "obs/secure-store.hpp"

#include <windows.h>
#include <wincred.h>

#include <vector>

namespace langlive::obsplugin::obs {

namespace {

std::wstring Widen(const std::string &text)
{
	if (text.empty())
		return {};
	const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
	std::wstring result(static_cast<size_t>(size), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
	return result;
}

// QA builds keep their own credentials, so a QA login never reaches a
// production build on the same computer (and the other way round).
std::wstring Target(const std::string &name)
{
#if LANGLIVE_QA_BUILD
	return L"LangLive OBS Plugin (QA)/" + Widen(name);
#else
	return L"LangLive OBS Plugin/" + Widen(name);
#endif
}

} // namespace

bool SaveSecret(const std::string &name, const std::string &secret)
{
	std::wstring target = Target(name);
	std::wstring user = L"langlive-obs-plugin";
	std::vector<BYTE> blob(secret.begin(), secret.end());

	CREDENTIALW credential = {};
	credential.Type = CRED_TYPE_GENERIC;
	credential.TargetName = target.data();
	credential.UserName = user.data();
	credential.CredentialBlobSize = static_cast<DWORD>(blob.size());
	credential.CredentialBlob = blob.data();
	// This user on this computer; not carried to other computers by a
	// roaming profile.
	credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
	const bool saved = CredWriteW(&credential, 0) != FALSE;
	SecureZeroMemory(blob.data(), blob.size());
	return saved;
}

std::optional<std::string> LoadSecret(const std::string &name)
{
	PCREDENTIALW credential = nullptr;
	if (!CredReadW(Target(name).c_str(), CRED_TYPE_GENERIC, 0, &credential))
		return std::nullopt;
	std::string secret(reinterpret_cast<const char *>(credential->CredentialBlob), credential->CredentialBlobSize);
	CredFree(credential);
	return secret;
}

bool DeleteSecret(const std::string &name)
{
	return CredDeleteW(Target(name).c_str(), CRED_TYPE_GENERIC, 0) != FALSE || GetLastError() == ERROR_NOT_FOUND;
}

} // namespace langlive::obsplugin::obs
