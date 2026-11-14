# DingoDiscordPresence — Discord Presence Library Plan

**Goal:** one small C++ static library that speaks Discord's local Rich Presence pipe, shipped prebuilt for
Windows and Linux, and used by both Headstone and 200 OK. Each game keeps only its own text, flags and settings.

**Built from:**
- Headstone's shipped client: `Headstone/src/Platform/DiscordPresence.{h,cpp}` (523 lines, Windows + Linux, live
  on Steam). This is the code base of the library.
- 200 OK's plan: `ok200/.claude/plans/discord-presence-plan.md` (not built yet). It adds buttons, image fields, a
  dry run, a frame log, a write timeout and a testable send gate.
- DingoNet's packaging: `Headstone/scripts/fetch-dingonet.ps1` and `Headstone/premake5.lua:225-228`, `:267-269`.
  The library ships the same way.

**How to run it:** one phase at a time; say **"execute presence phase N"**. Each phase is implementer, builds and
tests on Windows and Linux in all three configurations, a fresh reviewer, one commit. Push only when you say.

**Not read for this plan:** DingoNet's `build.yml` and `release.yml`. Phase 3 copies them, so read them first.
DingoNet's `CLAUDE.md` says CI builds with toolset `v143`, because the Windows runners lack `v145`.

Status: ⬜ todo · 🟨 in progress · ✅ done

## At a glance

| # | Phase | You get | Est. | Needs | St. |
|---|---|---|---|---|---|
| 1 | Repo and extraction | Headstone's client as a library with no engine dependency, building on Windows and Linux | 0.5 d | — | ✅ |
| 2 | 200 OK's features and tests | Buttons, images, dry run, frame log, write timeout, field limits. Unit tests and a fake-Discord test on both platforms | 0.75 d | 1 | ✅ |
| 3 | CI and release v0.1.0 | Six zips on a GitHub release (3 configs × 2 platforms) | 0.25 d | 2 | ✅ |
| 4 | 200 OK uses it | Replaces phase 1 of 200 OK's plan (0.75 d becomes 0.25 d). Linux no longer deferred there | 0.25 d | 3 | ⬜ |
| 5 | Headstone uses it | `DiscordPresence.cpp` shrinks to a ~60-line adapter. Same behaviour on your profile | 0.25 d + your live test | 4 | ⬜ |

All together: about **2 days**. 200 OK saves 0.5 d of it back.

## Decisions

### D1. A new repo, shaped like DingoNet

- Public repo `KingoBoiii/DingoDiscordPresence`, premake, static library only, no dependencies beyond the OS.
- Namespace `Dingo::DiscordPresence` (as DingoNet is `Dingo::Net`), macro prefix `DDP_`. The public header is
  `include/DingoDiscordPresence/DiscordPresence.h`.
- `VERSION`, `CHANGELOG.md`, `scripts/windows/Package.bat` and a Linux package script, as DingoNet has.

### D2. What is in the library, and what stays in the games

| In the library | Stays in each game |
|---|---|
| Pipe (Windows) and socket (Linux) transport, never blocking | The app id (`Tuning::DISCORD_APP_ID`, `Defs::DiscordClientId`) |
| Framing, handshake, `READY`, ping/pong, close | The text: `UpdatePresence` (Headstone), `MakePresence` (200 OK) |
| Building the `SET_ACTIVITY` JSON, escaping, field limits | `--discord`, `--discord-pipe=N`, `--discord-log` and the dev-launch rule |
| Retry timer, the 15 s send gate, resend after reconnect | The settings row and save key (Headstone only) |
| Clear, enable/disable, shutdown flush | Localisation, the capture hook `require-data-dir.js` |
| The fake Discord used in tests and captures | Logging: the game passes a callback |

### D3. The API: Headstone's behaviour, 200 OK's fields

