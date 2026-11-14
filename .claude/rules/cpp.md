---
paths: ["src/**/*.cpp", "src/**/*.h", "include/**/*.h", "tests/**/*.cpp", "tests/**/*.h"]
---

- C++20, MSVC v145 and GCC 13. `/W4 /WX` and `-Wall -Wextra -Werror`: a warning is a failed build on
  either platform, so build both before calling anything done.
- Tabs, Allman braces, `m_` member prefix, `s_` statics, PascalCase types and methods, `UPPER_SNAKE`
  constants and macros, `DDP_` macro prefix. Namespace `Dingo::DiscordPresence`, internals in
  `Dingo::DiscordPresence::Internal`.
- Comments: almost none. Only a load-bearing "why" (a constraint, an invariant, a non-obvious
  consequence). No narration, no dates, no plan references, no change tracking.
- The public header is the ABI of a prebuilt package that two games link. Changing a struct or a
  signature there means a new release and a change in both games: say so, never do it in passing.
- Public headers: plain C++20, no platform header, `(std::min)(a, b)` with the parentheses, nothing
  named `near` or `far`, a member named after its type spells the type qualified.
- No engine header, no third-party library. The library links only the OS.
- No threads and no mutable statics: all state lives in the `Client`.
- Logging only through `Config::Log`. Nothing Discord sent goes into a log message.
- Every string that goes into JSON goes through `Quoted`.
- Time from `std::chrono::steady_clock`. Pure helpers (`SendGate.h`, `Json.h`, `Frame.h`) take time and
  inputs as arguments so the tests need no pipe and no sleep.
- Run the Generate script after adding or deleting a `.cpp`.
