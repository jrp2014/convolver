<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Maintenance scripts

Housekeeping for the maintainer's clone and the GitHub repository: deleting
merged branches, and removing finished worktrees, local branches and old
clones. Nothing here builds or tests Convolver. Run each from the root of a
checkout. Each command that fetches or changes something first says which
checkout and remote it works on. Those that delete or move something only
report what they would do until you add `--apply`. That preview deletes
and moves nothing, but it does fetch: remote-tracking branches are updated,
and those of branches deleted on GitHub are pruned. A word or folder a
command does not take stops it instead of being ignored. Their tests are in
[`tests/maintenance_tests.py`](../../tests/maintenance_tests.py).

## Merged branches on GitHub

Merged branches are kept for a while and deleted by jrp from time to time
([merging and afterwards](../../docs/develop/workflow.md#merging-and-afterwards)).
Cloud agent sessions cannot delete branches, so jrp runs this from a clone:

```sh
python3 scripts/maintenance/delete_merged_branches.py            # look
python3 scripts/maintenance/delete_merged_branches.py --apply    # delete
```

It reads the pull requests from GitHub's API (set `GITHUB_TOKEN` or
`GH_TOKEN` for a higher rate limit) and deletes a branch only if a merged
pull request recorded the branch's current commit as its head and no pull
request from the branch is open. Such a branch can always be brought back:
GitHub keeps that commit as `refs/pull/N/head` and shows **Restore branch**
on the pull request. Branches of pull requests closed without merging are
deleted only with `--closed`. It keeps, and lists with the reason, the
default branch, branches with an open pull request, branches an open pull
request merges into, branches with commits pushed after their last pull
request, and branches with no pull request.

It prints the repository it read the pull requests from and every one
`git push origin` deletes from (origin's push URLs), and stops before
deleting anything if any of them is another repository. Each delete happens only if the branch is
still at the commit checked (`--force-with-lease`). Each one's restore
command is printed and appended to `.git/deleted-branches.log`. Tags are
not touched.

## The local workspace

[`workspace.sh`](workspace.sh) works on the checkout that holds it (choose
another with `--repo PATH`) and runs with the bash that ships with macOS.
Run `delete_merged_branches.py` first, so finished branches are already gone
from GitHub.

```sh
scripts/maintenance/workspace.sh find-clones [--under ~/source]   # where are the checkouts?
scripts/maintenance/workspace.sh tidy [--verbose] [--apply]       # finished worktrees and branches
scripts/maintenance/workspace.sh retire-dir DIR [--apply]         # move an old clone away
scripts/maintenance/workspace.sh verify-backup [FOLDER...]        # do the retired folders hold what they saved?
scripts/maintenance/workspace.sh empty-retired [--apply]          # retired folders over a week old
scripts/maintenance/workspace.sh update-main                      # fast-forward main
```

Removing finished work, retiring a clone and permanently deleting a retired
folder are separate commands because their consequences differ: the first
two keep a backup, the last does not.

- **`tidy`** fetches the branches and every pull request's head (kept under
  `refs/convolver-pr/`, so `git branch -r` is unchanged), and reads the
  merged pull requests from GitHub with `delete_merged_branches.py`. Its
  plan opens with `Remove: N · Review: N · Kept: N`, then lists what it
  would remove and what it leaves for review, one reason each; `--verbose`
  adds what it keeps and why.
  - **Remove**: a worktree or local branch that a merged pull request
    recorded under the branch's own name at this exact commit, whose branch
    is gone from GitHub, and, for a worktree, with no uncommitted or
    untracked files and no lock. Squash-merged branches count.
  - **Review**: GitHub has the commit, so it can be recovered, but nothing
    says the work is finished: a branch just made from `main` or from a
    merged branch for new work (its upstream is not evidence), a pull
    request still open or closed unmerged, a branch finished some other
    way, a detached worktree, and a worktree that builds in its own top
    folder (an in-source build, whose source and build output cannot be
    told apart).
    `tidy` never removes these; each comes with the command to remove it
    yourself.
  - **Kept**: the main checkout, the worktree `tidy` runs from, locked or
    changed worktrees, branches still on GitHub or checked out in a worktree
    that stays, and anything with commits only on this machine (listed).

  With `--apply` it first saves every local branch, tag and stash in
  `~/convolver-retired-DATE/convolver-local-refs-TIME.bundle` and checks
  that the bundle holds each one at its current commit, stopping if not.
  Just before removing a worktree it checks again that it is the one in
  the plan (same place, branch or detached state and commit, still clean
  and unlocked), and keeps it, saying why, if anything changed. It copies
  the worktree's ignored files that Git has never seen (fuzz crashes, logs)
  and the logs and results in its build folders into
  `~/convolver-retired-DATE/NAME-kept/`, with a manifest; build products go
  with the worktree. A local branch is deleted only if still at the planned
  commit and checked out nowhere, and its `branch.NAME` settings go with it.
  Restore a branch with `git fetch BUNDLE 'refs/heads/NAME:refs/heads/NAME'`.
- **`find-clones`** lists every checkout or worktree of the repository under
  a folder (your home folder by default, skipping `Library` and the Trash).
  For each it gives the branch, size, uncommitted files and commits not on
  GitHub. It changes nothing.
- **`retire-dir`** saves a clone's branches, tags and stashes in
  `~/convolver-retired-DATE/NAME-kept/refs.bundle`, checks that it holds
  them all, and copies there, with a manifest, the files whose content Git
  has never seen and the logs and results in its build folders. It then
  moves the whole clone into the retired folder. The folder must be found
  on its own: it refuses anything that is, holds or is inside the checkout
  it works on or that checkout's Git data, a folder holding another Git
  checkout below its top (retire that one first), a folder that holds
  REAPER projects, which refer to audio by path, and an in-source build.
- **`verify-backup`** answers "could I restore what the cleanup said it
  saved?" for each retired folder (or those named). Every bundle must
  verify and hold each branch, tag and stash tip recorded when it was made
  (`BUNDLE.tips`), and every copied file must match the manifest
  (`NAME-kept/manifest.txt`). It names anything missing or changed. It also
  names what the folder holds that exists nowhere else: commits neither on
  GitHub nor in the checkout, and copied files Git never saw. Recoverable
  from this folder is not the same as safe to delete. Backups made before
  CI-57 have no such record: `tidy`'s are checked against the branches and
  tags it listed in `STAMP-refs.txt` (stashes were not listed), and other
  bundles and copied files are reported as missing their record.
- **`empty-retired`** deletes `~/convolver-retired-YYYY-MM-DD` folders
  dated more than seven days ago, after running `verify-backup` on each. It
  keeps a folder missing anything its backups recorded, and one holding the
  only known copy of local work unless given `--delete-local-work`; copy
  what you need elsewhere first.

Every step that changes something asks you to type `yes` first.
