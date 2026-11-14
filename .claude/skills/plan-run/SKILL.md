---
name: plan-run
description: Run a DingoDiscordPresence plan in .claude/plans/ from a keeper session, one sub-session per phase, local or cloud depending on where the keeper runs. Each sub-session implements, builds, tests, reviews, fixes, commits, pushes, updates the PR description and reports back; the keeper updates the plan, cleans the sub-session up and tells the user when a live test is needed. Use when the user says "run <name> with plan-run", "orchestrate <name>", "one session per phase", or "spawn a session for phase N". Not for a single phase in this session: that is "execute <name> phase N" (feature-plan skill).
---

# Plan-run: a keeper session and one sub-session per phase

The `feature-plan` skill runs one phase in the current session. This skill runs the plan from a
**keeper session** that holds only the plan and the reports, and hands each phase to a **sub-session**
that does the per-phase loop (`feature-plan`, "Executing"). The keeper's context stays small, so one
keeper can take a whole plan.

**The rule this mode changes:** a sub-session commits **and pushes** its phase, updates the PR
description if the branch has a PR, and reports back. When it is done, the keeper runs the sub-session's
cleanup (below). This overrides "push only when asked" for a plan run in this mode and nowhere else.
Sub-sessions run **where the keeper runs**, local or cloud.

## Where the sub-sessions run

A sub-session inherits the keeper's environment. That decides what a phase can verify, and the keeper
writes the matching block into every prompt:

| Keeper | Sub-session can | The prompt's "Where you run" block |
| --- | --- | --- |
| **Local, on the Windows dev machine** (the normal case) | Everything in the loop: both platforms, all three configurations | "Windows checkout with WSL. The full loop: Generate, `scripts\windows\Test.bat <Config>` and `wsl -e sh scripts/linux/Test.sh <Config>` for Debug, Release and Distribution." |
| **Cloud** (Linux box) | Implement, review, the Linux builds and tests. **No Windows build** | "Linux cloud box. Build and test the three configurations with `scripts/linux/Test.sh`. The Windows build does not run here: list it under *unverified* and never claim it." |

Prefer a local keeper for any phase that touches `src/Platform/WinPipe.cpp`: only there is it built
and tested. No sub-session ever reaches a real Discord: tests pin pipe index 9.

## Before the first phase (the keeper)

1. **Find the plan** as `feature-plan` says (grep `.claude/plans/` for `execute <name>`). Read it whole.
   Note the open phases, their Needs column and the branch in **How to run it**.
2. **Branch and PR exist before the first phase.**
   - **The branch** is the one the plan's **How to run it** line names. If the plan names none, name it
     in conventional form from the plan's nature and short name (`feat/<name>`, `fix/<name>`,
     `docs/<name>`) and write it into that line. Never a session-generated name.
   - Create the branch from `origin/main` if it is not on `origin`, and switch to it.
   - **Write the run mode into the plan**, one line under **How to run it**, if it is not there:
     `**Run mode:** plan-run (keeper <session id>, local|cloud); each phase is pushed and the PR description updated.`
   - Commit the plan change as `docs(plans): start <name> with plan-run` and push.
   - **The PR**: if `origin/<branch>` has no open PR, open a draft one: title in conventional form from
     the plan's goal, body = the plan's **Goal**, its **At a glance** table, and an empty `## Phases`
     list that each sub-session adds its line to. Mark it ready when the last phase is ✅. The user
     merges.
3. **Checks, stop and say so if one fails:**
   - The session tools are present (start a session, send it a message, read its state, set its title,
     archive it).
   - This session is on the keeper's model: Sonnet at medium. Never Fable: point it out and switch.
   - The permission mode does not prompt. A sub-session inherits the mode, nobody watches it, and a
     prompt stalls it for good.
   - The plan branch and its PR are on `origin`. An unpushed branch is invisible to a sub-session.
4. **Names mean something.** A session-generated branch name like `claude/serene-babbage-nrcnd7` is
   never the plan branch. A worktree is named after the plan (`presence-<name>`), never after the
   session. Session titles carry the plan and the phase.
