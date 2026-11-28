#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Tidy a local Convolver workspace: finished worktrees, local branches, old
# clones and retired folders (CI-56, CI-57). Bash 3.2, as shipped with macOS, is fine.
#
#   tidy            remove worktrees and local branches a merged pull request accounts for
#   find-clones     list every Convolver checkout under a folder (read-only)
#   retire-dir      keep any unknown source, logs and results, then move a clone away
#   verify-backup   check that the retired folders hold what each backup recorded
#   empty-retired   delete ~/convolver-retired-* folders a week old that verify-backup passes
#   update-main     switch the checkout to an up-to-date main
#   backup          bundle every local branch, tag and stash into the retired folder
#
# tidy, retire-dir and empty-retired change nothing unless given --apply; each
# then asks you to type yes. Run from the root of a checkout:
#   scripts/maintenance/workspace.sh find-clones [--under ~/source]
#   scripts/maintenance/workspace.sh tidy [--verbose] [--apply]
#   scripts/maintenance/workspace.sh retire-dir ~/source/old-clone [--apply]
#   scripts/maintenance/workspace.sh verify-backup [RETIRED-FOLDER...]
#   scripts/maintenance/workspace.sh empty-retired [--apply] [--delete-local-work]
#   scripts/maintenance/workspace.sh update-main
#
# The checkout is the one holding this script; --repo PATH chooses another.
# Removed folders and branches go into RETIRE (default
# ~/convolver-retired-YYYY-MM-DD) or a backup bundle there, except worktrees
# and branches that GitHub still has. See scripts/maintenance/README.md.

set -eu

TODAY=$(date +%Y-%m-%d)
RETIRE=${RETIRE:-"$HOME/convolver-retired-$TODAY"}
BUNDLE=""
BUNDLED_STASHES=0
BUNDLED_REFS=0
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$SCRIPT_DIR/../.." && pwd)
UNDER=""
ARGS=""
APPLY=no
VERBOSE=no
DELETE_LOCAL_WORK=no
TAB=$(printf '\t')
# Separates the fields of a planned worktree; unlike a tab, an empty field
# (a detached worktree's branch) survives read.
SEP=$(printf '\037')
# shellcheck disable=SC2034 # used through eval in add_line
NL='
'

TMPLIST=$(mktemp "${TMPDIR:-/tmp}/convolver-workspace.XXXXXX")
trap 'rm -rf "$TMPLIST.restore"; rm -f "$TMPLIST" "$TMPLIST".*' EXIT

die() { printf 'Stopped: %s\n' "$*" >&2; exit 1; }

say() { printf '%s\n' "$*"; }

section() { printf '\n== %s ==\n' "$*"; }

confirm() {
  printf '%s [type yes to go on] ' "$1"
  read -r answer
  [ "$answer" = "yes" ]
}

add_line() { # add_line VAR VALUE: append VALUE as a line to the list in VAR
  eval "_cur=\${$1}"
  if [ -z "$_cur" ]; then eval "$1=\$2"; else eval "$1=\${_cur}\${NL}\$2"; fi
}

parse() {
  while [ $# -gt 0 ]; do
    case "$1" in
      --repo) [ $# -ge 2 ] || die "--repo needs a path"; REPO=$2; shift 2 ;;
      --under) [ $# -ge 2 ] || die "--under needs a path"; UNDER=$2; shift 2 ;;
      --apply) APPLY=yes; shift ;;
      --verbose) VERBOSE=yes; shift ;;
      --delete-local-work) DELETE_LOCAL_WORK=yes; shift ;;
      -*) die "unknown option $1" ;;
      *) add_line ARGS "$1"; shift ;;
    esac
  done
}

# no_args COMMAND: refuse a folder or word the command does not take, rather
# than ignoring it (tidy wrong-path once ran tidy on the default checkout).
no_args() {
  [ -z "$ARGS" ] || die "$1 takes no argument, but was given '$(printf '%s\n' "$ARGS" | head -1)'; choose the checkout with --repo PATH"
}

need_repo() {
  [ -n "$REPO" ] || die "give the main checkout with --repo PATH"
  git -C "$REPO" rev-parse --git-dir >/dev/null 2>&1 || die "$REPO is not a Git checkout"
  # The checkout's top folder, even when --repo names a folder inside it.
  # A .git folder or bare repository has none: refuse it rather than let
  # cd "" fall back to the current folder.
  _top=$(git -C "$REPO" rev-parse --show-toplevel 2>/dev/null) && [ -n "$_top" ] ||
    die "$REPO is not inside a checkout's working tree; give the checkout's top folder with --repo PATH"
  REPO=$(cd "$_top" && pwd)
}

# Say which checkout and remote the command works on, before anything else.
header() {
  _fetch=$(git -C "$REPO" remote get-url origin 2>/dev/null || echo "none")
  _push=$(git -C "$REPO" remote get-url --push origin 2>/dev/null || echo "none")
  say "Checkout: $REPO"
  if [ "$_fetch" = "$_push" ]; then say "Remote:   origin = $_fetch"; else say "Remote:   origin = $_fetch (pushes to $_push)"; fi
}

fetch() {
  say "Fetching origin (updates remote-tracking refs only; no files change)..."
  git -C "$REPO" fetch --prune origin '+refs/heads/*:refs/remotes/origin/*' --tags >/dev/null || die "could not fetch origin; nothing changed"
}

human_size() { du -sh "$1" 2>/dev/null | cut -f1; }

# Directories that hold build output: any folder with a CMakeCache.txt.
build_roots() { find "$1" -name CMakeCache.txt -not -path '*/_deps/*' -print 2>/dev/null | sed 's#/CMakeCache.txt$##'; }

# Does DIR build in its own top folder, so build output and source are mixed?
in_source_build() { [ -f "$1/CMakeCache.txt" ]; }

# Is DIR itself the top of a readable Git checkout (not merely inside one)?
own_checkout() {
  _top=$(git -C "$1" rev-parse --show-toplevel 2>/dev/null) || return 1
  [ "$(cd "$_top" && pwd -P)" = "$(cd "$1" && pwd -P)" ]
}

