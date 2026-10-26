#include "pch.hpp"
#include "launcher/dialog.hpp"

#include <thread>
#include <utility>

#include <commctrl.h>
#include <dwmapi.h>

#include "platform/text.hpp"
#include "resource.h"

namespace openfeslhook::launcher
{
	namespace
	{
		// The message the sign-in worker posts back to the dialog; its wParam owns the job it carries.
		constexpr UINT kSignedIn = WM_APP + 1;

		// The game's languages: the `+lang` code the launch passes and the string that names it. The first stands for
		// the system's language, which is resolved only when the game is launched.
		struct Language
		{
			const char* code;
			UINT name;
		};

		constexpr Language kLanguages[] = {
			{"", IDS_LANG_SYSTEM},     {"en", IDS_LANG_ENGLISH},    {"de", IDS_LANG_GERMAN},
			{"fr", IDS_LANG_FRENCH},   {"it", IDS_LANG_ITALIAN},    {"pl", IDS_LANG_POLISH},
			{"pt", IDS_LANG_PORTUGUESE}, {"ru", IDS_LANG_RUSSIAN},  {"es", IDS_LANG_SPANISH},
			{"tr", IDS_LANG_TURKISH},  {"nl", IDS_LANG_DUTCH},      {"br", IDS_LANG_BRAZILIAN},
		};

		// The dialog's own state, reached through the window's user data for as long as it is open.
		struct State
		{
			HINSTANCE instance = nullptr;
			const relay::ServerAddress* server = nullptr;
			const Defaults* defaults = nullptr;
			const std::vector<std::string>* maps = nullptr;
			Choice choice;
			bool busy = false;
		};

		// What the sign-in worker needs and returns, owned by the worker until it posts the window back.
		struct SignInJob
		{
			HWND dialog = nullptr;
			relay::ServerAddress server;
			bool host = false;
			std::string account;
			std::string password;
			SessionResult result;
		};

		std::wstring loadString(const HINSTANCE instance, const UINT id)
		{
			const wchar_t* text = nullptr;
			const int length = LoadStringW(instance, id, reinterpret_cast<wchar_t*>(&text), 0);
			return length > 0 ? std::wstring(text, static_cast<std::size_t>(length)) : std::wstring();
		}

		std::wstring windowText(const HWND control)
		{
			const int length = GetWindowTextLengthW(control);
			if (length <= 0)
				return {};
			std::wstring text(static_cast<std::size_t>(length), L'\0');
			GetWindowTextW(control, text.data(), length + 1);
			return text;
		}

