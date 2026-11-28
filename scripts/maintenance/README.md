<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Maintenance scripts

Housekeeping for the maintainer's clone, the GitHub repository and the
computer used for host tests: deleting merged branches, removing finished
worktrees, local branches and old clones, and listing the Convolver
plug-ins installed before a host test. Nothing here builds or tests
Convolver: those scripts are in [`scripts/`](../README.md), and
[where a script goes](../../docs/develop/architecture.md#where-a-script-goes)
says which folder a new one belongs in. Run each from the root of a
checkout. Each command that fetches or changes something first says which
checkout and remote it works on. Those that delete or move something only
report what they would do until you add `--apply`. That preview deletes
and moves nothing, but it does fetch: remote-tracking branches are updated,
and those of branches deleted on GitHub are pruned. A word or folder a
command does not take stops it instead of being ignored. The host doctor
never fetches or changes anything. Their tests are in
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

  With `--apply` it first saves every local branch, tag and stash (and the
  checkout's commit if it is on a detached HEAD) in
  `~/convolver-retired-DATE/convolver-local-refs-TIME.bundle`, restores the
  bundle into an empty repository to prove that it brings each one back at
  its current commit, and stops if not. It records each tip in
  `BUNDLE.tips` and the bundle's content hash in `BUNDLE.hash`.
  Just before removing a worktree it checks again that it is the one in
  the plan (same place, branch or detached state and commit, still clean
  and unlocked), and keeps it, saying why, if anything changed. It copies
  the worktree's ignored files that Git has never seen (fuzz crashes, logs)
  and the logs and results in its build folders into
  `~/convolver-retired-DATE/NAME-kept/`, with a manifest; build products go
  with the worktree. A local branch is deleted only if still at the planned
  commit and checked out nowhere, and its `branch.NAME` settings go with it.
  Restore a branch with `git fetch BUNDLE 'refs/heads/NAME:refs/heads/NAME'`.
  Refs already under `refs/retired-head/` or `refs/retired-stash/`, such as
  ones restored from an earlier bundle, are saved and recorded too, and left
  in place. A commit that only one of those refs, or the main checkout's
  detached HEAD, names counts as local work: `empty-retired` keeps that
  backup until the commit is on a branch or on GitHub, or you add
  `--delete-local-work`.
- **`find-clones`** lists every checkout or worktree of the repository under
  a folder (your home folder by default, skipping `Library` and the Trash).
  For each it gives the branch, size, uncommitted files and commits not on
  GitHub. It changes nothing.
- **`retire-dir`** saves a clone's branches, tags and stashes, and its
  commit if it is on a detached HEAD (as `refs/retired-head/HEAD`, or
  `HEAD-2` and so on if that name is taken), in
  `~/convolver-retired-DATE/NAME-kept/refs.bundle`, proves as `tidy` does
  that the bundle restores them all, and copies there, with a manifest, the files whose content Git
  has never seen and the logs and results in its build folders. It then
  moves the whole clone into the retired folder. The folder must be found
  on its own: it refuses anything that is, holds or is inside the checkout
  it works on or that checkout's Git data, a folder holding another Git
  checkout below its top (retire that one first), a clone with linked
  worktrees elsewhere, whose Git data it holds, even one whose folder is
  not there now (remove, retire or prune those first), a folder that holds
  REAPER projects, which refer to audio by path, and an in-source build.
- **`verify-backup`** answers "could I restore what the cleanup said it
  saved?" for each retired folder (or those named). Every bundle must be
  unchanged since it was made (`BUNDLE.hash`), and must restore into an
  empty repository with each branch, tag, stash and detached HEAD tip
  recorded then (`BUNDLE.tips`); `git bundle verify` alone reads only the
  bundle's header, so a damaged bundle can pass it. Every copied file must
  match the manifest (`NAME-kept/manifest.txt`). It names anything missing
  or changed, and after checking every folder exits with an error if
  anything was. It also
  names what the folder holds that exists nowhere else: commits neither on
  GitHub nor in the checkout, copied files Git never saw, and the copied
  logs and results from build folders (a measurement from one machine or
  host session cannot be rebuilt). CTest's own run logs, under
  `Testing/Temporary/`, are copied and checked but not counted, since
  nearly every tested build has them. Recoverable
  from this folder is not the same as safe to delete. Commits that only a
  reflog names are not saved, by any command. Backups made before
  CI-57 have no such record: `tidy`'s are checked against the branches and
  tags it listed in `STAMP-refs.txt` (stashes were not listed), and other
  bundles and copied files are reported as missing their record.
- **`empty-retired`** deletes `~/convolver-retired-YYYY-MM-DD` folders
  dated more than seven days ago, after running `verify-backup` on each. It
  keeps a folder missing anything its backups recorded, and one holding the
  only known copy of local work unless given `--delete-local-work`; copy
  what you need elsewhere first: read the list `verify-backup` prints, since
  the flag lets go of everything on it.

Every step that changes something asks you to type `yes` first.

## Installed plug-ins before a host test

[`host_doctor.py`](host_doctor.py) lists the Convolver plug-ins on this
computer, so a host test does not start on the wrong build (HOST-14). The
Audacity timing of 27 November 2026 began with 0.19.21 downloaded and
0.18.23 still installed. It needs Python 3.8 or later (macOS's
`/usr/bin/python3` will do) and a checkout, because it reads
[`plugins/identities.json`](../../plugins/identities.json).

```sh
python3 scripts/maintenance/host_doctor.py                    # compare with the newest download
python3 scripts/maintenance/host_doctor.py --expect 0.19.21   # or with a version you name
```

It reads, and never changes, the folders hosts scan: on macOS the VST3 and
Audio Unit folders for your account (`~/Library/Audio/Plug-Ins/`) and for
all accounts (`/Library/Audio/Plug-Ins/`); on Linux `~/.vst3`, `/usr/lib/vst3`
and `/usr/local/lib/vst3`, and the folders of `LV2_PATH` or, when it is not
set, `~/.lv2`, `/usr/lib/lv2` and `/usr/local/lib/lv2`. A bundle is
Convolver's when its bundle identifier, VST3 class ID or LV2 URI is the one
in `plugins/identities.json`; only those count as installed. A bundle that
is only named Convolver is listed as carrying another identity and left out
of the checks, so a third-party bundle with a similar name never passes for
an install, even with `--expect`. A bundle named Convolver whose identity
cannot be read is listed as a problem, not as an install. A symbolic link to
a bundle already listed is shown as a link to it, not as another copy; the
bundle itself is the entry, whichever folder is read first and whatever
links lie in the folders above it (macOS's `/var` is a link to
`/private/var`). For each bundle it prints the format, the version (from
`Info.plist`, the VST3 `moduleinfo.json` or the LV2 Turtle files), user or
system scope, path and the date of its files (the package's build time,
which copying keeps, not when it was installed). It keeps three things
apart:

- **found**: the bundle is in a folder hosts scan. Which copy a host loads
  next is still the host's choice;
- **opened**: a running program has some of the bundle's files open, such as
  Spotlight or a host's scanner reading `Info.plist`. The line says that the
  executable is not mapped, so the plug-in is not loaded;
- **loaded**: a running program has the bundle's executable
  (`Contents/MacOS/…`, a VST3 architecture folder, or the LV2 `.so`) mapped
  as code, and it is the file now on disk (`lsof` for your own processes on
  macOS, `/proc` on Linux). Only this confirms which copy a host uses. On
  Linux a mapping counts only with execute permission, since a scanner can
  map the library read-only to read it; `lsof` on macOS does not show the
  permission, so there any mapping of the executable (`txt`) counts.

When a program started before the bundle was reinstalled still has the old
executable mapped (the mapped file is deleted, or its inode differs from the
file now on disk), it says that an earlier copy, since replaced, is loaded:
quit and reopen that program before testing. When no program has it loaded
it says **not seen loaded in the processes checked**, and names them: only
your own processes with `lsof`, or those `/proc` lets you read. Start the
host, add Convolver, and run the doctor again while it is open. If `lsof`
fails or lists nothing, it says that it does not know instead.

It then lists what needs fixing, and exits with status 1 if anything does:
no plug-in with Convolver's identity installed, a bundle named Convolver
whose identity cannot be read, a format installed more than once (user and
system, or a renamed copy), formats at different versions, a version it
cannot read, an installed version other than `--expect`, or a program still
running a replaced copy. Without `--expect` it compares with the newest
`Convolver-X.Y.Z-…` package or folder for this platform in `~/Downloads`
(choose another with `--downloads`) and names any older install; when it
finds none, or cannot read the folder (macOS may refuse Terminal access to
Downloads), it says that no versions were compared. It removes nothing:
delete the copies you do not want yourself.

Its tests build bundles from the build's own `Info.plist` templates and
LV2 Turtle generator, and run on Linux, including a real process that
keeps a replaced file mapped and a library mapped read-only. On macOS only
the folder layout (with fixture bundles) and the reading of `lsof -F pcfin`
output (with sample text, so the tests need no `lsof`) are tested; the tests reach every folder through a symbolic link, as `/var`
is on a Mac. Not yet checked on a Mac: that `lsof` lists the executable of
a loaded bundle as `txt` for REAPER, Audacity and GarageBand's Audio Unit
host, whether a scanner that only reads the executable shows the same way,
how it shows a replaced file, and how long it takes.