5. **Phases the keeper never hands out:** a phase whose Needs says "your live test" is spawned only
   after the user's notes are in, with the notes verbatim in the prompt.

## Order

**One sub-session at a time, on the one branch.** Phase N+1 starts when phase N is pushed and the
keeper has read its report. Two in parallel only when neither Needs the other **and** the plan says
they touch different files; the second to finish rebases on `origin/<branch>` before it pushes.

## Spawning a phase

Start a session with:

- a title: `<plan short name> phase N — <phase name>` (the keeper's own title is
  `<plan short name> plan-run`);
- the plan branch as the branch it starts from and pushes to;
- the model omitted (Sonnet), unless the phase touches `src/Platform/`: then say "effort high" in the
  prompt;
- this prompt, filled in. Nothing in the keeper's context reaches the sub-session except the prompt.

```
execute <name> phase N  (feature-plan skill, "Executing"). Read CLAUDE.md and the whole plan first.

You are a plan-run sub-session; the keeper session is <keeper session id>.
Where you run: <the Windows or Linux block from "Where the sub-sessions run">
Deltas from the normal loop:
- Review: a fresh phase-reviewer on the diff, then fix what holds up, rebuild and re-run the tests
  on every platform you can.
- Plan: tick the items, flip the phase to ✅ (or 🟨 if it needs the user), write down what turned
  out different. The plan is the handoff.
- Commit code and plan together with the plan's commit message. No AI mention.
- Branch: all work on <branch>, the plan's branch, never a session-named one. Push:
  git push -u origin <branch> (retry on network errors, 2s/4s/8s/16s).
- PR: the branch's open PR is <PR url>. Update its description: keep the body's structure, add one
  line for this phase under the "## Phases" list (number, name, what the user gets, what is
  unverified). Create no PR.
- Last action: send <keeper session id> exactly this report, then stop:
    plan-run report: <name> phase N
    commit: <sha> on origin/<branch>  (or: NOT PUSHED, why)
    pr: <url, updated> | none
    works now: <2-4 lines, concrete>
    tests: <"N passed, 0 failed" per platform and configuration>
    unverified: <a platform not built, a configuration not run, ...> | none
    review: <findings fixed> / <open, with file:line> | clean
    plan changes: <what differs from the plan> | none
    needs live test: yes — <what to try on a real Discord> | no
    next: phase M — <name> | blocked: <why>
```

## Waiting and the report

- The report arrives in the keeper as a turn from the sub-session. **Do not poll with sleep.**
- **Verify the report, don't trust it:**
  1. `git fetch origin <branch>`; the reported sha is on it, and the plan file changed in that commit.
  2. `git diff <before>..<sha> --stat` matches the phase's files. A change under `src/Platform/` or in
     `include/DingoDiscordPresence/` that the phase did not name is a stop: review it yourself first.
  3. Local keeper: run `scripts\windows\Test.bat Release` yourself on the fetched commit.
  4. Anything under "unverified" is on the user's list, not forgotten.

## Cleaning the sub-session up

The `done` skill is for a **merged PR** and deletes the branch. A sub-session's phase is not merged and
its branch is the plan's, so run only these, from the keeper:

1. `git ls-remote origin <branch>` equals the reported sha. If not, do not archive: tell the
   sub-session to push.
2. The sub-session is idle.
3. Prefix its title with ✅.
4. Archive it.

Never delete the branch here. The real `done` runs once, in the keeper, when the user says the PR
is merged.

## Telling the user

After every phase, one short message: the phase, the commit, what works, and then **one of**:

- "**Live test:** <what to try on a real Discord>" and stop, when the report says so or when the next
  phase Needs it. The phase stays 🟨.
- "Next: phase M, spawning." and spawn it, when nothing is unverified and the user said "run the
  whole plan".

When the last phase is ✅, mark the PR ready for review and say the plan is finished. The user merges;
then `done` in the keeper.