		void addItem(const HWND dialog, const int control, const std::wstring& text)
		{
			SendDlgItemMessageW(dialog, control, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
		}

		void setStatus(const State& state, const HWND dialog, const UINT id)
		{
			SetDlgItemTextW(dialog, IDC_STATUS, loadString(state.instance, id).c_str());
		}

		// The server names why it refused a sign-in by a code, so the text the player reads stays in this string table.
		UINT refusalText(const std::string_view refusal)
		{
			static constexpr std::pair<std::string_view, UINT> texts[] = {
				{"wrong-sign-in", IDS_LAUNCHER_WRONG_SIGN_IN}, {"unknown-game", IDS_LAUNCHER_UNKNOWN_GAME},
				{"disabled", IDS_LAUNCHER_DISABLED},           {"banned", IDS_LAUNCHER_BANNED},
				{"deactivated", IDS_LAUNCHER_DEACTIVATED},     {"no-persona", IDS_LAUNCHER_NO_SOLDIER},
			};
			for (const auto& [code, text] : texts)
				if (code == refusal)
					return text;
			return IDS_LAUNCHER_FAILED;
		}

		void updateMode(const State& state, const HWND dialog)
		{
			const bool host = IsDlgButtonChecked(dialog, IDC_MODE_HOST) == BST_CHECKED;
			SetDlgItemTextW(dialog, IDOK, loadString(state.instance, host ? IDS_LAUNCHER_HOST : IDS_LAUNCHER_PLAY).c_str());
			const int show = host ? SW_SHOW : SW_HIDE;
			for (const int control : {IDC_MAP_LABEL, IDC_MAP, IDC_RANKED})
				ShowWindow(GetDlgItem(dialog, control), show);
		}

		void enableControls(const HWND dialog, const bool on)
		{
			for (const int control :
			     {IDOK, IDCANCEL, IDC_MODE_PLAY, IDC_MODE_HOST, IDC_ACCOUNT, IDC_PASSWORD, IDC_REMEMBER, IDC_LANGUAGE,
			      IDC_MAP, IDC_RANKED})
				EnableWindow(GetDlgItem(dialog, control), on ? TRUE : FALSE);
		}

		std::string languageCode(const HWND dialog)
		{
			const LRESULT selected = SendDlgItemMessageW(dialog, IDC_LANGUAGE, CB_GETCURSEL, 0, 0);
			if (selected < 0 || selected >= static_cast<LRESULT>(std::size(kLanguages)))
				return {};
			return kLanguages[selected].code;
		}

		void prefill(State& state, const HWND dialog)
		{
			const Defaults& defaults = *state.defaults;
			for (const Language& language : kLanguages)
				addItem(dialog, IDC_LANGUAGE, loadString(state.instance, language.name));
			LRESULT language = 0;
			for (std::size_t i = 0; i < std::size(kLanguages); ++i)
				if (defaults.language == kLanguages[i].code)
					language = static_cast<LRESULT>(i);
			SendDlgItemMessageW(dialog, IDC_LANGUAGE, CB_SETCURSEL, static_cast<WPARAM>(language), 0);

			// The first map item leaves the choice to the server's own map list (no +loadLevel); the rest are the
			// maps that list names, so the host can pin a starting one.
			addItem(dialog, IDC_MAP, loadString(state.instance, IDS_MAP_DEFAULT));
			for (const std::string& map : *state.maps)
				addItem(dialog, IDC_MAP, platform::widen(map));
			const LRESULT map = defaults.map.empty()
				                    ? 0
				                    : SendDlgItemMessageW(dialog, IDC_MAP, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
				                                          reinterpret_cast<LPARAM>(platform::widen(defaults.map).c_str()));
			SendDlgItemMessageW(dialog, IDC_MAP, CB_SETCURSEL, static_cast<WPARAM>(map < 0 ? 0 : map), 0);

			SetDlgItemTextW(dialog, IDC_MODE_PLAY,
			                (loadString(state.instance, IDS_TILE_PLAY) + L"\n" +
			                 loadString(state.instance, IDS_TILE_PLAY_SUB)).c_str());
			SetDlgItemTextW(dialog, IDC_MODE_HOST,
			                (loadString(state.instance, IDS_TILE_HOST) + L"\n" +
			                 loadString(state.instance, IDS_TILE_HOST_SUB)).c_str());
			SetDlgItemTextW(dialog, IDC_ACCOUNT, platform::widen(defaults.account).c_str());
			CheckDlgButton(dialog, IDC_REMEMBER, defaults.remember ? BST_CHECKED : BST_UNCHECKED);
			CheckRadioButton(dialog, IDC_MODE_PLAY, IDC_MODE_HOST,
			                 defaults.mode == Mode::Host ? IDC_MODE_HOST : IDC_MODE_PLAY);
			updateMode(state, dialog);
		}

		void startSignIn(State& state, const HWND dialog)
		{
			if (state.busy)
				return;
			const std::wstring account = windowText(GetDlgItem(dialog, IDC_ACCOUNT));
			const std::wstring password = windowText(GetDlgItem(dialog, IDC_PASSWORD));
			if (account.empty() || password.empty())
			{
				setStatus(state, dialog, IDS_LAUNCHER_NEED_ACCOUNT);
				MessageBeep(MB_ICONWARNING);
				return;
			}

			state.busy = true;
			enableControls(dialog, false);
			setStatus(state, dialog, IDS_LAUNCHER_CONNECTING);

			auto* const job = new SignInJob{.dialog = dialog,
			                                .server = *state.server,
			                                .host = IsDlgButtonChecked(dialog, IDC_MODE_HOST) == BST_CHECKED,
			                                .account = platform::narrow(account),
			                                .password = platform::narrow(password)};
			std::thread(
				[job]
				{
					job->result = requestSession(job->server, job->host, job->account, job->password);
					PostMessageW(job->dialog, kSignedIn, reinterpret_cast<WPARAM>(job), 0);
				})
				.detach();
		}

		void finishSignIn(State& state, const HWND dialog, SignInJob& job)
		{
			state.busy = false;
			if (!job.result.session)
			{
				enableControls(dialog, true);
				updateMode(state, dialog); // the map controls follow the mode again
				if (job.result.problem.empty())
					setStatus(state, dialog, refusalText(job.result.refusal));
				else
					SetDlgItemTextW(dialog, IDC_STATUS, platform::widen(job.result.problem).c_str());
				return;
			}

			Choice& choice = state.choice;
			choice.mode = IsDlgButtonChecked(dialog, IDC_MODE_HOST) == BST_CHECKED ? Mode::Host : Mode::Play;
			choice.session = *job.result.session;
			choice.account = job.account;
			choice.password = job.password;
			choice.language = languageCode(dialog);
			// The first map item means "leave it to the server's map list", which the launch passes as no map at all.
			const LRESULT mapIndex = SendDlgItemMessageW(dialog, IDC_MAP, CB_GETCURSEL, 0, 0);
			choice.map = mapIndex <= 0 ? std::string() : platform::narrow(windowText(GetDlgItem(dialog, IDC_MAP)));
			choice.ranked = IsDlgButtonChecked(dialog, IDC_RANKED) == BST_CHECKED;
			choice.remember = IsDlgButtonChecked(dialog, IDC_REMEMBER) == BST_CHECKED;
			EndDialog(dialog, IDOK);
		}

		// Gives the window the current Windows' chrome where it is available: a Mica backdrop, rounded corners and a
		// title bar following the system's light or dark setting. The attributes are named by their number so the
		// build does not depend on the SDK version, and each is ignored on a Windows that lacks it.
		void applyModernFrame(const HWND dialog)
		{
			constexpr DWORD kImmersiveDarkMode = 20;
			constexpr DWORD kSystemBackdropType = 38;
			constexpr DWORD kWindowCornerPreference = 33;
			constexpr DWORD kBackdropMica = 2;
			constexpr DWORD kCornerRound = 2;

			DWORD light = 1;
			DWORD size = sizeof(light);
			RegGetValueW(HKEY_CURRENT_USER,
			             L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
			             L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
			const BOOL dark = light == 0 ? TRUE : FALSE;
			DwmSetWindowAttribute(dialog, kImmersiveDarkMode, &dark, sizeof(dark));
			const DWORD backdrop = kBackdropMica;
			DwmSetWindowAttribute(dialog, kSystemBackdropType, &backdrop, sizeof(backdrop));
			const DWORD corner = kCornerRound;
			DwmSetWindowAttribute(dialog, kWindowCornerPreference, &corner, sizeof(corner));
		}

		INT_PTR CALLBACK dialogProc(const HWND dialog, const UINT message, const WPARAM wParam, const LPARAM lParam)
		{
			auto* const state = reinterpret_cast<State*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));
			switch (message)
			{
			case WM_INITDIALOG:
			{
				auto* const initial = reinterpret_cast<State*>(lParam);
				SetWindowLongPtrW(dialog, GWLP_USERDATA, static_cast<LONG_PTR>(lParam));
				applyModernFrame(dialog);
				prefill(*initial, dialog);
				SetFocus(GetDlgItem(dialog, initial->defaults->account.empty() ? IDC_ACCOUNT : IDC_PASSWORD));
				return FALSE; // the focus was set here
			}
			case WM_COMMAND:
				switch (LOWORD(wParam))
				{
				case IDC_MODE_PLAY:
				case IDC_MODE_HOST:
					if (HIWORD(wParam) == BN_CLICKED)
						updateMode(*state, dialog);
					return TRUE;
				case IDOK:
					startSignIn(*state, dialog);
					return TRUE;
				case IDCANCEL:
					if (!state->busy)
						EndDialog(dialog, IDCANCEL);
					return TRUE;
				default:
					return FALSE;
				}
			case kSignedIn:
			{
				std::unique_ptr<SignInJob> job(reinterpret_cast<SignInJob*>(wParam));
				finishSignIn(*state, dialog, *job);
				return TRUE;
			}
			case WM_SYSCOMMAND:
				if ((wParam & 0xFFF0) == SC_CLOSE && state != nullptr && state->busy)
					return TRUE; // a sign-in is in flight; the window stays until it answers
				return FALSE;
			default:
				return FALSE;
			}
		}
	}

