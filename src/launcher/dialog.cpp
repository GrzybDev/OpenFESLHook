#include "pch.hpp"
#include "launcher/dialog.hpp"

#include <thread>
#include <utility>

#include <commctrl.h>
#include <dwmapi.h>

#include "launcher/website_links.hpp"
#include "platform/text.hpp"
#include "platform/translation.hpp"
#include "resource.h"

namespace openfeslhook::launcher
{
	namespace
	{
		// The messages the workers post back to the dialog; the wParam of each owns the job it carries.
		constexpr UINT kSignedIn = WM_APP + 1;
		constexpr UINT kLinksChecked = WM_APP + 2;

		// The game's languages: the `+lang` code the launch passes and the id of the text that names it. The first
		// stands for the system's language, which is resolved only when the game is launched.
		struct Language
		{
			std::string_view code;
			std::string_view name;
		};

		constexpr Language kLanguages[] = {
			{"", "launcher.language.system"}, {"en", "launcher.language.en"}, {"de", "launcher.language.de"},
			{"fr", "launcher.language.fr"},   {"it", "launcher.language.it"}, {"pl", "launcher.language.pl"},
			{"pt", "launcher.language.pt"},   {"ru", "launcher.language.ru"}, {"es", "launcher.language.es"},
			{"tr", "launcher.language.tr"},   {"nl", "launcher.language.nl"}, {"br", "launcher.language.br"},
		};

		// The dialog's own state, reached through the window's user data for as long as it is open.
		struct State
		{
			const i18n::Translation* texts = nullptr;
			const relay::ServerAddress* server = nullptr;
			const Defaults* defaults = nullptr;
			const std::vector<std::string>* maps = nullptr;
			const std::filesystem::path* exe = nullptr;
			const LaunchLink* link = nullptr;
			Choice choice;
			bool busy = false;
			bool checkingLinks = false;
		};

		// What the sign-in worker needs and returns, owned by the worker until it posts the window back.
		struct SignInJob
		{
			HWND dialog = nullptr;
			relay::ServerAddress server;
			bool host = false;
			std::string account;
			std::string password;
			// The code of the website link to sign in with in place of the account and password; empty for those.
			std::string code;
			SessionResult result;
		};

		enum class LinksChange
		{
			None,
			Register,
			Remove,
		};

		// What the website-link worker changes and finds, owned by the worker until it posts the window back.
		struct LinksJob
		{
			HWND dialog = nullptr;
			std::filesystem::path exe;
			LinksChange change = LinksChange::None;
			std::wstring name;
			std::string problem;
			bool here = false;
			// The other copy the links start, while it exists.
			std::optional<std::filesystem::path> elsewhere;
		};

		// Runs a job on a worker thread and posts it back to its dialog as a message, which then owns it; when the
		// dialog is gone, the job ends with the worker.
		template <typename Job, typename Work>
		void runOnWorker(const UINT message, Job job, Work work)
		{
			std::thread(
				[message, work, job = std::make_unique<Job>(std::move(job))]() mutable
				{
					work(*job);
					if (PostMessageW(job->dialog, message, reinterpret_cast<WPARAM>(job.get()), 0) != FALSE)
						job.release();
				})
				.detach();
		}

		std::wstring text(const State& state, const std::string_view id)
		{
			return platform::widen(state.texts->text(id));
		}

