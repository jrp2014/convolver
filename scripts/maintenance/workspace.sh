#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Tidy a local Convolver workspace: finished worktrees, local branches, old
# clones and retired folders (CI-56). Bash 3.2, as shipped with macOS, is fine.
#
#   tidy            remove finished worktrees and local branches, after a backup
#   find-clones     list every Convolver checkout under a folder (read-only)
#   empty-retired   delete ~/convolver-retired-* folders more than a week old
#   retire-dir      keep any unknown source, logs and results, then move a clone away
#   update-main     switch the checkout to an up-to-date main
#   backup          bundle every local branch, tag and stash into the retired folder
#
# tidy, empty-retired and retire-dir change nothing unless given --apply; each
# then asks you to type yes. Run from the root of a checkout:
#   scripts/maintenance/workspace.sh find-clones [--under ~/source]
#   scripts/maintenance/workspace.sh tidy [--apply]
#   scripts/maintenance/workspace.sh empty-retired [--apply]
#   scripts/maintenance/workspace.sh retire-dir ~/source/old-clone [--apply]
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
REPO=$(cd "$(dirname "$0")/../.." && pwd)
UNDER=""
ARGS=""
APPLY=no
# shellcheck disable=SC2034 # used through eval in add_line
NL='
'

TMPLIST=$(mktemp "${TMPDIR:-/tmp}/convolver-workspace.XXXXXX")
trap 'rm -f "$TMPLIST" "$TMPLIST".*' EXIT

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
      --repo) REPO=$2; shift 2 ;;
      --under) UNDER=$2; shift 2 ;;
      --apply) APPLY=yes; shift ;;
      -*) die "unknown option $1" ;;
      *) add_line ARGS "$1"; shift ;;
    esac
  done
}

need_repo() {
  [ -n "$REPO" ] || die "give the main checkout with --repo PATH"
  git -C "$REPO" rev-parse --git-dir >/dev/null 2>&1 || die "$REPO is not a Git checkout"
  REPO=$(cd "$REPO" && pwd)
}

fetch() {
  say "Fetching origin (updates remote-tracking refs only; no files change)..."
  git -C "$REPO" fetch --prune origin '+refs/heads/*:refs/remotes/origin/*' --tags >/dev/null
}

human_size() { du -sh "$1" 2>/dev/null | cut -f1; }

