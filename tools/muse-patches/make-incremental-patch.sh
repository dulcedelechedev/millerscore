#!/usr/bin/env bash
# Writes the next muse patch as an *incremental* diff: upstream muse with every
# existing tools/muse-patches/*.patch applied, compared with the current muse
# working tree, restricted to the given paths (relative to muse/). A plain
# `git -C muse diff` would repeat what earlier patches already change in the
# same files. New files are supported.
#
# usage (Git Bash, from anywhere):
#   tools/muse-patches/make-incremental-patch.sh tools/muse-patches/NNNN-topic.patch \
#       framework/audio/engine/internal/mixer.cpp framework/audio/tests/new_tests.cpp
#
# Check afterwards that the whole series reproduces the working tree: run it once
# more with every modified/untracked muse path and an output outside the folder;
# the result must be empty.
set -euo pipefail

repo=$(cd "$(dirname "$0")/../.." && pwd)
out=$1; shift
case "$out" in /*|?:*) ;; *) out="$PWD/$out" ;; esac

base=$(mktemp -d)
git -C "$repo/muse" worktree add -q --detach "$base/wt" HEAD
trap 'git -C "$repo/muse" worktree remove --force "$base/wt"; rm -rf "$base"' EXIT

for p in "$repo"/tools/muse-patches/*.patch; do
    [ -e "$out" ] && [ "$p" -ef "$out" ] && continue
    git -C "$base/wt" apply "$p"
done

tmp="$out.tmp"
: > "$tmp"
for f in "$@"; do
    if [ ! -e "$base/wt/$f" ]; then
        (cd "$base" && git diff --no-index --no-color -- /dev/null "$repo/muse/$f" || true) \
            | sed -e "s#^diff --git .*#diff --git a/$f b/$f#" \
                  -e "s#^+++ b/.*#+++ b/$f#" \
                  -e '/^index /d' >> "$tmp"
    elif ! diff -q "$base/wt/$f" "$repo/muse/$f" > /dev/null 2>&1; then
        (cd "$base" && git diff --no-index --no-color -- "wt/$f" "$repo/muse/$f" || true) \
            | sed -e "s#^diff --git .*#diff --git a/$f b/$f#" \
                  -e "s#^--- a/wt/$f#--- a/$f#" \
                  -e "s#^+++ b/.*#+++ b/$f#" \
                  -e '/^index /d' >> "$tmp"
    fi
done

# Patches are stored with LF endings (see .gitattributes).
tr -d '\r' < "$tmp" > "$out"
rm -f "$tmp"
echo "wrote $out ($(grep -c '^diff --git' "$out" || true) files)"