```cpp
// include/DingoDiscordPresence/DiscordPresence.h: no platform types, no engine types
namespace Dingo::DiscordPresence
{
    struct Button { std::string Label, Url; };
    struct Activity
    {
        std::string Details, State;
        int64_t StartUnix = 0;                  // 0: no timer
        std::string LargeImage, LargeText;      // the art asset key and its hover text
        std::string SmallImage, SmallText;
        int PartySize = 0, PartyMax = 0;        // 0: no party line
        std::string PartyId;                    // party.size needs it; Headstone passes the lobby id as text
        std::vector<Button> Buttons;            // at most 2 are sent
        bool operator==(const Activity&) const = default;
    };

    enum class LogLevel { Info, Warn };
    using LogFn = void (*)(LogLevel, std::string_view message, void* user);

    struct Config
    {
        std::string AppId;          // empty: dry run, no pipe is ever opened
        bool Enabled = true;
        int PipeIndex = -1;         // -1 scans discord-ipc-0..9, N pins one
        bool LogFrames = false;     // 200 OK's --discord-log
        double RetrySeconds = 10, MinSendSeconds = 15, WriteTimeoutSeconds = 5;
        LogFn Log = nullptr; void* LogUser = nullptr;
    };

    class Client
    {
    public:
        explicit Client(const Config& config);   // connects on the first Update, not here
        ~Client();                               // clear, flush up to 200 ms, close
        void Update();                           // once a frame
        void SetActivity(const Activity& activity);
        void Clear();                            // past the rate limit
        void SetEnabled(bool on);
        bool IsConnected() const;
    };
}
```

- **A class, not statics.** Tests need two clients and a fake server in one process. Each game holds one instance.
- **No engine dependency.** `DE_INFO_TAG` becomes the `Log` callback. `READY`'s body never reaches it (it carries
  the player's Discord user id, name and avatar).
- **State moves from file-scope statics into a pimpl**, so `windows.h` stays out of the header.

### D4. Where the two sources disagree

| Topic | Headstone (shipped) | 200 OK plan | Library |
|---|---|---|---|
| Clear | `"activity":null` | `args` with only `pid` | Headstone's. It is verified live |
| Retry | 10 s | 30 s | Config, default 10 s |
| Gate after reconnect | First send goes at once | Never two sends inside 15 s | An option, `Config::KeepSendGapAcrossReconnects`. Default: Headstone's |
| Hung Discord | Next frame is dropped, link stays | Write pending 5 s drops the link | Both: drop the frame, then drop the link at the timeout |
| Empty app id | Disabled, one warning | Dry run with frame log | Dry run. Headstone sees the same result: nothing sent |
| Line length | Not cut | Cut at 120 code points | Library cuts at 128 UTF-16 units on a code point boundary, as a safety net. 200 OK's own cut stays |
| Time source | `steady_clock` inside | `nowSeconds` passed in | `steady_clock` inside. The gate is a pure header with time passed in, so it is unit-tested |

### D5. Prebuilt static libraries: the ABI rules

This is the fragile part. The API passes `std::string` and `std::vector`, so the library and the game must agree.

- **Windows:** x86_64, MSVC, dynamic CRT (`staticruntime "Off"`, as both games and the engine). No `/GL`, so the
  `.lib` does not tie to one exact compiler build. Build with the oldest toolset in use: a newer linker accepts it.
  Headstone pins `v145` (`premake5.lua:340`). 200 OK's `premake5.lua` pins none.
- **Three configs:** Debug, Release, Distribution. Debug uses the debug CRT and checked iterators, so a Release lib
  in a Debug game breaks at link time.
- **Linux:** x86_64, GCC, libstdc++, built on the same CI image as DingoNet so the glibc floor (2.38 for Headstone)
  holds. `-fPIC` on, as DingoNet (a game may link it into a shared object). No LTO. The library needs no extra system library: sockets are in libc.
- **Language:** C++20 in the library (the games are `C++latest`; the engine package is C++20).
- **Symbols:** embedded in the lib (`/Z7`), as DingoNet does, so no `.pdb` ships.

### D6. The package

One zip per config per platform, named like DingoNet's:
`DingoDiscordPresence-<version>.<build>-<Config>-<windows|linux>-x86_64.zip`

```
include/DingoDiscordPresence/          public headers
lib/<Config>/DingoDiscordPresence.lib  or libDingoDiscordPresence.a
tools/fake-discord.ps1          the fake pipe for captures (from Headstone's scripts/dev/discord-pipe.ps1)
VERSION.txt
```

Each game gets `scripts/fetch-dingodiscordpresence.ps1` and `.sh`, copied from `fetch-dingonet`, including `-Source` for
a local checkout. Vendored to `vendor/DingoDiscordPresence/` (Windows) and `vendor/DingoDiscordPresence/linux/`.

### D7. Testing lives in the library

- **Unit tests:** JSON escape, activity JSON (empty fields left out, party needs an id, two buttons at most), frame
  header, the send gate (gap, reconnect, latest wins), the UTF-16 cut on `·` and `æøå`.