# Files under DIR whose content is not in REPO's object store: possible
# uncommitted work (ignored files such as fuzz crashes and logs included). Build folders, .git and
# caches are skipped. Content Git knows (an older or newer committed
# version) is not listed, so a stale copy of the source is not "work".
unknown_sources() {
  _roots=$(build_roots "$1")
  find "$1" \( -name .git -o -name .cache -o -name node_modules \) -prune -o -type f -print 2>/dev/null |
    while IFS= read -r f; do
      case "$f" in *.o|*.a|*.dylib|*.so|*.pyc|*/.DS_Store) continue ;; esac
      _skip=no
      if [ -n "$_roots" ]; then
        while IFS= read -r r; do case "$f" in "$r"/*) _skip=yes; break ;; esac; done <<EOF_ROOTS
$_roots
EOF_ROOTS
      fi
      # Not "[ no ] && printf": as the loop's last command, a skipped last
      # file would fail the pipeline and stop the script under set -e.
      [ "$_skip" = yes ] || printf '%s\n' "$f"
    done >"$TMPLIST"
  [ -s "$TMPLIST" ] || return 0
  git -C "$REPO" hash-object --no-filters --stdin-paths <"$TMPLIST" |
    git -C "$REPO" cat-file --batch-check='%(objectname)' >"$TMPLIST.check" 2>/dev/null || true
  # A file that vanished or could not be read stops hash-object early; then
  # the lists no longer line up, and nothing can be called known.
  [ "$(wc -l <"$TMPLIST.check")" -eq "$(wc -l <"$TMPLIST")" ] ||
    die "could not read every file under $1 (one vanished or has a newline in its name); nothing changed"
  paste -d '\t' "$TMPLIST.check" "$TMPLIST" | awk -F '\t' '$1 ~ / missing$/ { print $2 }'
  rm -f "$TMPLIST.check"
}

# Logs and results inside build folders worth keeping.
build_evidence() {
  build_roots "$1" | while IFS= read -r b; do
    find "$b" \( -name CMakeFiles -o -name _deps -o -name '*-prefix' -o -name vcpkg_installed -o -name '*.vst3' -o -name '*.component' -o -name '*.lv2' \) -prune -o \
      -type f \( -name '*.log' -o -name 'junit*.xml' -o -name '*benchmark*' -o -name '*.csv' -o -name 'results*' -o -name '*.md' \) -print 2>/dev/null
  done
}

# keep_copies DIR DEST: copy DIR's unknown files and build logs and results
# into DEST/files, with their lists beside them, and print how many. Writes
# DEST/manifest.txt (content hash, "source" or "build", path under files/),
# which verify-backup checks before the copies may expire. Fails (and the
# caller removes nothing) if any step fails.
keep_copies() {
  unknown_sources "$1" >"$TMPLIST.unknown" || return 1
  build_evidence "$1" >"$TMPLIST.evidence" || return 1
  mkdir -p "$2/files" || return 1
  cp "$TMPLIST.unknown" "$2/unknown-sources.txt" || return 1
  cp "$TMPLIST.evidence" "$2/build-evidence.txt" || return 1
  : >"$TMPLIST.rel" || return 1
  for _kind in source build; do
    _list="$TMPLIST.unknown"; [ "$_kind" = source ] || _list="$TMPLIST.evidence"
    while IFS= read -r f; do
      [ -n "$f" ] || continue
      rel=${f#"$1"/}
      mkdir -p "$2/files/$(dirname "$rel")" || return 1
      cp -p "$f" "$2/files/$rel" || return 1
      printf '%s\t%s\n' "$_kind" "$rel" >>"$TMPLIST.rel" || return 1
    done <"$_list"
  done
  : >"$2/manifest.txt" || return 1
  while IFS="$TAB" read -r _kind rel; do
    _h=$(git -C "$REPO" hash-object --no-filters -- "$2/files/$rel") || return 1
    printf '%s\t%s\t%s\n' "$_h" "$_kind" "$rel" >>"$2/manifest.txt" || return 1
  done <"$TMPLIST.rel"
  wc -l <"$TMPLIST.rel" | tr -d ' '
}

# fresh_dir PATH: PATH, or PATH-2, PATH-3... if it exists, so that nothing
# saved earlier is overwritten.
fresh_dir() {
  _d=$1; _n=2
  while [ -e "$_d" ]; do _d="$1-$_n"; _n=$((_n + 1)); done
  printf '%s\n' "$_d"
}

# bundle_unrestored BUNDLE TIPS: restore BUNDLE into an empty repository, as
# a recovery would, and print each "SHA REF" line of TIPS that did not come
# back (every line when it cannot be restored). Unlike git bundle verify,
# which reads only the header, this reads and checks every object (fetch
# indexes the whole pack and checks that every tip's history is there).
bundle_unrestored() {
  _bf="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
  rm -rf "$TMPLIST.restore"
  if git init -q --bare "$TMPLIST.restore" >/dev/null 2>&1 &&
    git -C "$TMPLIST.restore" fetch -q "$_bf" '+refs/*:refs/*' >/dev/null 2>&1 &&
    git -C "$TMPLIST.restore" for-each-ref --format='%(objectname) %(refname)' >"$TMPLIST.restored"; then
    grep -vxF -f "$TMPLIST.restored" "$2" || true
  else
    cat "$2"
  fi
  rm -rf "$TMPLIST.restore"
}

# bundle_refs CHECKOUT FILE: save the checkout's branches, tags and stashes,
# and a detached HEAD, in FILE; their tips in FILE.tips; and FILE's content
# hash in FILE.hash. Stops, before anything is removed, unless the bundle
# restores into an empty repository with every one of those tips. Returns 1
# when the checkout has none.
bundle_refs() {
  _tips="$2.tips"
  # Refs already under refs/retired-*, such as ones restored from an earlier
  # bundle, are saved and recorded like branches, and left in place.
  git -C "$1" for-each-ref --format='%(objectname) %(refname)' refs/heads refs/tags refs/retired-head refs/retired-stash >"$_tips"
  : >"$TMPLIST.made"
  # A bundle takes refs only, so give a detached HEAD and each stash a
  # temporary ref, under a name not yet taken: a commit no branch names
  # would otherwise be left out.
  if ! git -C "$1" symbolic-ref -q HEAD >/dev/null 2>&1 && _h=$(git -C "$1" rev-parse -q --verify HEAD 2>/dev/null); then
    _r=refs/retired-head/HEAD; _n=2
    while git -C "$1" rev-parse -q --verify "$_r" >/dev/null 2>&1; do _r="refs/retired-head/HEAD-$_n"; _n=$((_n + 1)); done
    git -C "$1" update-ref "$_r" "$_h"
    printf '%s %s\n' "$_h" "$_r" >>"$_tips"
    printf '%s\n' "$_r" >>"$TMPLIST.made"
  fi
  _i=0; _n=0
  for _s in $(git -C "$1" stash list --format=%H); do
    while git -C "$1" rev-parse -q --verify "refs/retired-stash/$_n" >/dev/null 2>&1; do _n=$((_n + 1)); done
    git -C "$1" update-ref "refs/retired-stash/$_n" "$_s"
    printf '%s refs/retired-stash/%s\n' "$_s" "$_n" >>"$_tips"
    printf 'refs/retired-stash/%s\n' "$_n" >>"$TMPLIST.made"
    _i=$((_i + 1)); _n=$((_n + 1))
  done
  if [ ! -s "$_tips" ]; then rm -f "$_tips"; return 1; fi
  _made=yes
  git -C "$1" bundle create "$2" --branches --tags --glob='refs/retired-stash/*' --glob='refs/retired-head/*' >/dev/null 2>&1 || _made=no
  # Remove only the temporary refs made above.
  while IFS= read -r r; do git -C "$1" update-ref -d "$r"; done <"$TMPLIST.made"
  [ "$_made" = yes ] || die "could not create the bundle $2; nothing removed"
  git -C "$1" bundle verify "$2" >/dev/null 2>&1 || die "the bundle $2 does not verify; nothing removed"
  _missing=$(bundle_unrestored "$2" "$_tips")
  [ -z "$_missing" ] || die "the bundle $2 does not restore $(printf '%s\n' "$_missing" | awk '{ print $2 }' | tr '\n' ' '); nothing removed"
  git -C "$REPO" hash-object --no-filters -- "$2" >"$2.hash"
  BUNDLED_STASHES=$_i
  BUNDLED_REFS=$(wc -l <"$_tips" | tr -d ' ')
}

cmd_backup() {
  need_repo
  mkdir -p "$RETIRE"
  RETIRE=$(cd "$RETIRE" && pwd)
  BUNDLE="$RETIRE/convolver-local-refs-$(date +%H%M%S).bundle"
  [ ! -e "$BUNDLE" ] || BUNDLE="${BUNDLE%.bundle}-$$.bundle"  # never overwrite an earlier one
  bundle_refs "$REPO" "$BUNDLE" || die "no branches, tags or stashes to save"
  stamp=$(basename "$BUNDLE" .bundle)
  git -C "$REPO" worktree list --porcelain >"$RETIRE/$stamp-worktrees.txt"
  git -C "$REPO" for-each-ref --format='%(objectname) %(refname)' refs/heads refs/tags >"$RETIRE/$stamp-refs.txt"
  say "Saved every local branch and tag$( [ "$BUNDLED_STASHES" -gt 0 ] && echo " and $BUNDLED_STASHES stash(es)") in $BUNDLE"
  say "  (checked: it holds all $BUNDLED_REFS of them at their current commits)"
  say "Restore one later with: git fetch $BUNDLE 'refs/heads/NAME:refs/heads/NAME'"
}

# The folder that holds REPO's Git data (the main checkout's .git).
common_git_dir() { (cd "$REPO" && cd "$(git rev-parse --git-common-dir)" && pwd -P); }

# overlaps A B: is either folder A or B the same as, or inside, the other?
overlaps() {
  case "$1/" in "$2"/*) return 0 ;; esac
  case "$2/" in "$1"/*) return 0 ;; esac
  return 1
}

cmd_retire_dir() {
  need_repo
  [ "$(printf '%s\n' "$ARGS" | grep -c .)" -eq 1 ] || die "name exactly one folder to retire"
  d=$ARGS
  [ -d "$d" ] || die "$d is not an existing folder"
  d=$(cd "$d" && pwd -P)
  active=$(cd "$(git -C "$REPO" rev-parse --show-toplevel)" && pwd -P)
  own_git=$(cd "$(git -C "$REPO" rev-parse --absolute-git-dir)" && pwd -P)
  # The target must be found on its own, never part of the checkout this
  # script works on or of its Git data (.git/objects once got through).
  overlaps "$d" "$active" && die "$d is, holds or is inside the checkout this script works on ($active); refusing"
  overlaps "$d" "$(common_git_dir)" && die "$d is, holds or is inside the Git data of $REPO and its worktrees; refusing"
  overlaps "$d" "$own_git" && die "$d is, holds or is inside the Git data of $REPO; refusing"
  if [ -n "$(find "$d" -name '*.rpp' -type f -print -quit 2>/dev/null)" ]; then
    die "$d holds REAPER projects, which reference audio by path; move it yourself if you mean to"
  fi
  # Another checkout inside it (build folders' fetched dependencies aside)
  # would move without its branches and stashes saved or checked.
  nested=$(find "$d" -mindepth 2 \( -name _deps -o -name node_modules \) -prune -o -name .git -print -quit 2>/dev/null)
  if [ -n "$nested" ]; then
    die "$d holds another Git checkout, $(dirname "$nested"); retire that one first, or move this folder yourself"
  fi
  # Its linked worktrees keep their Git data, HEAD included, in it: they
  # would stop working, and a detached HEAD of theirs would not be saved.
  # That holds for one whose folder is absent (unplugged, or deleted).
  if [ -d "$d/.git" ]; then
    git -C "$d" worktree list --porcelain 2>/dev/null | sed -n 's/^worktree //p' >"$TMPLIST.linked" || : >"$TMPLIST.linked"
    while IFS= read -r w; do
      [ -n "$w" ] || continue
      if [ -d "$w" ]; then
        [ "$(cd "$w" && pwd -P)" = "$d" ] && continue
        die "$d has a linked worktree, $w, whose Git data is in it; remove or retire that worktree first (git -C $d worktree list)"
      fi
      die "$d has a linked worktree registered at $w, which is not there now (unplugged or deleted); its HEAD is kept only in $d. Reconnect it and retire or remove it, or, if nothing in it is needed, run git -C $d worktree prune (after git -C $d worktree unlock if it is locked)"
    done <"$TMPLIST.linked"
  fi
  if in_source_build "$d"; then
    die "$d builds in its own top folder (CMakeCache.txt there), so its source and build output cannot be told apart; look through it and move it yourself"
  fi
  header
  fetch
  name=$(basename "$d")
  keep=$(fresh_dir "$RETIRE/$name-kept")
  target=$(fresh_dir "$RETIRE/$name")
  local_refs=""
  if [ -d "$d/.git" ] && own_checkout "$d"; then
    dirty=$(git -C "$d" status --porcelain)
    [ -z "$dirty" ] || { say "$d has uncommitted changes (copied below):"; printf '%s\n' "$dirty" | head -20; }
    local_refs=$(git -C "$d" log --oneline --branches --tags --not --remotes 2>/dev/null | head -20)
    stashes=$(git -C "$d" stash list | wc -l | tr -d ' ')
    say "Its branches, tags and $stashes stash(es) are saved in $keep/refs.bundle first."
    [ -z "$local_refs" ] || { say "Commits in it that are on none of its remotes:"; printf '%s\n' "$local_refs" | sed 's/^/  /'; }
  fi
  unknown_sources "$d" >"$TMPLIST.u"
  build_evidence "$d" >"$TMPLIST.e"
  say "$(wc -l <"$TMPLIST.u" | tr -d ' ') file(s) Git has never seen and $(wc -l <"$TMPLIST.e" | tr -d ' ') log or result file(s) in build folders:"
  cat "$TMPLIST.u" "$TMPLIST.e" | sed 's/^/  /' | head -40
  if [ "$APPLY" = no ]; then
    say "Preview: nothing copied or moved. With --apply these are copied to $keep, then $d ($(human_size "$d")) moves to $target."
    return
  fi
  confirm "Copy those, then move $d ($(human_size "$d")) to $target?" || die "nothing changed"
  if [ -d "$d/.git" ] && own_checkout "$d"; then
    mkdir -p "$keep"
    bundle_refs "$d" "$keep/refs.bundle" || say "(it has no branches, tags or stashes)"
  fi
  n=$(keep_copies "$d" "$keep") || die "could not copy the files out of $d; it was not moved"
  mv "$d" "$target"
  say "Moved to $target; $n file(s) copied to $keep. If it was a worktree, run tidy next."
  say "empty-retired deletes $RETIRE once it is a week old and verify-backup finds everything in it."
}

cmd_update_main() {
  need_repo
  header
  [ -z "$(git -C "$REPO" status --porcelain)" ] || die "the checkout has uncommitted or untracked files (git status)"
  fetch
  cur=$(git -C "$REPO" rev-parse --abbrev-ref HEAD)
  if [ "$cur" != main ]; then
    n=$(git -C "$REPO" rev-list --count "origin/main..$cur")
    [ "$n" -eq 0 ] || die "$cur has $n commit(s) not on origin/main; inspect them first"
  fi
  if git -C "$REPO" rev-parse -q --verify refs/heads/main >/dev/null; then
    n=$(git -C "$REPO" rev-list --count origin/main..main)
    [ "$n" -eq 0 ] || die "local main has $n commit(s) not on origin/main; inspect them first"
  fi
  confirm "Switch $REPO from $cur to main and fast-forward it to origin/main?" || die "nothing changed"
  git -C "$REPO" switch main
  git -C "$REPO" merge --ff-only origin/main
  say "Now on main at $(git -C "$REPO" log -1 --format='%h %s')."
}

# Fetch branches and every pull request's head. The heads go under
# refs/convolver-pr/, outside refs/remotes, so `git branch -r` is unchanged.
fetch_with_prs() {
  say "Fetching origin and pull request heads (updates and prunes remote-tracking refs; no files change)..."
  git -C "$REPO" fetch --quiet --prune origin '+refs/heads/*:refs/remotes/origin/*' \
    '+refs/pull/*/head:refs/convolver-pr/*' --tags || die "could not fetch origin; nothing changed"
}

# Is commit $1 on GitHub: in a branch there or in a pull request's head?
on_github() {
  [ -n "$(git -C "$REPO" for-each-ref --count=1 --contains "$1" refs/remotes/origin refs/convolver-pr 2>/dev/null)" ]
}

remote_branch_exists() { git -C "$REPO" rev-parse -q --verify "refs/remotes/origin/$1" >/dev/null; }

# Read every merged pull request (branch, head commit, number) from GitHub
# into $MERGED. Without them nothing is removed automatically.
MERGED=""
load_merged() {
  MERGED="$TMPLIST.merged"; : >"$MERGED"
  if ! command -v python3 >/dev/null 2>&1; then
    say "python3 was not found, so merged pull requests could not be read; nothing is removed automatically."
    return
  fi
  if ! (cd "$REPO" && python3 "$SCRIPT_DIR/delete_merged_branches.py" --merged-heads) >"$MERGED" 2>"$TMPLIST.merr"; then
    : >"$MERGED"
    say "Could not read the merged pull requests ($(tail -1 "$TMPLIST.merr")); nothing is removed automatically."
  fi
}

# The merged pull request that recorded branch $1 at commit $2, or nothing.
# Only the branch's own name counts: an upstream is set as easily for new
# work started from a merged branch, and a detached HEAD names no branch.
merged_at_tip() { awk -F '\t' -v b="$1" -v s="$2" '$1 == b && $2 == s { print $3; exit }' "$MERGED"; }

# worktree_changed PATH BRANCH HEAD: print why the worktree at PATH is no
# longer the one planned (branch or detached state, HEAD, lock, files), or
# nothing when it is unchanged.
worktree_changed() {
  git -C "$REPO" worktree list --porcelain >"$TMPLIST.now" || { echo "the worktree list could not be read"; return; }
  echo >>"$TMPLIST.now"
  _state=$(awk -v p="$1" '
    $0 == "worktree " p { found = 1; head = ""; branch = ""; locked = "no"; next }
    found && /^HEAD / { head = substr($0, 6) }
    found && /^branch / { branch = substr($0, 19) }
    found && /^locked/ { locked = "yes" }
    found && $0 == "" { print head "\t" branch "\t" locked; exit }
  ' "$TMPLIST.now")
  if [ -z "$_state" ]; then echo "it is no longer a worktree here"; return; fi
  _h=$(printf '%s' "$_state" | cut -f1); _b=$(printf '%s' "$_state" | cut -f2); _l=$(printf '%s' "$_state" | cut -f3)
  [ "$_h" = "$3" ] || { echo "its HEAD moved from $(printf %.9s "$3") to $(printf %.9s "$_h")"; return; }
  [ "$_b" = "$2" ] || { echo "it switched from ${2:-a detached HEAD} to ${_b:-a detached HEAD}"; return; }
  [ "$_l" = no ] || { echo "it was locked"; return; }
  _dirty=$(git -C "$1" status --porcelain 2>/dev/null) || { echo "git status failed in it"; return; }
  [ -z "$_dirty" ] || echo "it has uncommitted or untracked files"
}

cmd_tidy() {
  no_args tidy
  need_repo
  header
  fetch_with_prs
  load_merged
  here=$(cd "$REPO" && git rev-parse --show-toplevel)
  here=$(cd "$here" && pwd -P)
  wt_remove="$TMPLIST.wt"; br_remove="$TMPLIST.br"; kept="$TMPLIST.kept"
  remove_lines="$TMPLIST.lremove"; review_lines="$TMPLIST.lreview"; keep_lines="$TMPLIST.lkeep"
  : >"$wt_remove"; : >"$br_remove"; : >"$kept"; : >"$remove_lines"; : >"$review_lines"; : >"$keep_lines"
  first=yes

  pruneable=$(git -C "$REPO" worktree prune --dry-run -v 2>&1 || true)
  # Porcelain output: "worktree PATH", "HEAD SHA", "branch REF" or "detached",
  # optionally "locked"/"prunable", then a blank line.
  # Its own file: unknown_sources, called in the loop, rewrites $TMPLIST.
  git -C "$REPO" worktree list --porcelain >"$TMPLIST.wtlist"
  echo >>"$TMPLIST.wtlist"
  path=""; head=""; branch=""; locked=no; prunable=no
  while IFS= read -r line; do
    case "$line" in
      "worktree "*) path=${line#worktree }; head=""; branch=""; locked=no; prunable=no ;;
      "HEAD "*) head=${line#HEAD } ;;
      "branch "*) branch=${line#branch refs/heads/} ;;
      locked*) locked=yes ;;
      prunable*) prunable=yes ;;
      "")
        [ -n "$path" ] || continue
        p=$path; path=""
        label="worktree $p (${branch:-detached at $(printf %.9s "$head")})"
        stays() { say "  $label: $1" >>"$keep_lines"; [ -z "$branch" ] || echo "$branch" >>"$kept"; }
        review() {
          say "  $label: $1" >>"$review_lines"
          say "      when you are sure: git worktree remove '$p'" >>"$review_lines"
          [ -z "$branch" ] || echo "$branch" >>"$kept"
        }
        # The first entry is always the main checkout, which holds the Git data.
        if [ "$first" = yes ]; then first=no; stays "the main checkout"; continue; fi
        [ "$prunable" = no ] && [ -d "$p" ] || continue
        if [ "$(cd "$p" && pwd -P)" = "$here" ]; then stays "this script is running from it"; continue; fi
        if [ "$locked" = yes ]; then stays "locked"; continue; fi
        if ! dirty=$(git -C "$p" status --porcelain 2>/dev/null); then stays "git status failed in it"; continue; fi
        if [ -n "$dirty" ]; then stays "$(printf '%s\n' "$dirty" | wc -l | tr -d ' ') uncommitted or untracked file(s)"; continue; fi
        if ! on_github "$head"; then stays "its commit is not on GitHub (only on this Mac)"; continue; fi
        if [ -n "$branch" ] && remote_branch_exists "$branch"; then
          stays "its branch is still on GitHub (open work, or run delete_merged_branches.py first)"; continue
        fi
        if in_source_build "$p"; then
          review "it builds in its own top folder, so which files are worth keeping cannot be told; look through it yourself"; continue
        fi
        if [ -z "$branch" ]; then
          review "it is detached, so no merged pull request can account for it; its commit is on GitHub"; continue
        fi
        pr=$(merged_at_tip "$branch" "$head")
        if [ -z "$pr" ]; then
          review "its commit is on GitHub, but no merged pull request recorded this branch at this commit (new work, or finished another way)"
          continue
        fi
        n_keep=$( { unknown_sources "$p"; build_evidence "$p"; } | wc -l | tr -d ' ')
        say "  $label, $(human_size "$p"): merged in #$pr at this commit; $n_keep ignored file(s) or build log(s) in it are copied first" >>"$remove_lines"
        printf '%s%s%s%s%s\n' "$p" "$SEP" "$branch" "$SEP" "$head" >>"$wt_remove"
        ;;
    esac
  done <"$TMPLIST.wtlist"

  git -C "$REPO" for-each-ref --format='%(refname:short)' refs/heads/ >"$TMPLIST.branches"
  while IFS= read -r br; do
    if [ "$br" = main ]; then say "  branch main: the default branch" >>"$keep_lines"; continue; fi
    if grep -qxF "$br" "$kept"; then
      say "  branch $br: checked out in a worktree that stays" >>"$keep_lines"; continue
    fi
    if remote_branch_exists "$br"; then say "  branch $br: still on GitHub" >>"$keep_lines"; continue; fi
    tip=$(git -C "$REPO" rev-parse "refs/heads/$br")
    if ! on_github "$tip"; then
      n=$(git -C "$REPO" rev-list --count "$tip" --not --remotes=origin --glob='refs/convolver-pr/*' 2>/dev/null || echo "?")
      say "  branch $br: $n commit(s) only on this Mac" >>"$keep_lines"
      git -C "$REPO" log --format='      %h %ad %s' --date=short "$tip" --not --remotes=origin --glob='refs/convolver-pr/*' | head -5 >>"$keep_lines"
      continue
    fi
    pr=$(merged_at_tip "$br" "$tip")
    if [ -n "$pr" ]; then
      say "  branch $br at $(printf %.9s "$tip"): merged in #$pr at this commit" >>"$remove_lines"
      printf '%s\t%s\n' "$br" "$tip" >>"$br_remove"
    else
      say "  branch $br at $(printf %.9s "$tip"): its commit is on GitHub, but no merged pull request recorded this branch at this commit" >>"$review_lines"
      say "      when you are sure: git branch -D '$br'" >>"$review_lines"
    fi
  done <"$TMPLIST.branches"

  n_wt=$(wc -l <"$wt_remove" | tr -d ' '); n_br=$(wc -l <"$br_remove" | tr -d ' ')
  n_review=$(grep -c '^  [^ ]' "$review_lines" || true); n_keep=$(grep -c '^  [^ ]' "$keep_lines" || true)
  section "Remove: $((n_wt + n_br)) · Review: $n_review · Kept: $n_keep"
  if [ -n "$pruneable" ]; then
    say "Forgotten registrations of worktree folders that are already gone (pruned with --apply):"
    printf '%s\n' "$pruneable" | sed 's/^/  /'
  fi
  if [ -s "$remove_lines" ]; then say "Remove (a merged pull request recorded each at this exact commit):"; cat "$remove_lines"; fi
  if [ -s "$review_lines" ]; then say "Review (never removed by tidy; GitHub has the commit, which makes it recoverable, not finished):"; cat "$review_lines"; fi
  if [ "$VERBOSE" = yes ]; then say "Kept:"; cat "$keep_lines"; else say "Kept: $n_keep (add --verbose to list them with the reason)"; fi
  cur=$(git -C "$REPO" rev-parse --abbrev-ref HEAD)
  if git -C "$REPO" rev-parse -q --verify refs/heads/main >/dev/null &&
    git -C "$REPO" rev-parse -q --verify refs/remotes/origin/main >/dev/null; then
    # shellcheck disable=SC2046 # two counts, split on purpose
    set -- $(git -C "$REPO" rev-list --left-right --count main...origin/main)
    say "The main checkout is on $cur. Local main: $1 commit(s) not on GitHub, $2 behind origin/main."
    [ "$2" -eq 0 ] || say "  To bring it up to date: $0 update-main --repo $REPO"
  fi

  if [ "$APPLY" = no ]; then
    say "Preview: nothing removed (the fetch above did update and prune remote-tracking refs). Add --apply to remove the items under Remove; a backup bundle is made first."
    return
  fi
  if [ "$n_wt" -eq 0 ] && [ "$n_br" -eq 0 ] && [ -z "$pruneable" ]; then say "Nothing to remove."; return; fi
  confirm "Back up all local branches, then remove the items under Remove?" || die "nothing changed"
  cmd_backup
  [ -z "$pruneable" ] || git -C "$REPO" worktree prune
  while IFS="$SEP" read -r p b h; do
    [ -n "$p" ] || continue
    # Only the worktree that was planned: same place, branch or detached
    # state and commit, still clean and unlocked.
    why=$(worktree_changed "$p" "$b" "$h")
    if [ -n "$why" ]; then say "kept $p: $why since the plan"; continue; fi
    dest=$(fresh_dir "$RETIRE/$(basename "$p")-kept")
    if ! n=$(keep_copies "$p" "$dest"); then
      say "kept $p: could not copy the ignored files out of it"; continue
    fi
    why=$(worktree_changed "$p" "$b" "$h")
    if [ -n "$why" ]; then say "kept $p: $why while its files were copied"; continue; fi
    if git -C "$REPO" worktree remove "$p"; then
      say "removed worktree $p ($n file(s) copied to $dest)"
    else
      say "kept $p: git could not remove it"
    fi
  done <"$wt_remove"
  while IFS="$TAB" read -r br tip; do
    [ -n "$br" ] || continue
    # Never a branch some worktree still has checked out (one whose removal
    # failed included), and only if still at the checked commit.
    if git -C "$REPO" worktree list --porcelain | grep -qxF "branch refs/heads/$br"; then
      say "kept $br: still checked out in a worktree"; continue
    fi
    if git -C "$REPO" update-ref -d "refs/heads/$br" "$tip"; then
      # update-ref leaves branch.NAME.remote and .merge behind; git branch -d would not.
      git -C "$REPO" config --remove-section "branch.$br" 2>/dev/null || true
      say "deleted branch $br (was $(printf %.9s "$tip"))"
    else
      say "kept $br: it moved or is in use"
    fi
  done <"$br_remove"
  say "Done. Restore a branch with: git fetch $BUNDLE 'refs/heads/NAME:refs/heads/NAME'"
}

cmd_find_clones() {
  no_args find-clones
  under=${UNDER:-$HOME}
  section "Convolver checkouts under $under"
  say "(Library, .Trash, node_modules and build folders are skipped; nothing changes.)"
  find "$under" -maxdepth 6 \( -name Library -o -name .Trash -o -name node_modules -o -name _deps -o -name CMakeFiles \) -prune -o \
    -name .git -print 2>/dev/null | while IFS= read -r g; do
    d=$(dirname "$g")
    url=$(git -C "$d" remote get-url origin 2>/dev/null || true)
    case "$url" in
      *jrp2014/convolver*) ;;
      *) # A worktree whose main checkout is gone has no remote; its .git file names the path.
        if ! { [ -f "$g" ] && grep -q 'convolver' "$g" 2>/dev/null; }; then continue; fi ;;
    esac
    if ! git -C "$d" rev-parse --git-dir >/dev/null 2>&1; then
      say "  $d  ($(human_size "$d")): Git cannot read it (a worktree whose main checkout is gone?)"; continue
    fi
    kind=checkout; [ -f "$g" ] && kind=worktree
    br=$(git -C "$d" symbolic-ref -q --short HEAD 2>/dev/null || echo "a detached HEAD")
    dirty=$(git -C "$d" status --porcelain 2>/dev/null | wc -l | tr -d ' ')
    local_only=$(git -C "$d" rev-list --count HEAD --not --remotes --glob='refs/convolver-pr/*' 2>/dev/null || echo "?")
    last=$(git -C "$d" log -1 --format=%cd --date=short 2>/dev/null)
    say "  $d"
    say "      $kind on $br, last commit $last, $(human_size "$d"); $dirty uncommitted or untracked file(s); $local_only commit(s) not on GitHub as last fetched"
  done
  section "Retired folders"
  for r in "$HOME"/convolver-retired-*; do [ -d "$r" ] && say "  $r ($(human_size "$r"))"; done
  say ""
  say "A worktree of your main checkout: tidy removes it when a merged pull request accounts for it."
  say "Another checkout: retire-dir --repo MAIN DIR keeps any unknown files, then moves it into the retired folder."
}

# verify_folder DIR: check that a retired folder still holds what its backups
# recorded. Sets VERIFY_MISSING (what is missing or changed) and VERIFY_LOCAL
# (work found nowhere else: commits neither on GitHub nor in the checkout,
# and copied files Git never saw), one item per line.
verify_folder() {
  VERIFY_MISSING=""; VERIFY_LOCAL=""; _nb=0; _nf=0
  # Every NAME-kept folder, with the -2, -3... that fresh_dir adds when two
  # share a name.
  _kept_dirs=$(for _k in "$1"/*-kept "$1"/*-kept-[0-9]*; do [ ! -d "$_k" ] || printf '%s\n' "$_k"; done)
  # A bundle whose record survives it is missing, not merely absent.
  for _t in "$1"/convolver-local-refs-*.bundle.tips "$1"/convolver-local-refs-*-refs.txt; do
    [ -f "$_t" ] || continue
    case "$_t" in *.tips) _b=${_t%.tips} ;; *) _b="${_t%-refs.txt}.bundle" ;; esac
    [ -f "$_b" ] || add_line VERIFY_MISSING "$_b is missing (its record $(basename "$_t") is there)"
  done
  printf '%s\n' "$_kept_dirs" >"$TMPLIST.keptdirs"
  while IFS= read -r _k; do
    [ -n "$_k" ] || continue
    if [ -f "$_k/refs.bundle.tips" ] && [ ! -f "$_k/refs.bundle" ]; then
      add_line VERIFY_MISSING "$_k/refs.bundle is missing (its record refs.bundle.tips is there)"
    fi
  done <"$TMPLIST.keptdirs"
  for _b in "$1"/convolver-local-refs-*.bundle "$1"/*-kept/refs.bundle "$1"/*-kept-[0-9]*/refs.bundle; do
    [ -f "$_b" ] || continue
    _nb=$((_nb + 1))
    _tips="$_b.tips"
    # Bundles tidy made before verify-backup have the same list in STAMP-refs.txt.
    if [ ! -f "$_tips" ]; then
      case "$_b" in */convolver-local-refs-*) _tips="${_b%.bundle}-refs.txt" ;; esac
    fi
    if [ ! -f "$_tips" ]; then
      add_line VERIFY_MISSING "$_b: no record of what it should hold (made before verify-backup)"; continue
    fi
    if [ -f "$_b.hash" ] && [ "$(git -C "$REPO" hash-object --no-filters -- "$_b")" != "$(cat "$_b.hash")" ]; then
      add_line VERIFY_MISSING "$_b has changed since it was made"
    fi
    if ! git -C "$REPO" bundle verify "$_b" >/dev/null 2>&1; then
      add_line VERIFY_MISSING "$_b: does not verify"; continue
    fi
    # Restore it, as a recovery would: a damaged pack passes bundle verify.
    bundle_unrestored "$_b" "$_tips" >"$TMPLIST.bmiss"
    while read -r _sha _ref; do
      [ -n "$_ref" ] || continue
      add_line VERIFY_MISSING "$_b does not restore $_ref at $(printf %.9s "$_sha")"
    done <"$TMPLIST.bmiss"
    git -C "$REPO" stash list --format=%H >"$TMPLIST.stashes" 2>/dev/null || : >"$TMPLIST.stashes"
    while read -r _sha _ref; do
      [ -n "$_ref" ] || continue
      grep -qxF "$_sha $_ref" "$TMPLIST.bmiss" && continue
      _c=$(git -C "$REPO" rev-parse -q --verify "$_sha^{commit}" 2>/dev/null || true)
      if [ -n "$_c" ]; then
        on_github "$_c" && continue
        [ -z "$(git -C "$REPO" for-each-ref --count=1 --contains "$_c" refs/heads refs/tags 2>/dev/null)" ] || continue
        grep -qxF "$_c" "$TMPLIST.stashes" && continue
      fi
      add_line VERIFY_LOCAL "${_ref#refs/} at $(printf %.9s "$_sha") (in ${_b#"$1"/})"
    done <"$_tips"
  done
  while IFS= read -r _k; do
    [ -n "$_k" ] || continue
    if [ ! -f "$_k/manifest.txt" ]; then
      if [ -n "$(find "$_k/files" -type f -print -quit 2>/dev/null)" ]; then
        add_line VERIFY_MISSING "$_k: copied files with no manifest (made before verify-backup)"
      fi
      continue
    fi
    while IFS="$TAB" read -r _h _kind _rel; do
      [ -n "$_rel" ] || continue
      _nf=$((_nf + 1))
      _f="$_k/files/$_rel"
      if [ ! -f "$_f" ]; then add_line VERIFY_MISSING "$_f is missing"; continue; fi
      [ "$(git -C "$REPO" hash-object --no-filters -- "$_f")" = "$_h" ] || { add_line VERIFY_MISSING "$_f has changed"; continue; }
      [ "$_kind" != source ] || add_line VERIFY_LOCAL "$(basename "$_k")/files/$_rel (a file Git never saw)"
    done <"$_k/manifest.txt"
  done <"$TMPLIST.keptdirs"
  VERIFY_SUMMARY="$_nb bundle(s), $_nf preserved file(s)"
}

