---
name: feature-plan
description: Write a phased build plan in .claude/plans/ in DingoDiscordPresence's plan format, and execute it phase by phase (implementer, builds and tests on Windows and Linux, fresh reviewer, one commit per phase). Use when the user asks for a plan, says "execute <name> phase N" or "execute the whole plan", or when a change is big enough to need phases (over about an hour, a public API change, a protocol change, a packaging change).
---

# Plans: write them, then run them phase by phase

## Writing a plan

**File:** `.claude/plans/<name>-plan.md`. Copy the structure of
`discord-presence-library-plan.md` (the first plan in this repo).

**Sections, in this order:**
1. `# DingoDiscordPresence — <Name> Plan`
2. **`**Goal:**`** in 2–4 lines, including the user's reason in their own words.
3. **Scope bullets:** what changes and what is deliberately untouched. Add "Line numbers are at
   `<short sha>`. If they've moved, grep the symbol."
4. **`**How to run it:**`** "one phase at a time; say **"execute <short-name> phase N"**", the branch
   name, the per-phase loop (below), and "Push only when you say."
5. **The status legend:**
   `Status: ⬜ todo · 🟨 in progress (waiting on you) · ✅ done · ⏭ skipped`
6. **`## At a glance`:** a `| # | Phase | You get | Est. | Needs | St. |` table, then
   "All together: about **X**."
   - "You get" is what the user can see or do after that phase.
   - Estimates are concrete (40 min, 0.25 d), never "some work".
7. **`## Decisions`:** one `### D1. <the call, stated as a decision>` per call.
   - Give each a code sketch and `file:line` refs, checked in the code, not guessed.
   - Put the user's own choices here too.
8. **`## API and ABI`:** what changes in `include/DingoDiscordPresence/`, and what Headstone and 200 OK must
   change to take the new release. "None" is a valid answer and is written down.
9. **`## Coordination`:** only when another plan or open branch touches the same files, or a game's
   plan waits on this one. Say which one lands first.
10. **`## Testing`:** the unit tests and the fake-Discord tests per phase, on both platforms and in
    which configurations, a **Reviewer checklist** (concrete edge cases to check), and what can't be
    tested here (a real Discord client, a friend's view of the card: it goes on the user's live-test
    list).
11. **`## Deferred (build when needed)`:** the bigger options, parked rather than built.
12. **`## Open questions for you`:** each one with the default you'll use if the user doesn't
    answer. It becomes `## Answered by the user (<date>)` once they answer.
13. **`## Risks`.**
14. **`## Phases`:** one per phase, written as `### Phase N — <name> (~est) ⬜`.
    - List the work as `- [ ]` items with D-refs.
    - Give the verification and the exact commit message.

**Rules for the content:**
- **Smallest step first** ("no space rocket before it's necessary"). Each phase should leave
  something the user can see or try.
- **One phase = one commit**, with a conventional message in present tense, written in the plan.
- **Name the traps in the phase that hits them:** the Generate script for an added or deleted `.cpp`,
  a blocking call, a public-header change (ABI), a test that could reach a real Discord, a platform
  the phase forgets.
- **The games own** the app id, the card's text, their flags and their settings. A plan that pulls one
  of those into the library is wrong.
- **Present the plan in chat as the At a glance table plus the open questions,** not the whole file,
  after the advisor review below.

## Reviewing the plan (the advisor)

A wrong decision in a plan costs every phase built on it, so the plan gets the most expensive
reviewer once, and the phases get cheap ones. After the plan is written and before it is presented, run
the `plan-advisor` agent (`.claude/agents/plan-advisor.md`) once on the finished plan; pass the plan path.
Never a second call unless the plan is largely rewritten, and never for code: a phase's diff review
stays Sonnet.

Then fold the findings into the plan (a changed decision stays the user's call: it becomes an open
question), and add one line under **How to run it**: `**Advisor review:** <date>, N findings folded in.`

## Executing "execute <name> phase N"

**Find the plan.** The short name isn't always the file name: grep `.claude/plans/` for
`execute <name>` in the "How to run it" lines. Look in `.claude/plans/` first and `.claude/plans/done/`
second: finished plans (every phase ✅ or ⏭) live in `done/`. "Execute <name> phase N" is the user's
go-ahead to commit that phase. It is not a go-ahead to push. One phase per session, at most one day.

1. **Read the whole plan** (and `CLAUDE.md`). Check that the phases in the Needs column are ✅.
   Work on the plan's branch; create it from `origin/main` if it doesn't exist.
2. **Flip the phase to 🟨** in its heading and in the At a glance table.
3. **Implement.** For anything beyond a few lines, use the `phase-implementer` agent
   (`.claude/agents/phase-implementer.md`). Hand it the phase, its D-sections and the files to touch.
   Pass effort high when the phase touches `src/Platform/`.
4. **Build and test** all three configurations on Windows (`scripts\windows\Test.bat <Config>`) and on
   Linux (`wsl -e sh scripts/linux/Test.sh <Config>`), after the Generate script if a `.cpp` was added
   or deleted. Hand this to `build-runner` (`.claude/agents/build-runner.md`) or run it yourself. Every
   build must be clean and every test must pass, on both.
5. **A fresh `phase-reviewer`** (`.claude/agents/phase-reviewer.md`), never the one that wrote the
   code. It reviews code that already builds and passes.
   - Give it `git diff`, the phase, its D-sections and the Reviewer checklist.
   - It reports real bugs only, each with `file:line` and a concrete failure scenario.
   - Pass effort high for a diff under `src/Platform/`.
6. **Fix what holds up,** then rebuild and re-run the tests on both platforms.
7. **Update the plan:**
   - Tick the phase's `- [ ]` items and flip it to ✅ in the heading and the table.
   - Write down anything that turned out different from the plan. The plan stays the truth for
     later sessions: it is the handoff between phases.
8. **Commit** the code and the plan update together, with the plan's commit message. No Claude or
   AI mention in the message.
9. **Report** what now works, in concrete terms, with the test summaries. Add what the user should
   check on a real Discord, and name the next phase.

**Stop after the phase,** unless the user said "execute the whole plan". Then go on to the next
phase. They may still ask you to pause and push after one. A phase that Needs "your live test" stops at
🟨 until the user answers. Push only when asked.

## After the live test

The user sends short notes ("the card stays after I quit", "the timer restarts"). Fix them all in one
follow-up commit on the same PR. If a note changes a decision, update that D-section too.
