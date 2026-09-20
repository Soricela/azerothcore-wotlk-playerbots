#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(
    cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &&
    pwd
)"

ACORE_ROOT="$(
    cd -- "$SCRIPT_DIR/../.." &&
    pwd
)"

LOCK_FILE="${1:-$ACORE_ROOT/modules-lock.tsv}"
MODULES_DIR="$ACORE_ROOT/modules"

if [ ! -f "$LOCK_FILE" ]; then
    echo "ERROR: module lock not found: $LOCK_FILE" >&2
    exit 1
fi

mkdir -p "$MODULES_DIR"

installed=0

while IFS=$'\t' read -r module_name branch_name commit_id repository_url
do
    repository_url="${repository_url%$'\r'}"

    [ -z "$module_name" ] && continue
    [ "$module_name" = "MODULE" ] && continue

    if [[ ! "$module_name" =~ ^[A-Za-z0-9._-]+$ ]]; then
        echo "ERROR: invalid module name: $module_name" >&2
        exit 1
    fi

    if [[ ! "$commit_id" =~ ^[0-9a-f]{40}$ ]]; then
        echo "ERROR: invalid commit for $module_name: $commit_id" >&2
        exit 1
    fi

    destination="$MODULES_DIR/$module_name"

    echo
    echo "=== $module_name ==="
    echo "branch=$branch_name"
    echo "commit=$commit_id"
    echo "origin=$repository_url"

    if [ -e "$destination" ]; then
        if ! git -C "$destination" rev-parse \
             --is-inside-work-tree >/dev/null 2>&1
        then
            echo "ERROR: destination exists but is not a Git repository: $destination" >&2
            exit 1
        fi

        current_commit="$(
            git -C "$destination" rev-parse HEAD
        )"

        if [ "$current_commit" != "$commit_id" ]; then
            echo "ERROR: existing module has unexpected commit" >&2
            echo "expected=$commit_id" >&2
            echo "actual=$current_commit" >&2
            exit 1
        fi

        echo "OK: existing module already matches lock"
    else
        git clone \
          --filter=blob:none \
          --no-checkout \
          --no-tags \
          --single-branch \
          --branch "$branch_name" \
          "$repository_url" \
          "$destination"

        git -C "$destination" checkout \
          --detach \
          "$commit_id"

        current_commit="$(
            git -C "$destination" rev-parse HEAD
        )"

        if [ "$current_commit" != "$commit_id" ]; then
            echo "ERROR: checked out commit does not match lock" >&2
            exit 1
        fi

        echo "OK: cloned and verified"
    fi

    installed=$((installed + 1))
done < "$LOCK_FILE"

if [ "$installed" -eq 0 ]; then
    echo "ERROR: no modules were processed" >&2
    exit 1
fi

echo
echo "OK: $installed locked modules verified"