# Directories that hold build output: any folder with a CMakeCache.txt.
build_roots() { find "$1" -name CMakeCache.txt -not -path '*/_deps/*' -print 2>/dev/null | sed 's#/CMakeCache.txt$##'; }

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
      [ "$_skip" = no ] && printf '%s\n' "$f"
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
# into DEST/files, with their lists beside them, and print how many. Fails
# (and the caller removes nothing) if any step fails.
keep_copies() {
  unknown_sources "$1" >"$TMPLIST.unknown" || return 1
  build_evidence "$1" >"$TMPLIST.evidence" || return 1
  cat "$TMPLIST.unknown" "$TMPLIST.evidence" >"$TMPLIST.all" || return 1
  mkdir -p "$2" || return 1
  cp "$TMPLIST.unknown" "$2/unknown-sources.txt" || return 1
  cp "$TMPLIST.evidence" "$2/build-evidence.txt" || return 1
  while IFS= read -r f; do
    [ -n "$f" ] || continue
    rel=${f#"$1"/}
    mkdir -p "$2/files/$(dirname "$rel")" || return 1
    cp -p "$f" "$2/files/$rel" || return 1
  done <"$TMPLIST.all"
  wc -l <"$TMPLIST.all" | tr -d ' '
}

# fresh_dir PATH: PATH, or PATH-2, PATH-3... if it exists, so that nothing
# saved earlier is overwritten.
fresh_dir() {
  _d=$1; _n=2
  while [ -e "$_d" ]; do _d="$1-$_n"; _n=$((_n + 1)); done
  printf '%s\n' "$_d"
}

# bundle_refs CHECKOUT FILE: save the checkout's branches, tags and stashes
# in FILE. Returns 1 when it has none.
bundle_refs() {
  [ -n "$(git -C "$1" for-each-ref --count=1 refs/heads refs/tags)$(git -C "$1" stash list)" ] || return 1
  # A bundle takes refs only, so give each stash a temporary ref.
  _i=0
  for _s in $(git -C "$1" stash list --format=%H); do git -C "$1" update-ref "refs/retired-stash/$_i" "$_s"; _i=$((_i + 1)); done
  git -C "$1" bundle create "$2" --branches --tags --glob='refs/retired-stash/*' >/dev/null 2>&1 ||
    git -C "$1" bundle create "$2" --branches --tags >/dev/null
  git -C "$1" for-each-ref --format='%(refname)' refs/retired-stash/ | while IFS= read -r r; do git -C "$1" update-ref -d "$r"; done
  git -C "$1" bundle verify "$2" >/dev/null 2>&1 || die "the bundle $2 does not verify; nothing removed"
  BUNDLED_STASHES=$_i
}

cmd_backup() {
  need_repo
  mkdir -p "$RETIRE"
  BUNDLE="$RETIRE/convolver-local-refs-$(date +%H%M%S).bundle"
  [ ! -e "$BUNDLE" ] || BUNDLE="${BUNDLE%.bundle}-$$.bundle"  # never overwrite an earlier one
  bundle_refs "$REPO" "$BUNDLE" || die "no branches, tags or stashes to save"
  stamp=$(basename "$BUNDLE" .bundle)
  git -C "$REPO" worktree list --porcelain >"$RETIRE/$stamp-worktrees.txt"
  git -C "$REPO" for-each-ref --format='%(objectname) %(refname)' refs/heads refs/tags >"$RETIRE/$stamp-refs.txt"
  say "Saved every local branch and tag$( [ "$BUNDLED_STASHES" -gt 0 ] && echo " and $BUNDLED_STASHES stash(es)") in $BUNDLE"
  say "Restore one later with: git fetch $BUNDLE 'refs/heads/NAME:refs/heads/NAME'"
}

# The folder that holds REPO's Git data (the main checkout's .git).
common_git_dir() { (cd "$REPO" && cd "$(git rev-parse --git-common-dir)" && pwd -P); }

cmd_retire_dir() {
  need_repo
  d=$(printf '%s\n' "$ARGS" | head -1)
  [ -n "$d" ] && [ -d "$d" ] || die "name one existing folder"
  d=$(cd "$d" && pwd -P)
  case "$(cd "$REPO" && pwd -P)/" in "$d"/*) die "$d holds the checkout this script works on; refusing" ;; esac
  case "$(common_git_dir)/" in "$d"/*) die "$d holds the Git data of $REPO and its worktrees; refusing" ;; esac
  if [ -n "$(find "$d" -name '*.rpp' -type f -print -quit 2>/dev/null)" ]; then
    die "$d holds REAPER projects, which reference audio by path; move it yourself if you mean to"
  fi
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
    say "Dry run: nothing copied or moved. With --apply these are copied to $keep, then $d ($(human_size "$d")) moves to $target."
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
  say "empty-retired deletes $RETIRE once it is a week old."
}

cmd_update_main() {
  need_repo
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
  say "Fetching origin and pull request heads (no files change)..."
  git -C "$REPO" fetch --quiet --prune origin '+refs/heads/*:refs/remotes/origin/*' \
    '+refs/pull/*/head:refs/convolver-pr/*' --tags
}

# Is commit $1 on GitHub: in a branch there or in a pull request's head?
on_github() {
  [ -n "$(git -C "$REPO" for-each-ref --count=1 --contains "$1" refs/remotes/origin refs/convolver-pr 2>/dev/null)" ]
}

remote_branch_exists() { git -C "$REPO" rev-parse -q --verify "refs/remotes/origin/$1" >/dev/null; }

cmd_tidy() {
  need_repo
  fetch_with_prs
  here=$(cd "$REPO" && git rev-parse --show-toplevel)
  here=$(cd "$here" && pwd -P)
  wt_remove="$TMPLIST.wt"; br_remove="$TMPLIST.br"; kept="$TMPLIST.kept"
  : >"$wt_remove"; : >"$br_remove"; : >"$kept"
  first=yes

  section "Worktrees"
  pruneable=$(git -C "$REPO" worktree prune --dry-run -v 2>&1 || true)
  [ -z "$pruneable" ] || { say "Registrations whose folder is already gone (forgotten):"; printf '%s\n' "$pruneable" | sed 's/^/  /'; }
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
        label="$p (${branch:-detached at $(printf %.9s "$head")})"
        keep() { say "  KEEP   $label: $1"; [ -z "$branch" ] || echo "$branch" >>"$kept"; }
        # The first entry is always the main checkout, which holds the Git data.
        if [ "$first" = yes ]; then first=no; [ -z "$branch" ] || echo "$branch" >>"$kept"; continue; fi
        [ "$prunable" = no ] && [ -d "$p" ] || continue
        if [ "$(cd "$p" && pwd -P)" = "$here" ]; then keep "this script is running from it"; continue; fi
        if [ "$locked" = yes ]; then keep "locked"; continue; fi
        if ! dirty=$(git -C "$p" status --porcelain 2>/dev/null); then keep "git status failed in it"; continue; fi
        if [ -n "$dirty" ]; then keep "$(printf '%s\n' "$dirty" | wc -l | tr -d ' ') uncommitted or untracked file(s)"; continue; fi
        if ! on_github "$head"; then keep "its commit is not on GitHub (only on this Mac)"; continue; fi
        if [ -n "$branch" ] && remote_branch_exists "$branch"; then
          keep "its branch is still on GitHub (open work, or run delete_merged_branches.py first)"; continue
        fi
        n_keep=$( { unknown_sources "$p"; build_evidence "$p"; } | wc -l | tr -d ' ')
        say "  REMOVE $label, $(human_size "$p"); $n_keep ignored file(s) or build log(s) in it are copied first"
        printf '%s\t%s\n' "$p" "$branch" >>"$wt_remove"
        ;;
    esac
  done <"$TMPLIST.wtlist"
  [ "$(git -C "$REPO" worktree list | wc -l)" -gt 1 ] || say "  No worktrees besides the main checkout."

  section "Local branches"
  git -C "$REPO" for-each-ref --format='%(refname:short)' refs/heads/ | while IFS= read -r br; do
    [ "$br" != main ] || continue
    if grep -qxF "$br" "$kept"; then
      say "  KEEP   $br: checked out in a worktree that stays"; continue
    fi
    if remote_branch_exists "$br"; then say "  KEEP   $br: still on GitHub"; continue; fi
    tip=$(git -C "$REPO" rev-parse "refs/heads/$br")
    if on_github "$tip"; then
      say "  REMOVE $br at $(printf %.9s "$tip"): on GitHub as a pull request's head or in a branch"
      printf '%s\t%s\n' "$br" "$tip" >>"$br_remove"
    else
      n=$(git -C "$REPO" rev-list --count "$tip" --not --remotes=origin --glob='refs/convolver-pr/*' 2>/dev/null || echo "?")
      say "  KEEP   $br: $n commit(s) only on this Mac:"
      git -C "$REPO" log --format='           %h %ad %s' --date=short "$tip" --not --remotes=origin --glob='refs/convolver-pr/*' | head -5
    fi
  done

  section "Main checkout"
  cur=$(git -C "$REPO" rev-parse --abbrev-ref HEAD)
  if git -C "$REPO" rev-parse -q --verify refs/heads/main >/dev/null; then
    # shellcheck disable=SC2046 # two counts, split on purpose
    set -- $(git -C "$REPO" rev-list --left-right --count main...origin/main)
    say "  On $cur. Local main: $1 commit(s) not on GitHub, $2 behind origin/main."
    [ "$2" -eq 0 ] || say "  To bring it up to date: $0 update-main --repo $REPO"
  fi

  n_wt=$(wc -l <"$wt_remove" | tr -d ' '); n_br=$(wc -l <"$br_remove" | tr -d ' ')
  section "Plan: remove $n_wt worktree(s) and $n_br local branch(es)"
  if [ "$APPLY" = no ]; then
    say "Dry run: nothing changed. Add --apply to do it (a backup bundle is made first)."
    rm -f "$wt_remove" "$br_remove" "$kept"; return
  fi
  if [ "$n_wt" -eq 0 ] && [ "$n_br" -eq 0 ] && [ -z "$pruneable" ]; then say "Nothing to do."; rm -f "$wt_remove" "$br_remove" "$kept"; return; fi
  confirm "Back up all local branches, then remove these?" || die "nothing changed"
  cmd_backup
  [ -z "$pruneable" ] || git -C "$REPO" worktree prune
  while IFS="$(printf '\t')" read -r p _b; do
    [ -n "$p" ] || continue
    dest=$(fresh_dir "$RETIRE/$(basename "$p")-kept")
    if ! n=$(keep_copies "$p" "$dest"); then
      say "could not copy the ignored files out of $p (left as it is)"; continue
    fi
    if git -C "$REPO" worktree remove "$p"; then
      say "removed worktree $p ($n file(s) copied to $dest)"
    else
      say "could not remove $p (left as it is)"
    fi
  done <"$wt_remove"
  while IFS="$(printf '\t')" read -r br tip; do
    [ -n "$br" ] || continue
    # Never a branch some worktree still has checked out (one whose removal
    # failed included), and only if still at the checked commit.
    if git -C "$REPO" worktree list --porcelain | grep -qxF "branch refs/heads/$br"; then
      say "kept $br: still checked out in a worktree"; continue
    fi
    if git -C "$REPO" update-ref -d "refs/heads/$br" "$tip"; then
      say "deleted branch $br (was $(printf %.9s "$tip"))"
    else
      say "kept $br: it moved or is in use"
    fi
  done <"$br_remove"
  rm -f "$wt_remove" "$br_remove" "$kept"
  say "Done. Restore a branch with: git fetch $BUNDLE 'refs/heads/NAME:refs/heads/NAME'"
}

cmd_find_clones() {
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
  say "A worktree of your main checkout: tidy removes it when it is finished."
  say "Another checkout: retire-dir --repo MAIN DIR keeps any unknown files, then moves it into the retired folder."
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
  for r in "$HOME"/convolver-retired-*; do
    [ -d "$r" ] || continue
    if old_retired | grep -qxF "$r"; then
      say "  DELETE $r ($(human_size "$r"))"
      find "$r" -maxdepth 3 -name '*.bundle' -print 2>/dev/null | while IFS= read -r b; do
        say "         it holds $b (branches, tags and stashes): copy it elsewhere first to keep them"
      done
    else
      say "  KEEP   $r ($(human_size "$r")): less than a week old, or not named convolver-retired-YYYY-MM-DD"
    fi
  done
  [ -n "$(old_retired)" ] || { say "No retired folder is more than a week old."; return; }
  if [ "$APPLY" = no ]; then say "Dry run: nothing deleted. Add --apply to delete the folders marked DELETE."; return; fi
  confirm "Delete these folders permanently?" || die "nothing deleted"
  old_retired | while IFS= read -r r; do rm -rf "$r" && say "deleted $r"; done
}

[ $# -gt 0 ] || { sed -n '3,27p' "$0"; exit 1; }
command=$1; shift
parse "$@"
case "$command" in
  tidy) cmd_tidy ;;
  find-clones) cmd_find_clones ;;
  empty-retired) cmd_empty_retired ;;
  retire-dir) cmd_retire_dir ;;
  update-main) cmd_update_main ;;
  backup) cmd_backup ;;
  *) die "unknown command $command" ;;
esac
