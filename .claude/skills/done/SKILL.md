---
name: done
description: Clean up a DingoNametag session after the user merged its PR. Use when the user says "done", "I merged the PR", "mark this session done", "clean up the session" or "clean up this branch". Checks the PR is merged and the checkout is clean, deletes the local branch, marks the session title with a check mark and archives the session.
---

# Session cleanup after a merged PR

**"done"** means "I merged the PR. Mark this session done and clean it up." This needs: `gh` must be authenticated against `KingoBoiii/DingoNametag` (check `gh auth status`) and the session tools present; stop and say so if not.
Do these in order:

1. **Check it's safe.** `gh pr view <N> -R KingoBoiii/DingoNametag --json state,headRefOid` must say
   `MERGED`. `git rev-parse HEAD` must equal `headRefOid`, and `git status` must be clean. If any check
   fails, stop and say what's left (PR not merged, unpushed commits, uncommitted changes).
2. **Delete the local branch.** In a worktree: `git checkout --detach`, then `git branch -D <branch>`
   (`-D` because a squash merge isn't an ancestor of `main`; step 1 already proved it merged). In the
   main checkout: `git switch main`, `git pull`, then `git branch -D <branch>`. Then
   `git fetch --prune origin`.
3. **Mark it done.** Prefix the session title with ✅ (`set_session_title`, session `self`).
4. **Archive the session** (`archive_session`, session `self`). The app stops the session and removes
   its worktree, and the conversation ends, so this goes last.