- **Fake-Discord test, both platforms:** a test-only server on `discord-ipc-9` in the same process. It checks the
  handshake, no send before `READY`, ping/pong with the same body, a split frame, an oversize frame, close and
  reconnect, a server that stops reading (the write timeout), and clear on destruction.
- The games keep one capture each to prove their own wiring, not the protocol.

## Risks

- **ABI mismatch** between a prebuilt lib and a game (D5). The link fails loudly on Windows. On Linux, a newer
  libstdc++ on the build box is the quiet failure: keep the CI image shared with DingoNet.
- **Regressing Headstone**, which is live. That is why 200 OK goes first (phase 4) and Headstone's switch ends with
  your own live test (phase 5).
- **Discord closes the raw pipe.** Then only this library changes, to the Social SDK. Both games keep their code.

## Phases

### Phase 1 — Repo and extraction (~0.5 d) ✅
- [x] Create the repo with DingoNet's skeleton: `premake5.lua`, `VERSION`, `.editorconfig`, licence, `CLAUDE.md`.
- [x] Move `Headstone/src/Platform/DiscordPresence.cpp` in. Split it: `src/Platform/WinPipe.cpp`,
  `src/Platform/PosixPipe.cpp` (behind `Pipe.h`), `Client.cpp`, `Json.h`, `Frame.h`, `SendGate.h`.
- [x] Statics become a pimpl. `DE_*_TAG` becomes the log callback. `PresenceInfo` becomes `Activity` (D3).
- [x] Remove the hardcoded `"headstone"` asset (`DiscordPresence.cpp:320`): it comes from `Activity`.
- Verify: builds clean in all three configs on Windows and on Linux.
- Commit: `feat(client): add the Discord pipe client from Headstone`

**As built (14 Nov 2026):**
- 25 tests pass in Debug, Release and Distribution on Windows (v145) and Linux (WSL, GCC 13.3). Fresh review: no
  findings.
- The public header has only what phase 1 implements. `Buttons`, `SmallImage`, `LogFrames` and
  `WriteTimeoutSeconds` (D3) arrive with phase 2.
- The send gate still has Headstone's rule (first send on a connection goes at once). D4's stricter rule is
  phase 2.
