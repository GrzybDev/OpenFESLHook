#include "pch.hpp"

// Asio starts Winsock from global objects, which in a DLL run under the loader lock, where Winsock must not start.
// An object in the library initialization segment runs before all of them and tells Asio that the hook starts
// Winsock itself, which app::start does on the hook's own thread.
#pragma warning(push)
#pragma warning(disable : 4073)
#pragma init_seg(lib)
const boost::asio::detail::winsock_init<>::manual gManualWinsockStartup;
#pragma warning(pop)
