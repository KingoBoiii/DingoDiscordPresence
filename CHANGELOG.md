# Changelog

## 0.2.0 (2026-11-14, build 10)

- `Activity::EndUnix`: a countdown to a Unix time. Left off when it is not after `StartUnix`.
- `Activity::Type`: `ActivityType::Playing`, `Listening`, `Watching` or `Competing`. Playing sends the same frame
  as 0.1.
- `Client::GetStatus()` and `Config::OnStatus`: `ConnectionStatus::Disabled`, `DryRun`, `Waiting` or `Connected`,
  for a settings row. The callback fires on a change only, from `Update`, `SetEnabled` and `Clear`, and never
  carries the player's data.
- Binary-incompatible: `Activity` and `Config` grew. Game code compiles unchanged (every new field is appended),
  but a game must take the 0.2 headers and libraries together and rebuild.

## 0.1.0 (2026-11-14, build 4)

The first version: Headstone's Discord Rich Presence client as a standalone library.

- `Client`, `Config` and `Activity` in `DingoDiscordPresence/DiscordPresence.h`: details, state, the elapsed clock, a large and a
  small image, a party and up to two buttons.
- Windows (named pipe, overlapped I/O) and Linux (Unix socket, with the Flatpak and Snap folders). No call waits
  on Discord.
- A dry run for an empty app id, a frame log (`Config::LogFrames`), a write timeout for a hung Discord, and the
  send gap as an option (`Config::KeepSendGapAcrossReconnects`).
- `tools/fake-discord.ps1`: a fake Discord pipe for a game's captures.
- Packages for Debug, Release and Distribution on Windows and Linux x86_64.
