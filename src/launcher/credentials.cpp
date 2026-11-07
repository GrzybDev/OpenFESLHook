#include "pch.hpp"
#include "launcher/credentials.hpp"

#include <windows.h>
#include <wincred.h>

#include "platform/text.hpp"

namespace openfeslhook::launcher
{
	namespace
	{
		std::wstring target(const std::string_view account)
		{
			return L"OpenFESLHook/bfheroes/" + platform::widen(account);
		}
	}

	std::string readPassword(const std::string_view account)
	{
		PCREDENTIALW credential = nullptr;
		if (CredReadW(target(account).c_str(), CRED_TYPE_GENERIC, 0, &credential) == FALSE)
			return {};
		std::string password(reinterpret_cast<const char*>(credential->CredentialBlob),
		                     credential->CredentialBlobSize);
		SecureZeroMemory(credential->CredentialBlob, credential->CredentialBlobSize);
		CredFree(credential);
		return password;
	}

	void writePassword(const std::string_view account, const std::string_view password)
	{
		std::wstring name = target(account);
		std::wstring user = platform::widen(account);
		std::string blob(password);
		CREDENTIALW credential{.Type = CRED_TYPE_GENERIC,
		                       .TargetName = name.data(),
		                       .CredentialBlobSize = static_cast<DWORD>(blob.size()),
		                       .CredentialBlob = reinterpret_cast<LPBYTE>(blob.data()),
		                       .Persist = CRED_PERSIST_LOCAL_MACHINE,
		                       .UserName = user.data()};
		CredWriteW(&credential, 0);
		SecureZeroMemory(blob.data(), blob.size());
	}

	void deletePassword(const std::string_view account)
	{
		CredDeleteW(target(account).c_str(), CRED_TYPE_GENERIC, 0);
	}
}