- An empty app id turns the client off (Headstone's rule). D4's dry run is phase 2.
- Fixed while extracting: `SetEnabled(true)` could switch on a client with no app id. After `Clear()`,
  `SetActivity` with the value shown before was dropped; `Clear()` now forgets the activity. Headstone never calls
  `Clear` itself, so its shipped build is not affected.
- `Frame.h` is a fourth pure header (encode and decode, byte shifts, so not tied to a little-endian host).
- The Claude setup (settings, agents, rules, the `done`, `feature-plan` and `plan-run` skills) is adapted from
  200 OK's.

**Carried into phase 2:**
- A `Clear` whose write is dropped (full pipe) is not retried, so the old card can stay.
- A dropped handshake or pong is not retried.
- `READY` is found by a substring search, as in Headstone.
- No test reaches `READY`, ping and pong, or a write drop yet: that needs the fake server (D7). Add one for the
  `Clear` fix.

### Phase 2 — 200 OK's features and tests (~0.75 d) ✅
- [x] Buttons, small image, `LogFrames`, the dry run, the write timeout, the UTF-16 cut, the strict gate (D4).
- [x] The test project: unit tests and the in-process fake server (D7).
- [x] `tools/fake-discord.ps1` from Headstone's `discord-pipe.ps1`, with 200 OK's close and hang modes.

**As built (14 Nov 2026):**
- 67 tests pass in Debug, Release and Distribution on Windows (v145) and Linux (WSL, GCC 13.3), in about 2 s per
  configuration. 21 of them run against a fake Discord inside the test project (`tests/FakeDiscord*`), on pipe
  index 9 only; on Linux it points `XDG_RUNTIME_DIR` at a temp folder first.
- Fresh review: one bug, fixed. A dropped activity write that `SetActivity` then took back left the write timer
  running, and a healthy link was dropped after `WriteTimeoutSeconds`. Two test gaps it named are closed, each
  checked by removing the line it guards.
- The dry run is an empty app id only. 200 OK's plan also wanted it whenever the link is down: not built. A
  game that wants the log in a sandboxed run passes an empty `AppId`.
- `LogFrames` logs a received frame as its opcode and its event or command name only, and that name only when
  it is 1 to 32 characters of `A-Z` and `_`. A pong is logged without its body.
- The write timeout lives in `Client.cpp`, the same on both platforms. `src/Platform/` is unchanged since
  phase 1.
- A dropped clear and a dropped pong are retried every `Update`. A dropped handshake drops the link.
  `~Client` retries a dropped clear once after its 200 ms flush.
- **The send gap is an option** (the user, 14 Nov 2026: "Make it an option/setting when initializing this
  library"): `Config::KeepSendGapAcrossReconnects`. False, the default, is Headstone's shipped rule: the first
  activity on a new connection or after a `Clear` goes at once. True is D4's strict rule: two activities are
  never closer than `MinSendSeconds`. Both have fake-server tests.
- `tools/fake-discord.ps1` has `-CloseAfterSeconds` and `-HangAfterSeconds`. Smoke-tested by hand on index 9,
  not yet against a game.

**Not tested:** a dropped handshake (a fresh pipe always has room), and anything against a real Discord. That
is phase 4 and 5's live test.

### Phase 3 — CI and release (~0.25 d) ✅
- [x] Read DingoNet's `build.yml` and `release.yml`, then copy them. Build and test on both platforms per push.
- [x] Package scripts for both platforms (D6). Tag `v0.1.0`.
- Verify: six zips on the release, each with the layout in D6.

**As built (14 Nov 2026):**
- `Package.bat` and `Package.sh` write the six zips locally with D6's layout. `tools/fake-discord.ps1` is in
  the Linux zips too.
- `Dingo::DiscordPresence::GetVersion()`, `Version.h` and a stamped `BuildInfo.h`, as DingoNet. 68 tests on both
  platforms.
- `build.yml` and `release.yml` are DingoNet's with the names changed. CI builds Windows with toolset `v143`;
  a local Release build with `v143` compiles clean.
- Fresh review: no findings.
- The repo is public at `KingoBoiii/DingoDiscordPresence`. `build.yml`'s first run passed on all six jobs.
- `v0.1.0` is published (build 4, commit `9d59f80`). `release.yml` attached the six zips,
  `DingoDiscordPresence-0.1.0.4-<Config>-<windows|linux>-x86_64.zip`. The games fetch version `0.1.0`, build `4`.

### Phase 4 — 200 OK uses it (~0.25 d) ⬜
- [ ] `scripts/fetch-dingodiscordpresence.{ps1,sh}`, premake `includedirs`/`libdirs`/`links` for both systems.
- [ ] Edit 200 OK's plan: phase 1 becomes "vendor DingoDiscordPresence and wire `GameLayer`". Drop `src/Discord/`,
  `Json.h`, `SendGate.h`, `fake-discord.ps1`, `test/DiscordTests.cpp` and the non-Windows stub. Take Linux out of
  Deferred. Phases 2 and 3 stay as written.
- Verify: 200 OK's phase 1 capture against the fake pipe on index 9.

### Phase 5 — Headstone uses it (~0.25 d + your live test) ⬜
- [ ] Fetch scripts and premake, as phase 4. On Linux, put the link before `pthread` like DingoNet
  (`premake5.lua:228`).
- [ ] `DiscordPresence.cpp` becomes an adapter: same static API, one `Client` inside, `PresenceInfo` mapped to
  `Activity` with `LargeImage = "headstone"`. No caller changes (`HeadstoneLayer.cpp:452`, `:1752`, `:2223`,
  `SettingsScreen.cpp:253`).
- [ ] `docs/build.md:84-111`: point to the library.
- Verify: the fake-pipe capture, then your profile with real Discord on Windows and Linux. Toggle the setting off
  and on.
- Commit: `refactor(discord): use DingoDiscordPresence for the pipe client`

## Answered by the user (14 Nov 2026)

1. **Name:** `DingoDiscordPresence` (the user, 14 Nov 2026, during phase 3: "I'm gonna rename this to
   DingoDiscordPresence"). It was `DingoNametag` through phases 1 and 2. Namespace `Dingo::DiscordPresence`,
   header `DingoDiscordPresence/DiscordPresence.h`, macro prefix `DDP_`. The local folder is still named
   `DingoNametag` until the user renames it.
2. **Public repo with release zips**, like DingoNet: "Yes to both".
3. **Order:** 200 OK first (phase 4), Headstone second (phase 5): "Yes to both".
