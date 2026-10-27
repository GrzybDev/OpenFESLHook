# OpenFESLHook

Hook for Windows games that use EA's FESL online services, such as Battlefield: Bad Company 2, that sends their online traffic (FESL, Theater and HTTP) to a relay server over a WebSocket, or straight to servers of your choice.

Table of Contents
-----------------
- [Supported games](#supported-games)
- [Legal notes](#legal-notes)
- [Features](#features)
- [Build Requirements](#build-requirements)
- [Compiling](#compiling)
- [Installing](#installing)
- [Configuration](#configuration)
- [Usage](#usage)
- [File formats](#file-formats)
- [Credits](#credits)

Supported games
---------------

The hook's redirection knows no game in particular: it serves every host below the FESL and Theater domains on the ports each game connects to, so a game needs only to load `dinput8.dll` from its folder and to look its servers up by those names. Battlefield Heroes also gets parts of its own: its website among the default hosts, the launcher and the dedicated-server parts.

| Game                       | Releases                                                    | Tried |
|:---------------------------|:------------------------------------------------------------|:-----:|
| Battlefield: Bad Company 2 | Windows client (Steam, Origin, EA app) and dedicated server | yes   |
| Battlefield Heroes         | Windows client and dedicated server (game versions 1.46 and 1.89) | yes  |

Other FESL games for Windows that meet both conditions may work as they are, or with their hosts in [Configuration](#hosts).

Legal notes
-----------

- The project contains no original code or assets from any game.
- To use this project you need to have an original copy of the game, the project doesn't make piracy easier and doesn't break any of the DRM included in-game.
- The project is not affiliated with or endorsed by Electronic Arts or the makers of the games it serves.

Features
--------

- Answers the game's look-ups of the FESL, Theater and HTTP hosts with a loopback address of each host's own, through `gethostbyname` and, for a game's embedded browser, `getaddrinfo`, and refuses the game's `gethostbyname` look-ups of other hosts
- Starts a listener on the port the game connects to, so that no game needs ports of its own in the configuration and a game and its dedicated server can run on one PC
- Completes the game's SSLv3 on its FESL connections with a key and certificate generated on your PC that the game's client accepts; no certificate or key ships with the hook
- Carries FESL, Theater and Theater's UDP datagrams to a relay server over one WebSocket per game process ([openfesl.v1](docs/relay-protocol.md)), naming the host of each connection, through Windows' own TLS, certificate store and proxy settings
- Forwards the game's HTTP requests to the relay server's host, adding the host the game asked for as `X-OpenFESL-Host` and the relay token the server hands out with its WebSocket as `X-OpenFESL-Relay`, and leaves redirects and cookies to the game
- Opens the pages a game shows in the system's browser, such as Battlefield Heroes' hero creator and funds page, on the relay server or the host's own server instead of the game's former website
- Reaches the relay server's host name over IPv4 first, so that the server learns each player's IPv4 address even where the name also has IPv6 addresses; a request that cannot get through over IPv4 is sent again over every address family, and IPv4 is given up once IPv6 got through where IPv4 did not
- Sends chosen hosts straight to other servers as the game's own packets, over plain TCP or re-encrypted with TLS 1.2 or later
- Changes none of a client's code, so the client works the same with every release of a game
- Starts a dedicated server whose own start-up code cannot run without a display (Battlefield Heroes) by patching that code in memory, only when the game is launched as a server
- Gives Battlefield Heroes' client, run as a dedicated server, the server parts it lacks, as the 1.42 server build has them: it lists the server's game in Theater, accepts players on the server's port, takes them into the game, and pings them and drops the silent ones, so a player who leaves can come back
- Finds everything it changes in a game's code by signature, string and run-time type, including what moves between a game's releases, so one hook serves each release tried
- Starts Battlefield Heroes from a launcher window when the game is started without a session, as from the game's own shortcut: signs in at the relay server and starts the game, or a dedicated server to host
- Optional log to a console and to a file, from the hook's start and each game connection down to every packet with its fields and every HTTP request with its headers, tagged and coloured by topic, with passwords, login keys, session tickets, game join keys and host secrets hidden

Build Requirements
------------------

- Visual Studio 2026

### Libraries (installed by vcpkg):
- Boost.Asio, Boost.Beast, Boost.Log, Boost.PropertyTree, Boost.URL, Boost.Endian and Boost.Algorithm
- OpenSSL 3.6, built with SSLv3 and its weak suites
- MinHook
- GoogleTest (unit tests)

Compiling
---------

See the [shared build workflows](https://github.com/GrzybDev/actions/tree/v1/.github/workflows) `cpp-checks.yml` and `cpp-dll.yml`, which CI and the release build run, for more detailed steps

1. [Integrate vcpkg with Visual Studio](https://learn.microsoft.com/vcpkg/commands/integrate)
2. Build the solution `OpenFESLHook.slnx` in Visual Studio (x86 Release for the hook); to build in another default relay server, pass `-p:OpenFESLHookServerUrl=wss://relay.example.net` to MSBuild
3. Run `Release\OpenFESLHook.Tests.exe` to run the unit tests

The hook is `Release\dinput8.dll`.

Installing
----------

1. Copy `dinput8.dll`, either from the latest build in [GitHub Releases](https://github.com/GrzybDev/OpenFESLHook/releases) or from `Release` if you compiled it yourself, next to the game's executable (for Battlefield: Bad Company 2, `BFBC2Game.exe`, or `Frost.Game.Main_Win32_Final.exe` for its dedicated server; for Battlefield Heroes, `BFHeroes.exe`, which is both the client and, with `+dedicated`, the server); the game loads it instead of the system's `dinput8.dll`, to which the hook passes the game's DirectInput calls
2. Put `OpenFESLHook.ini`, described in [Configuration](#configuration), next to it if you want to change any default

To uninstall, delete `dinput8.dll` and `OpenFESLHook.ini`, and `%LOCALAPPDATA%\OpenFESLHook\bfheroes.ini` if the Battlefield Heroes launcher remembered a choice.

Configuration
-------------

`OpenFESLHook.ini` is optional and is read from the folder of `dinput8.dll`, saved as UTF-8 (with or without a byte order mark) or UTF-16; a file in neither is read in the system's code page. Lines starting with `;` or `#` are comments, section and key names ignore case, spaces around `=` and quotes around a value are left out, and an empty value keeps its default. Switches take 1, true, yes or on, and 0, false, no or off.

A value the file gets wrong keeps its default, a line of `[Hosts]` or `[Forward]` it gets wrong is left out, and a section or key the hook does not read is reported. A section or key that appears twice, or a line that is neither a `[section]` nor `key=value`, keeps every default. The hook lists each problem in its log, or in a dialog when it starts with no log output open.

| Key                          | Description                                                                                                                                         | Allowed values                     | Default                          |
|-----------------------------:|:----------------------------------------------------------------------------------------------------------------------------------------------------|:-----------------------------------|:---------------------------------|
| `Server.Url`                 | The relay server's WebSocket address; HTTP requests go to the same host and port over `http://` for `ws://` and `https://` for `wss://`             | `ws://` or `wss://` address        | `wss://fesl.grzyb.app`           |
| `Redirect.BlockUnknownHosts` | Make the game's `gethostbyname` look-ups of hosts that `[Hosts]` does not name fail; `getaddrinfo` look-ups of them go to the system's resolver, which is how the hook looks its `[Forward]` servers up | Switch | 1 |
| `Forward.VerifyCertificates` | Require `tls://` servers in `[Forward]` to show a certificate from an authority in the Windows certificate store                                    | Switch                             | 1                                |
| `Log.Console`                | Show the log in a console window, or in the game's own console if it has one; in a window the hook opens, Ctrl+C does nothing, but closing it ends the game | Switch                             | 0                                |
| `Log.File`                   | Save the log to a file, which other programs can read while the game runs                                                                           | Switch                             | 0                                |
| `Log.Path`                   | The log file, absolute or relative to the folder of `dinput8.dll`; missing folders are created, and a file named here is appended to               | Any path                           | `OpenFESLHook_<date>_<time>.log`, new for each start |
| `Log.ConsoleColors`          | Colour the console's lines by topic and level, in a console the hook opens itself                                                                   | Switch                             | 1                                |
| `Log.ConsoleLevel`           | The least serious lines the console shows                                                                                                           | trace, debug, info, warning, error | info                             |
| `Log.FileLevel`              | The least serious lines the file holds                                                                                                              | trace, debug, info, warning, error | debug                            |

```ini
[Server]
Url=ws://127.0.0.1:8000

[Log]
Console=yes
ConsoleLevel=debug
```

Each log line names its level and its topic: `Hook` (the start, the settings and the log), `DNS` (the game's look-ups and its connections to the hook), `FESL`, `Theater`, `HTTP`, and `Relay` (the WebSocket to the relay server). The console shows each topic in a colour of its own, warnings in yellow, errors in red and the text of debug and trace lines in gray. `info` follows the hook's start, its listeners, the relay server and each game connection; `debug` adds every packet with its fields, every HTTP request and the hook's decisions, such as the ports it listens on; `trace` adds the headers of HTTP requests and responses. Passwords, login keys, session tickets, game join keys and host secrets are hidden at every level.

### Hosts

Each line of `[Hosts]` sends a host, or with `*.` every host below a domain, to the hook: `host=protocol`, where the protocol is `fesl` (SSLv3 over TCP), `theater` (TCP, and UDP datagrams on the same port) or `http`. Names ignore case, and a host takes the line that names it most closely. A `[Hosts]` section replaces the default lines as a whole, unless none of its lines can be used:

| Host                      | Protocol  |
|:--------------------------|:----------|
| `*.fesl.ea.com`           | `fesl`    |
| `*.theater.ea.com`        | `theater` |
| `easo.ea.com`             | `http`    |
| `*.battlefieldheroes.com` | `http`    |

Each FESL game has hosts of its own below the FESL and Theater domains, such as `bfbc2-pc.fesl.ea.com` and `bfbc2-pc-server.fesl.ea.com` for Battlefield: Bad Company 2 and its dedicated server, so the defaults cover them all. Battlefield Heroes also reaches its website, below `battlefieldheroes.com`, for its account, its store and the pages it opens in the browser.

### Forward

Each line of `[Forward]` sends the services of a host that `[Hosts]` redirects, or with `*.` those of every such host below a domain, straight to a server instead of the relay server: `host=server`, where the server is `host[:port]` or `tcp://host[:port]` (the game's packets as they are, FESL decrypted) or `tls://host[:port]` (the same inside TLS 1.2 or later, where the server's certificate has to come from an authority in the Windows certificate store and name the server, unless `VerifyCertificates=0` lets any certificate through). Without a port, the server gets the port the game connected to. Theater's datagrams go to the same host and port over UDP, which is never encrypted. An `http` host sends its requests to `http://host:port`, or `https://host:port` for `tls://`, whether the game sends them itself or through WinHTTP, and the pages the game opens in the browser open at the same path there. A line whose server `[Hosts]` redirects as well is left out, since the hook looks the server up through the same redirect.

```ini
[Forward]
bfbc2-pc.fesl.ea.com=tls://emulator.example.net
*.theater.ea.com=emulator.example.net
```

Usage
-----

1. Install the hook as described in [Installing](#installing)
2. Change the defaults you need (see [Configuration](#configuration))
3. Start the game or its dedicated server as usual

When the game starts, the hook:
1. Reads `OpenFESLHook.ini` and opens the log; when no log output is on, it shows the problems it found in a dialog
2. Answers each look-up of a host from `[Hosts]` with a loopback address of that host's own, and makes the game's `gethostbyname` look-ups of other hosts fail
3. When the game connects to such an address, starts the host's listener on the port the game asked for, if it is not running yet
4. Completes the SSLv3 handshake of each FESL connection with a certificate it generated that the game's client accepts
5. Opens a WebSocket to the relay server with the game's first FESL or Theater connection, carries every packet in it and closes it with the last connection; when the relay server goes away, the game's connections close and the game connects again
6. Passes the game's HTTP requests on, opens the pages it hands to the system's browser on the relay server, and passes its DirectInput calls to the system's `dinput8.dll`

### Battlefield Heroes

Started without `+sessionId`, `BFHeroes.exe` opens the hook's launcher instead of closing:
1. Choose Play to join a game or Server to host one
2. Enter the account and password the relay server knows; Remember me keeps the mode, account, language, map and ranked choice in `%LOCALAPPDATA%\OpenFESLHook\bfheroes.ini`, never the password
3. Choose the game's language (System default follows Windows), and for a server a map from its `maplist.con` (or the server's own map list) and whether it is ranked
4. The launcher signs in at the relay server ([Launcher sign-in](docs/relay-protocol.md#launcher-sign-in)), starts the game or the server with the session it was given and closes; a refused sign-in shows the server's reason

The launcher also starts the game with `+webSiteHostName www.battlefieldheroes.com +battleFundsHostName www.battlefieldheroes.com +survey 0`: without the first two the game opens none of its website's pages, such as the hero creator, and `+survey 1` would open a survey page once the game quits. A game started by hand needs the same options for those pages.

A server, started this way or by hand with `+dedicated 1`, shows only its console. Once it has logged in, it lists its game in Theater as the server builds named theirs, `[data center]name(IP:port)`, from `sv.serverName`, `sv.serverIP` (`(unknown)` when empty), `sv.serverPort` and `sv.maxPlayers`, and accepts players on `sv.serverPort`; with `sv.serverIP` empty, it listens on every network interface. A build in which the hook does not find every part it needs runs on without that part, and the log names what was missing.

File formats
------------

The hook and the relay server talk the WebSocket protocol `openfesl.v1`, which carries the game's packets in frames of a four-byte header; [docs/relay-protocol.md](docs/relay-protocol.md) describes it, the games' packet framing, the HTTP requests and browser pages the hook sends to the server's host, and what a relay server has to do.

Credits
-------

- [GrzybDev](https://grzyb.dev)

Thanks to:
- [Aim4kill](https://github.com/Aim4kill/Bug_OldProtoSSL) (for the research into the games' ProtoSSL client)

Special thanks to:
- EA DICE and Electronic Arts (for making Battlefield: Bad Company 2)
- Easy Studios and Electronic Arts (for making Battlefield Heroes)