		void setText(const State& state, const HWND dialog, const int control, const std::string_view id)
		{
			SetDlgItemTextW(dialog, control, text(state, id).c_str());
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

		// The server names why it refused a sign-in by a code, so the text the player reads stays in the catalogues.
		std::string_view refusalText(const std::string_view refusal, const bool viaLink)
		{
			// A code that expired or was used already is refused like a wrong password.
			if (viaLink && refusal == "wrong-sign-in")
				return "launcher.refusal.link_expired";
			static constexpr std::pair<std::string_view, std::string_view> texts[] = {
				{"wrong-sign-in", "launcher.refusal.wrong_sign_in"}, {"unknown-game", "launcher.refusal.unknown_game"},
				{"disabled", "launcher.refusal.disabled"},           {"banned", "launcher.refusal.banned"},
				{"deactivated", "launcher.refusal.deactivated"},     {"no-persona", "launcher.refusal.no_soldier"},
			};
			for (const auto& [code, id] : texts)
				if (code == refusal)
					return id;
			return "launcher.status.failed";
		}

		void updateMode(const State& state, const HWND dialog)
		{
			const bool host = IsDlgButtonChecked(dialog, IDC_MODE_HOST) == BST_CHECKED;
			setText(state, dialog, IDOK, host ? "launcher.button.host" : "launcher.button.play");
			const int show = host ? SW_SHOW : SW_HIDE;
			for (const int control : {IDC_MAP_LABEL, IDC_MAP, IDC_RANKED})
				ShowWindow(GetDlgItem(dialog, control), show);
		}

		// Cancel is left out: it stays usable during a sign-in, whose answer then goes nowhere. The website-link option
		// also waits for its own worker.
		void enableControls(const State& state, const HWND dialog, const bool on)
		{
			for (const int control : {IDOK, IDC_MODE_PLAY, IDC_MODE_HOST, IDC_ACCOUNT, IDC_PASSWORD, IDC_REMEMBER,
			                          IDC_LANGUAGE, IDC_MAP, IDC_RANKED})
				EnableWindow(GetDlgItem(dialog, control), on ? TRUE : FALSE);
			EnableWindow(GetDlgItem(dialog, IDC_LINKS), on && !state.checkingLinks ? TRUE : FALSE);
		}

		// Registers or removes the website links as asked, then finds which copy they start, on a worker: the copy
		// they name may sit on a network share that is slow to answer. The registry is changed only on the player's
		// tick.
		void checkLinks(State& state, const HWND dialog, const LinksChange change)
		{
			state.checkingLinks = true;
			EnableWindow(GetDlgItem(dialog, IDC_LINKS), FALSE);
			runOnWorker(kLinksChecked,
			            LinksJob{.dialog = dialog,
			                     .exe = *state.exe,
			                     .change = change,
			                     .name = text(state, "launcher.links.name")},
			            [](LinksJob& job)
			{
				try
				{
					if (job.change == LinksChange::Register)
						registerLinks(job.exe, job.name);
					else if (job.change == LinksChange::Remove)
						unregisterLinks(job.exe);
				}
				catch (const std::exception& problem)
				{
					job.problem = problem.what();
				}
				const std::optional<std::filesystem::path> linked = linkedCopy();
				std::error_code error;
				job.here = linked && std::filesystem::equivalent(*linked, job.exe, error);
				if (linked && !job.here && std::filesystem::exists(*linked, error))
					job.elsewhere = linked;
			});
		}

		// Ticks the website-link option when the links start this copy, names the copy they start when it is another
		// one that still exists, and tells how a change went unless a sign-in holds the status line.
		void showLinks(State& state, const HWND dialog, const LinksJob& job)
		{
			state.checkingLinks = false;
			EnableWindow(GetDlgItem(dialog, IDC_LINKS), state.busy ? FALSE : TRUE);
			CheckDlgButton(dialog, IDC_LINKS, job.here ? BST_CHECKED : BST_UNCHECKED);
			const std::string note =
				job.elsewhere
					? state.texts->format("launcher.links.elsewhere", {platform::narrow(job.elsewhere->wstring())})
					: "";
			SetDlgItemTextW(dialog, IDC_LINKS_NOTE, platform::widen(note).c_str());
			ShowWindow(GetDlgItem(dialog, IDC_LINKS_NOTE), job.elsewhere ? SW_SHOW : SW_HIDE);
			if (job.change == LinksChange::None || state.busy)
				return;
			const std::wstring status =
				job.problem.empty()
					? std::wstring()
					: text(state, "launcher.status.links_failed") + L"\n" + platform::widen(job.problem);
			SetDlgItemTextW(dialog, IDC_STATUS, status.c_str());
		}

		std::string languageCode(const HWND dialog)
		{
			const LRESULT selected = SendDlgItemMessageW(dialog, IDC_LANGUAGE, CB_GETCURSEL, 0, 0);
			if (selected < 0 || selected >= static_cast<LRESULT>(std::size(kLanguages)))
				return {};
			return std::string(kLanguages[selected].code);
		}

		void prefill(State& state, const HWND dialog)
		{
			const i18n::Translation& texts = *state.texts;
			SetWindowTextW(dialog, text(state, "hook.title").c_str());
			SetDlgItemTextW(dialog, IDC_MODE_PLAY,
			                platform::widen(texts.text("launcher.tile.play") + "\n" +
			                                texts.text("launcher.tile.play.description")).c_str());
			SetDlgItemTextW(dialog, IDC_MODE_HOST,
			                platform::widen(texts.text("launcher.tile.host") + "\n" +
			                                texts.text("launcher.tile.host.description")).c_str());
			static constexpr std::pair<int, std::string_view> captions[] = {
				{IDC_ACCOUNT_LABEL, "launcher.field.account"},   {IDC_PASSWORD_LABEL, "launcher.field.password"},
				{IDC_REMEMBER, "launcher.field.remember"},       {IDC_LINKS, "launcher.field.links"},
				{IDC_LANGUAGE_LABEL, "launcher.field.language"}, {IDC_MAP_LABEL, "launcher.field.map"},
				{IDC_RANKED, "launcher.field.ranked"},           {IDCANCEL, "launcher.button.cancel"},
			};
			for (const auto& [control, id] : captions)
				setText(state, dialog, control, id);

			const Defaults& defaults = *state.defaults;
			for (const Language& language : kLanguages)
				addItem(dialog, IDC_LANGUAGE, text(state, language.name));
			LRESULT language = 0;
			for (std::size_t i = 0; i < std::size(kLanguages); ++i)
				if (defaults.language == kLanguages[i].code)
					language = static_cast<LRESULT>(i);
			SendDlgItemMessageW(dialog, IDC_LANGUAGE, CB_SETCURSEL, static_cast<WPARAM>(language), 0);

			// The first map item leaves the choice to the server's own map list (no +loadLevel); the rest are the
			// maps that list names, so the host can pin a starting one.
			addItem(dialog, IDC_MAP, text(state, "launcher.map.server_list"));
			for (const std::string& map : *state.maps)
				addItem(dialog, IDC_MAP, platform::widen(map));
			const LRESULT map = defaults.map.empty()
				                    ? 0
				                    : SendDlgItemMessageW(dialog, IDC_MAP, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
				                                          reinterpret_cast<LPARAM>(platform::widen(defaults.map).c_str()));
			SendDlgItemMessageW(dialog, IDC_MAP, CB_SETCURSEL, static_cast<WPARAM>(map < 0 ? 0 : map), 0);

			SetDlgItemTextW(dialog, IDC_ACCOUNT, platform::widen(defaults.account).c_str());
			CheckDlgButton(dialog, IDC_REMEMBER, defaults.remember ? BST_CHECKED : BST_UNCHECKED);
			// A website link only ever signs in to play.
			const bool host = defaults.mode == Mode::Host && state.link->kind == LinkKind::None;
			CheckRadioButton(dialog, IDC_MODE_PLAY, IDC_MODE_HOST, host ? IDC_MODE_HOST : IDC_MODE_PLAY);
			updateMode(state, dialog);
			checkLinks(state, dialog, LinksChange::None);
		}

		void startSignIn(State& state, const HWND dialog, SignInJob request, const std::string_view status)
		{
			state.busy = true;
			enableControls(state, dialog, false);
			setText(state, dialog, IDC_STATUS, status);
			runOnWorker(kSignedIn, std::move(request), [](SignInJob& job)
			{
				job.result = job.code.empty() ? requestSession(job.server, job.host, job.account, job.password)
				                              : requestLinkSession(job.server, job.code);
			});
		}

		void signInWithPassword(State& state, const HWND dialog)
		{
			if (state.busy)
				return;
			const std::wstring account = windowText(GetDlgItem(dialog, IDC_ACCOUNT));
			const std::wstring password = windowText(GetDlgItem(dialog, IDC_PASSWORD));
			if (account.empty() || password.empty())
			{
				setText(state, dialog, IDC_STATUS, "launcher.status.need_account");
				MessageBeep(MB_ICONWARNING);
				return;
			}
			startSignIn(state, dialog,
			            {.dialog = dialog,
			             .server = *state.server,
			             .host = IsDlgButtonChecked(dialog, IDC_MODE_HOST) == BST_CHECKED,
			             .account = platform::narrow(account),
			             .password = platform::narrow(password)},
			            "launcher.status.signing_in");
		}

		// Signs in with the website link the launcher was started with, or says why this copy cannot use it.
		void signInWithLink(State& state, const HWND dialog)
		{
			if (state.link->kind == LinkKind::Unusable)
				setText(state, dialog, IDC_STATUS, "launcher.status.link_unusable");
			else if (state.link->kind == LinkKind::Play)
				startSignIn(state, dialog, {.dialog = dialog, .server = *state.server, .code = state.link->code},
				            "launcher.status.signing_in_link");
		}

		void finishSignIn(State& state, const HWND dialog, SignInJob& job)
		{
			state.busy = false;
			const bool viaLink = !job.code.empty();
			if (!job.result.session)
			{
				enableControls(state, dialog, true);
				updateMode(state, dialog); // the map controls follow the mode again
				if (job.result.problem.empty())
					setText(state, dialog, IDC_STATUS, refusalText(job.result.refusal, viaLink));
				else
				{
					const std::string_view lead =
						job.result.unreachable ? "launcher.status.no_server" : "launcher.status.failed";
					SetDlgItemTextW(dialog, IDC_STATUS,
					                (text(state, lead) + L"\n" + platform::widen(job.result.problem)).c_str());
				}
				return;
			}

			Choice& choice = state.choice;
			choice.viaLink = viaLink;
			const bool host = !viaLink && IsDlgButtonChecked(dialog, IDC_MODE_HOST) == BST_CHECKED;
			choice.mode = host ? Mode::Host : Mode::Play;
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
				signInWithLink(*initial, dialog);
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
				case IDC_LINKS:
					if (HIWORD(wParam) == BN_CLICKED)
						checkLinks(*state, dialog,
						           IsDlgButtonChecked(dialog, IDC_LINKS) == BST_CHECKED ? LinksChange::Register
						                                                                : LinksChange::Remove);
					return TRUE;
				case IDOK:
					signInWithPassword(*state, dialog);
					return TRUE;
				case IDCANCEL:
					EndDialog(dialog, IDCANCEL);
					return TRUE;
				default:
					return FALSE;
				}
			case kSignedIn:
			{
				const std::unique_ptr<SignInJob> job(reinterpret_cast<SignInJob*>(wParam));
				finishSignIn(*state, dialog, *job);
				return TRUE;
			}
			case kLinksChecked:
			{
				const std::unique_ptr<LinksJob> job(reinterpret_cast<LinksJob*>(wParam));
				showLinks(*state, dialog, *job);
				return TRUE;
			}
			default:
				return FALSE;
			}
		}
	}

	std::optional<Choice> showLauncher(const HINSTANCE instance, const relay::ServerAddress& server,
	                                   const Defaults& defaults, const std::vector<std::string>& maps,
	                                   const std::filesystem::path& exe, const LaunchLink& link)
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

		const i18n::Translation texts = platform::displayTranslation(instance);
		State state{
			.texts = &texts, .server = &server, .defaults = &defaults, .maps = &maps, .exe = &exe, .link = &link};
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