# report_folder DIR: print verify_folder's findings for DIR.
report_folder() {
  verify_folder "$1"
  if [ -n "$VERIFY_MISSING" ]; then
    VERIFY_FAILED=yes
    say "  MISSING $1: these are not as the backup recorded them:"
    printf '%s\n' "$VERIFY_MISSING" | sed 's/^/      /'
  else
    say "  OK      $1: $VERIFY_SUMMARY, all as recorded"
  fi
  if [ -n "$VERIFY_LOCAL" ]; then
    say "          It holds the only known copy of this local work (recoverable from this folder is not the same as safe to delete):"
    printf '%s\n' "$VERIFY_LOCAL" | head -10 | sed 's/^/      /'
    _more=$(printf '%s\n' "$VERIFY_LOCAL" | wc -l | tr -d ' ')
    [ "$_more" -le 10 ] || say "      and $((_more - 10)) more"
  fi
}

# The folders named on the command line, or else every retired folder.
retired_folders() {
  if [ -n "$ARGS" ]; then
    printf '%s\n' "$ARGS" >"$TMPLIST.args"
    while IFS= read -r r; do
      [ -d "$r" ] || die "$r is not a folder"
      (cd "$r" && pwd -P)
    done <"$TMPLIST.args"
  else
    for r in "$HOME"/convolver-retired-*; do [ ! -d "$r" ] || printf '%s\n' "$r"; done
  fi
}

