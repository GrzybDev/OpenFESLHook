# Relay protocol (openfesl.v1)

The hook carries a game's FESL, Theater and Theater datagram traffic to a relay server over one WebSocket per game process. Each WebSocket message is one frame: a four-byte header that says what happened to which of the game's connections, followed by one whole game packet or a short body. Each connection names the host the game looked up, so one relay server can serve every FESL game without knowing anything about sockets on the player's PC.

## Connection

- The hook connects to the address in `Server.Url` (`ws://` or `wss://`) and asks for the subprotocol `openfesl.v1` in `Sec-WebSocket-Protocol`. The server has to select it; the hook closes a connection that comes back without it.
- The hook looks the server's host name up for IPv4 addresses only, for the WebSocket and the HTTP requests alike, so that the server, or a proxy in front of it, sees the player's public IPv4 address, which Theater hands out to other players. When a request cannot be sent over IPv4, for example because the name has no IPv4 address, connecting fails or times out, or the TLS handshake fails, the hook sends it again at once over every address family, and the next request is looked up for IPv4 first again. Only when that second attempt gets through over IPv6 does the hook stop asking for IPv4 addresses until the game process ends, with a warning in the log, since the server then sees an IPv6 address. A hook that cannot restrict the look-ups says so when it starts and uses every family. A `Server.Url` with an IP address is used as it is.
- The hook's `User-Agent` is `OpenFESLHook/<version> (Windows)`.
- Every message is binary; a text message ends the session.
- One WebSocket belongs to one game process. The hook opens it with the game's first FESL or Theater connection and closes it, with code 1000, when the last of them closes; the server answers that close, and a connection whose answer does not arrive within ten seconds is dropped. A server keeps the state of a game process (the login, its Theater session) for as long as the WebSocket is open.
- When the WebSocket fails or the server closes it, the hook closes every game connection of the session; the game then connects again, which opens a new WebSocket. Windows takes the close codes 1012 to 1014, which RFC 6455 does not define, for an invalid response and drops the connection without answering its close, so a server that shuts down or restarts closes with 1001.
- WinHTTP sends a WebSocket keep-alive frame every 30 seconds, so quiet sessions stay open through proxies.

## Frame

All numbers are unsigned and big-endian.

