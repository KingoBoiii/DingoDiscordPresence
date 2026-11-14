# DingoDiscordPresence — Claude Context

## What this project is

DingoDiscordPresence is a cross-platform C++20 static library that shows Discord Rich Presence for DingoEngine games. It speaks the Discord client's local pipe protocol itself (the one the old `discord-rpc` library spoke): no Discord SDK, no DLL, no account linking. It is extracted from *Headstone*'s shipped client (`Headstone/src/Platform/DiscordPresence.cpp`) and is used by *Headstone* and *200 OK*. The library is engine-free: no DingoEngine header, and it logs through a callback the game passes in. Games consume a prebuilt package, laid out like DingoNet's: release zips per configuration, unpacked to `vendor/DingoDiscordPresence/{include, lib/<Config>, VERSION.txt}`.

## Build system

- **Tool**: Premake5 5.0.0-beta8, committed in `vendor/premake/bin/` (`premake5.exe` and the Linux `premake5`).
- **Scripts**: `scripts/windows/*.bat` and `scripts/linux/*.sh` (`Generate`, `Build`, `Test`, `Package`), runnable from any directory. A configuration argument is optional (`Debug` by default; `Release`, `Distribution`; Linux also takes lower case).
- **Warnings**: `warnings "Extra"` + `fatalwarnings` on both platforms (`/W4 /WX`, `-Wall -Wextra -Werror`). A build with a warning is a failed build.
- **Configs**: `Debug` (`DDP_DEBUG`), `Release` (`DDP_RELEASE`, symbols on), `Distribution` (`DDP_DIST`, no symbols). `DDP_PLATFORM_WINDOWS` / `DDP_PLATFORM_LINUX`.
- **Output**: `build/bin/<Config>-<system>-x86_64/<Project>/`, intermediates in `build/bin-int/`. `DingoDiscordPresence` is a static lib, `DingoDiscordPresence-Tests` a console app (its project files land in `tests/`, git-ignored).
- **Linux from Windows**: WSL (Ubuntu 24.04, GCC 13): `wsl -e sh scripts/linux/Test.sh`.
- **CI** (`.github/workflows/`): `build.yml` builds and tests Debug, Release and Distribution on `windows-latest` and `ubuntu-latest` on every push and PR to main. `release.yml` packages all three configs on both OSes when a release with a `vX.Y.Z` tag is published; the Windows runners lack v145, so CI builds with `/p:PlatformToolset=v143`. Details: `scripts/README.md`.
- **Version**: `VERSION` holds `major.minor`; CI and `Package` overwrite `include/DingoDiscordPresence/BuildInfo.h` (`DDP_VERSION_*`, read by `Dingo::DiscordPresence::GetVersion()`). The checked-in `BuildInfo.h` is a `0.1.0` placeholder.

## Project structure

```
include/DingoDiscordPresence/    Public API (DiscordPresence.h): plain C++20, no platform header
src/                     Client.cpp (the protocol and its timing); Frame.h, Json.h, SendGate.h (pure, unit-tested)
  Platform/              Pipe.h (internal pipe API), WinPipe.cpp (named pipe, overlapped I/O) |
                         PosixPipe.cpp (Unix socket): the only place for Windows.h, sys/socket.h,
                         unistd.h; premake builds one per system
tests/                   DingoDiscordPresence-Tests: own runner (TestFramework.h), one file per area;
                         FakeDiscord.h + FakeDiscordWindows.cpp | FakeDiscordPosix.cpp: the test-only fake Discord
tools/                   fake-discord.ps1: the fake pipe for captures and by-hand tests (Windows)
scripts/windows/         Generate.bat, Build.bat, Test.bat, Package.bat
scripts/linux/           Generate.sh, Build.sh, Test.sh, Package.sh
vendor/premake/          Premake binaries — NEVER modify vendor code
.claude/plans/           Plans (discord-presence-library-plan.md is the v0.1 plan)
CHANGELOG.md             Released versions
```

## Cross-platform (hard requirement)

Windows and Linux in every phase: each builds and passes its tests on both before it is committed.

- Platform headers only under `src/Platform/`; the public header is plain C++20.
- Time from `std::chrono::steady_clock`.
- Linux sends use `MSG_NOSIGNAL`: a send to a closed peer otherwise raises `SIGPIPE` and kills the game.

## Code conventions

