#pragma once

#include <string>
#include <string_view>

namespace openfeslhook::launcher
{
	/// Reads the password Remember me stored for an account in the current user's Windows Credential Manager.
	/// @param account The account name the password belongs to.
	/// @returns The password, or empty when none is stored.
	[[nodiscard]] std::string readPassword(std::string_view account);

	/// Stores an account's password in the current user's Windows Credential Manager, replacing an earlier one.
	/// @param account The account name the password belongs to.
	/// @param password The password.
	void writePassword(std::string_view account, std::string_view password);

	/// Removes the password stored for an account, if there is one.
	/// @param account The account name the password belongs to.
	void deletePassword(std::string_view account);
}
