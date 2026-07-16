#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BACKUP_DIR="${DOOM_SOURCE_BACKUP_DIR:-$SCRIPT_DIR/source-backups}"
TIMESTAMP="$(date -u +%Y%m%dT%H%M%S.%NZ)"
GIT_REVISION="$(git -C "$SCRIPT_DIR" rev-parse --short=12 HEAD 2>/dev/null || printf 'no-git')"
ARCHIVE="$BACKUP_DIR/doom550d-source-$TIMESTAMP-$GIT_REVISION.tar.gz"
TEMP_ARCHIVE=""

cleanup() {
    local status=$?

    trap - EXIT INT TERM

    if [[ -n "$TEMP_ARCHIVE" && -e "$TEMP_ARCHIVE" ]]; then
        rm -f -- "$TEMP_ARCHIVE"
    fi

    exit "$status"
}

trap cleanup EXIT INT TERM

mkdir -p -- "$BACKUP_DIR"
TEMP_ARCHIVE="$(mktemp "$BACKUP_DIR/.doom550d-source.XXXXXXXXXX.tar.gz")"

tar \
    --create \
    --gzip \
    --file "$TEMP_ARCHIVE" \
    --directory "$SCRIPT_DIR" \
    --exclude='./build' \
    --exclude='./source-backups' \
    --exclude='./known-good' \
    --exclude='./wad' \
    --exclude='./*.log' \
    .

gzip --test "$TEMP_ARCHIVE"
mv -- "$TEMP_ARCHIVE" "$ARCHIVE"
TEMP_ARCHIVE=""
sha256sum "$ARCHIVE" > "$ARCHIVE.sha256"

printf 'Bronback-up: %s\n' "$ARCHIVE"
printf 'SHA-256:    '
sha256sum "$ARCHIVE" | awk '{print $1}'