| Thing | Convention |
|---|---|
| Namespace | `Dingo::DiscordPresence` (internals in `Dingo::DiscordPresence::Internal`) |
| Members | `m_Name` (instance), `s_Name` (static), `UPPER_SNAKE_CASE` constants/macros |
| Macros | `DDP_` prefix |
| Formatting | tabs, braces on new lines |

- **`<Windows.h>` macros**: a game's headers may pull in `<Windows.h>` without `NOMINMAX`. Write `(std::min)(a, b)` / `(std::max)(...)` in public headers, and never name anything `near` or `far`.
- **X11 macros**: a Linux game may pull in `<X11/Xlib.h>`, which defines `Status`, `None`, `Success`, `True`, `False` and `Bool`. Never use those as names in a public header (hence `ConnectionStatus`).
- In a public header, a member named after its own type spells the type qualified. MSVC accepts `Version Version`, but GCC rejects it (`-Wchanges-meaning`).
- Scripts: `.sh` files are LF and executable in git (`git update-index --chmod=+x`), `.bat` files CRLF (`.gitattributes` enforces both).

## Comments — keep them to a minimum

Do not write code comments unless absolutely necessary. A comment must earn its place by stating a load-bearing "why" — a constraint, invariant, or non-obvious consequence the code cannot express itself. Never narrate what the next line does, never leave change-tracking or review commentary ("added X", "fixed Y"), and apply the same restraint to scripts, YAML and tests. When in doubt, leave it out.

## Key patterns

- **Nothing waits on Discord.** The Windows pipe is opened without `WaitNamedPipe`, reads go behind `PeekNamedPipe`, and writes are overlapped with one in flight at a time. The Linux socket is non-blocking and every send is `MSG_DONTWAIT`. A hung Discord must never stall a frame.
- **No threads, no globals.** The game calls `Client::Update` once a frame. All state is in the `Client` (a pimpl), so tests run several clients in one process.
- **The protocol**: a frame is a little-endian `uint32` opcode, a `uint32` length, then UTF-8 JSON. Opcodes: 0 handshake, 1 frame, 2 close, 3 ping, 4 pong. Nothing is sent before Discord's `READY`. Every ping gets a pong with the same body. Replies are drained every `Update`, or a long session fills the pipe.
- **Privacy**: `READY` carries the player's Discord user id, name and avatar. Of a received frame only the opcode and its event or command name (`FrameName`: 1 to 32 characters of `A-Z` and `_`) may be logged, and only with `Config::LogFrames`. Never the body, and a pong logs no body either.
- **Logging**: `Config::Log`; `nullptr` means silent. The library never prints any other way.
- **Connection status**: `GetStatus()` is live. `Config::OnStatus` fires on a change only, from the `Client::Update`, `SetEnabled` and `Clear` wrappers after the `Impl` call, never from `Shutdown`, `~Client`, `Drain` or `Disconnect`. `m_Reporting` blocks a nested report, and nothing but `m_Reporting` is touched after calling the game. It carries only the enum: none of READY's data.
- **The games own**: the app id, the card's text, their `--discord` flags, their settings row. None of that belongs here.

## Tests

`DingoDiscordPresence-Tests` uses its own runner (`tests/TestFramework.h`): `TEST(name)` registers a case, `CHECK(cond)` records a failure with file and line, and `main` prints `PASS`/`FAIL` per case and a summary, exiting non-zero on any failure. Tests that open a pipe pin index 9, where a real Discord never is (it takes the lowest free index), so a test run can never touch the developer's own Discord profile.

The fake Discord (`tests/FakeDiscord.h`) is test-only, single-threaded and never blocks: a test alternates `fake.Pump()` and `client.Update()`. It listens on `discord-ipc-9` only. It records every frame it receives, answers the handshake with a READY that carries the marker user name `SECRET-USER` (a test proves no log line contains it), and can send a ping, raw bytes (a split frame, an oversize length) and CLOSE, drop the connection, and stop reading. On Linux it first points `XDG_RUNTIME_DIR` at a fresh `mkdtemp` folder and restores it afterwards, so a test can never reach a real Discord socket; keep that rule for any new fake or test that opens a pipe. Real sleeps only where the send gate or the write timeout needs them (Windows sleeps in 15 ms steps; spin with `yield` otherwise), so the run stays around a second.

## Workflow

Do not commit unless asked. One commit per phase of the plan, after its review.

## Sub-agents

Phase implementation, diff reviews, docs, scripts and tests run on Sonnet. `src/Platform/` (overlapped I/O, non-blocking sockets) is the fragile part: implementation and review there run at high effort. Verification (build, tests) goes to Haiku at low, reporting failures only.