cmd_verify_backup() {
  need_repo
  header
  fetch_with_prs
  retired_folders >"$TMPLIST.retired"
  [ -s "$TMPLIST.retired" ] || { say "No retired folders."; return; }
  section "Retired folders"
  VERIFY_FAILED=no
  while IFS= read -r r; do report_folder "$r"; done <"$TMPLIST.retired"
  # Every folder is checked, but a missing or damaged backup fails the command.
  [ "$VERIFY_FAILED" = no ] || { echo "Some backups are missing or damaged (MISSING above)." >&2; exit 1; }
}

# Print the retired folders older than a week, one per line.
old_retired() {
  cutoff=$(date -v-7d +%Y-%m-%d 2>/dev/null || date -d '7 days ago' +%Y-%m-%d)
  for r in "$HOME"/convolver-retired-*; do
    [ -d "$r" ] || continue
    d=${r##*/convolver-retired-}
    case "$d" in [0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]) ;; *) continue ;; esac
    [ "$d" \> "$cutoff" ] || printf '%s\n' "$r"
  done
}

cmd_empty_retired() {
  no_args empty-retired
  need_repo
  header
  old_retired >"$TMPLIST.old"
  for r in "$HOME"/convolver-retired-*; do
    [ -d "$r" ] || continue
    grep -qxF "$r" "$TMPLIST.old" ||
      say "  KEEP    $r ($(human_size "$r")): less than a week old, or not named convolver-retired-YYYY-MM-DD"
  done
  [ -s "$TMPLIST.old" ] || { say "No retired folder is more than a week old."; return; }
  # verify-backup first: a folder is deleted only if everything its backups
  # recorded is there, and, unless --delete-local-work, nothing in it is the
  # only known copy of local work.
  fetch_with_prs
  : >"$TMPLIST.delete"
  while IFS= read -r r; do
    report_folder "$r"
    if [ -n "$VERIFY_MISSING" ]; then
      say "          KEEP: something its backup recorded is missing or changed (above)"
    elif [ -n "$VERIFY_LOCAL" ] && [ "$DELETE_LOCAL_WORK" = no ]; then
      say "          KEEP: copy what you need elsewhere, then add --delete-local-work"
    else
      say "          DELETE $r ($(human_size "$r"))"
      printf '%s\n' "$r" >>"$TMPLIST.delete"
    fi
  done <"$TMPLIST.old"
  [ -s "$TMPLIST.delete" ] || { say "Nothing to delete."; return; }
  if [ "$APPLY" = no ]; then say "Preview: nothing deleted. Add --apply to delete the folders marked DELETE."; return; fi
  confirm "Delete the folders marked DELETE permanently?" || die "nothing deleted"
  while IFS= read -r r; do rm -rf "$r" && say "deleted $r"; done <"$TMPLIST.delete"
}

[ $# -gt 0 ] || { awk 'NR > 2 && /^#/ { sub(/^# ?/, ""); print; next } NR > 2 { exit }' "$0"; exit 1; }
command=$1; shift
parse "$@"
case "$command" in
  tidy) cmd_tidy ;;
  find-clones) cmd_find_clones ;;
  verify-backup) cmd_verify_backup ;;
  empty-retired) cmd_empty_retired ;;
  retire-dir) cmd_retire_dir ;;
  update-main) no_args update-main; cmd_update_main ;;
  backup) no_args backup; cmd_backup ;;
  *) die "unknown command $command" ;;
esac