	std::optional<Choice> showLauncher(const HINSTANCE instance, const relay::ServerAddress& server,
	                                   const Defaults& defaults, const std::vector<std::string>& maps)
	{
		// The game's process is not manifested for Common-Controls 6, so without an activation context over the hook's
		// own manifest the launcher would draw in the classic, unthemed style.
		ACTCTXW manifest{.cbSize = sizeof(ACTCTXW),
		                 .dwFlags = ACTCTX_FLAG_HMODULE_VALID | ACTCTX_FLAG_RESOURCE_NAME_VALID,
		                 .lpResourceName = MAKEINTRESOURCEW(IDR_LAUNCHER_MANIFEST),
		                 .hModule = instance};
		const HANDLE context = CreateActCtxW(&manifest);
		ULONG_PTR cookie = 0;
		const bool activated = context != INVALID_HANDLE_VALUE && ActivateActCtx(context, &cookie) != FALSE;
		const INITCOMMONCONTROLSEX classes{.dwSize = sizeof(INITCOMMONCONTROLSEX), .dwICC = ICC_STANDARD_CLASSES};
		InitCommonControlsEx(&classes);

		State state{.instance = instance, .server = &server, .defaults = &defaults, .maps = &maps};
		const INT_PTR result = DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_LAUNCHER), nullptr, dialogProc,
		                                       reinterpret_cast<LPARAM>(&state));

		if (activated)
			DeactivateActCtx(0, cookie);
		if (context != INVALID_HANDLE_VALUE)
			ReleaseActCtx(context);
		if (result == IDOK)
			return state.choice;
		return std::nullopt;
	}
}
