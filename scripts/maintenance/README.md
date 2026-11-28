<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Maintenance scripts

Housekeeping for the maintainer's clone, the GitHub repository and the
computer used for host tests: deleting merged branches, removing finished
worktrees, local branches and old clones, and listing the Convolver
plug-ins installed before a host test. Nothing here builds or tests
Convolver. Run each from the root of a checkout. The branch and workspace
scripts only report what they would do until you add `--apply`; the host
doctor never changes anything. Their tests are in
[`tests/maintenance_tests.py`](../../tests/maintenance_tests.py) and
[`tests/host_doctor_tests.py`](../../tests/host_doctor_tests.py).

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

## Installed plug-ins before a host test

[`host_doctor.py`](host_doctor.py) lists the Convolver plug-ins on this
computer, so a host test does not start on the wrong build (HOST-14). The
Audacity timing of 27 November 2026 began with 0.19.21 downloaded and
0.18.23 still installed.

```sh
python3 scripts/maintenance/host_doctor.py                    # compare with the newest download
python3 scripts/maintenance/host_doctor.py --expect 0.19.21   # or with a version you name
```

It reads, and never changes, the folders hosts scan: on macOS the VST3 and
Audio Unit folders for your account (`~/Library/Audio/Plug-Ins/`) and for
all accounts (`/Library/Audio/Plug-Ins/`); on Linux `~/.vst3`, `~/.lv2`,
their `/usr/lib` and `/usr/local/lib` counterparts and `LV2_PATH`. A bundle
counts as Convolver's when its bundle identifier, VST3 class ID or LV2 URI is
the one in [`plugins/identities.json`](../../plugins/identities.json), or
its name contains "Convolver". For each it prints the format, the version
(from `Info.plist`, the VST3 `moduleinfo.json` or the LV2 Turtle files),
user or system scope, path and modification time, and then:

- **loaded now by** a program and its process ID, when a running process has
  the bundle's files open or mapped (`lsof` on macOS, `/proc` on Linux).
  Only this confirms which copy a host uses;
- **not loaded by any running program** otherwise. The plug-in is installed,
  but which copy the host loads next is the host's choice: start the host,
  add Convolver, and run the doctor again while it is open.

It then lists what needs fixing, and exits with status 1 if anything does:
no plug-in installed, a format installed more than once (user and system,
or a renamed copy), formats at different versions, a version it cannot
read, or an installed version other than `--expect`. Without `--expect` it
compares with the newest `Convolver-X.Y.Z-…` package or folder in
`~/Downloads` (choose another with `--downloads`) and names any older
install. It removes nothing: delete the copies you do not want yourself.

Its tests build bundles from the build's own `Info.plist` templates and
LV2 Turtle generator, and run on Linux. The macOS folders and `lsof`
output are tested with fixtures only; the doctor has not yet been run on a
Mac with a host open.