| Offset | Size | Field        | Meaning                                                                |
|-------:|-----:|:-------------|:-----------------------------------------------------------------------|
| 0      | 1    | `type`       | What the frame does, see [Frame types](#frame-types)                   |
| 1      | 1    | `channel`    | The game service the connection belongs to, see [Channels](#channels)  |
| 2      | 2    | `connection` | The connection's number, chosen by the hook                            |
| 4      | n    | `body`       | Depends on `type`                                                      |

The pair (`channel`, `connection`) names one game connection. The hook picks the number when the game connects, never uses 0, and does not reuse a number while that connection is open. Numbers of different channels are independent.

### Frame types

| Value | Name    | Sent by       | Body                                                       |
|------:|:--------|:--------------|:-----------------------------------------------------------|
| 1     | `Open`  | the hook      | u16 port the game connected to, u16 port of the game's socket, u8 length of the host name, the host name in ASCII |
| 2     | `Data`  | both          | exactly one whole game packet, see [Game packets](#game-packets) |
| 3     | `Close` | both          | empty                                                      |

- `Open` comes before any `Data` of its connection. The host name is the one the game looked up, such as `bfbc2-pc.fesl.ea.com` or `bfbc2-pc-server.theater.ea.com`, which tells the server the game, whether it is a game or a dedicated server, and the service; it is at most 253 bytes. The first port is the one the game connected to; the game's port is what the game's socket uses on the player's PC, which a server can answer a Theater `ECHO` with.
- `Data` from the server to a connection the hook does not know is dropped.
- `Close` from the hook means the game closed the connection; from the server it asks the hook to close it, after the packets sent before it have reached the game.
- A frame with another type or channel, a short header, a `Data` body that is not one whole packet or a `Close` with a body ends the session.

### Channels

| Value | Name               | The game's connection                                                     |
|------:|:-------------------|:--------------------------------------------------------------------------|
| 1     | `Fesl`             | A FESL connection over TCP; the hook terminates its SSLv3                 |
| 2     | `Theater`          | A Theater connection over TCP                                             |
| 3     | `TheaterDatagram`  | A game socket that sends UDP datagrams to Theater's port, such as `ECHO`  |

A `TheaterDatagram` connection is one game socket rather than a connection: its `Open` comes with the socket's first datagram and every datagram is one `Data` frame. The hook only opens one while the session has a FESL or Theater connection; datagrams outside one are dropped. It never sends `Close` for them, since UDP has no end; they end with the WebSocket.

## Game packets

FESL and Theater share a twelve-byte header, which the hook uses to cut the game's TCP streams into whole packets. It forwards each packet unchanged.

| Offset | Size | Field     | Meaning                                                                          |
|-------:|-----:|:----------|:---------------------------------------------------------------------------------|
| 0      | 4    | `command` | ASCII: lowercase FESL services (`fsys`, `acct`, …), uppercase Theater commands (`CONN`, `ECHO`, …) |
| 4      | 4    | `type`    | The kind in the top byte; for FESL the transaction number in the 24 bits below   |
| 8      | 4    | `length`  | The size of the whole packet, header included                                    |
| 12     | n    | `payload` | `key=value` lines separated by `\n`, ending with a NUL                           |

| Kind   | Meaning                                                    |
|:-------|:-----------------------------------------------------------|
| `0xC0` | FESL request from the game                                 |
| `0x80` | FESL answer or push from the server, or the game's answer to a push |
| `0xF0` | FESL request from the game in chunks                       |
| `0xB0` | FESL answer from the server in chunks                      |
| `0x40` | Theater request from the game                              |
| `0x00` | Theater answer or push from the server                     |

A length below 12 or above 1 MiB breaks the stream's framing; the hook then closes that game connection.

## HTTP

The game's HTTP requests do not use the WebSocket. The hook sends them to the relay server's host and port, over `https://` when `Server.Url` is `wss://` and `http://` when it is `ws://`, with the method, path, query, body and headers the game sent, and the game's `Host` header as `X-OpenFESL-Host`. The header is the hook's own because proxies in front of a server, such as Traefik, drop the `X-Forwarded-*` headers of a peer they do not trust and set `X-Forwarded-Host` to the host they were reached at; a server routes the game's requests by `X-OpenFESL-Host` and its path. The server's status, headers and body go back to the game. When the server cannot be reached, the game gets `502 Bad Gateway`.

## Launcher sign-in

The Battlefield Heroes launcher asks the relay server for a session over HTTP, at the same host and port as the game's HTTP requests: `POST /launcher/session` with the user agent `OpenFESLHook/<version> (Windows)` and a JSON body.

| Field      | Value                                           |
|:-----------|:------------------------------------------------|
| `game`     | `bfheroes`                                      |
| `role`     | `play` to join games, `host` to run a server    |
| `account`  | The account name the player entered             |
| `password` | The password the player entered                 |

A server grants the session with `200` and a JSON body holding `sessionId` and, optionally, `personaName`. The launcher starts the game with `+sessionId`, and a server also with the account, its password and the persona as the server's soldier (the account name when `personaName` is missing). Any other status, or a `200` without `sessionId`, refuses the sign-in; an `error` field names the reason, and the launcher shows its own message for `wrong-sign-in`, `unknown-game`, `disabled`, `banned`, `deactivated` and `no-persona`, and a general one otherwise.

## What the game needs from a server

- Theater addresses in FESL answers have to be host names the hook redirects, such as the game's own name below `theater.ea.com`, not IP addresses: the hook only learns about connections to the addresses it handed out. Any port works, since the hook listens on the one the game connects to.
- The game's FESL `Hello` arrives once per FESL connection, so a server resets the connection's FESL state when it closes, not the session's.

## Example

The game looks up `pc.theater.test`, connects to it on port 18395 from port 50000 and sends `CONN`; the server answers.

```text
hook   -> server  01 02 00 01  47 DB C3 50 0F  "pc.theater.test"
                  Open, Theater, #1, port 18395, game port 50000, 15-byte host name
hook   -> server  02 02 00 01  43 4F 4E 4E 40 00 00 00 00 00 00 1A  "TID=1\nPROT=2\n\0"
                  Data, Theater, #1, CONN, kind 0x40, 26 bytes
server -> hook    02 02 00 01  43 4F 4E 4E 00 00 00 00 00 00 00 29  "TID=1\nactivityTimeoutSecs=0\n\0"
                  Data, Theater, #1, CONN, kind 0x00, 41 bytes
hook   -> server  03 02 00 01
                  Close, Theater, #1 (or the WebSocket closes, if it was the last connection)
```
