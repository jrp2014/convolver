<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Maintenance scripts

Housekeeping for the maintainer's clone and the GitHub repository: deleting
merged branches, and removing finished worktrees, local branches and old
clones. Nothing here builds or tests Convolver: those scripts are in
[`scripts/`](../README.md), and
[where a script goes](../../docs/develop/architecture.md#where-a-script-goes)
says which folder a new one belongs in. Run each from the root of a
checkout. Each one only reports what it would do until you add `--apply`.
Their tests are in [`tests/maintenance_tests.py`](../../tests/maintenance_tests.py).

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

Each delete happens only if the branch is still at the commit checked
(`--force-with-lease`). Each one's restore command is printed and appended
to `.git/deleted-branches.log`. Tags are not touched.

## The local workspace

[`workspace.sh`](workspace.sh) works on the checkout that holds it (choose
another with `--repo PATH`) and runs with the bash that ships with macOS.
Run `delete_merged_branches.py` first, so finished branches are already gone
from GitHub.

```sh
scripts/maintenance/workspace.sh find-clones [--under ~/source]   # where are the checkouts?
scripts/maintenance/workspace.sh tidy [--apply]                   # finished worktrees and branches
scripts/maintenance/workspace.sh empty-retired [--apply]          # retired folders over a week old
scripts/maintenance/workspace.sh retire-dir DIR [--apply]         # move an old clone away
scripts/maintenance/workspace.sh update-main                      # fast-forward main
```

- **`tidy`** fetches the branches and every pull request's head (kept under
  `refs/convolver-pr/`, so `git branch -r` is unchanged). It never touches
  the main checkout or the worktree it runs from. It removes another
  worktree only if the worktree has no uncommitted or untracked files, its
  commit is on GitHub (in a branch or a pull request's head), and its branch
  is no longer on GitHub. Before removing it, it copies the worktree's
  ignored files that Git has never seen (fuzz crashes, logs) and the logs and
  results in its build folders into `~/convolver-retired-DATE/NAME-kept/`;
  build products go with the worktree. It deletes a local branch on the same
  terms, squash-merged ones included, but never one that a worktree still
  has checked out. Everything else is kept and listed, with the commits that
  exist only on this machine. With `--apply` it first saves every local
  branch, tag and stash in
  `~/convolver-retired-DATE/convolver-local-refs-TIME.bundle`. Restore one
  with `git fetch BUNDLE 'refs/heads/NAME:refs/heads/NAME'`.
- **`find-clones`** lists every checkout or worktree of the repository under
  a folder (your home folder by default, skipping `Library` and the Trash).
  For each it gives the branch, size, uncommitted files and commits not on
  GitHub. It changes nothing.
- **`retire-dir`** saves a clone's branches, tags and stashes in
  `~/convolver-retired-DATE/NAME-kept/refs.bundle`, and copies there the
  files whose content Git has never seen and the logs and results in its
  build folders. It then moves the whole clone into the retired folder. It
  refuses the checkout it works on, the folder holding its Git data, and a
  folder that holds REAPER projects, which refer to audio by path.
- **`empty-retired`** deletes `~/convolver-retired-YYYY-MM-DD` folders
  dated more than seven days ago, and names each backup bundle in them
  first.

Every step that changes something asks you to type `yes` first.
