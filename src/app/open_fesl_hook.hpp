#pragma once

namespace openfeslhook::app
{
	/// Starts the local listeners and sends the game's look-ups of the hosts FESL games share to them; what fails is
	/// logged, and the game runs on without it.
	void start() noexcept;
}
