#!/usr/bin/env bash
set -Eeuo pipefail

# Bouwt de actuele doom550d-bron, zoekt de Canon-kaart op label EOS_DIGITAL,
# controleert de Magic Lantern-build, kopieert zonder bevestiging,
# verifieert met SHA-256 en unmount de kaart daarna altijd veilig.

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SOURCE="$SCRIPT_DIR/doom550d.c"
MODULE="$SCRIPT_DIR/build/doom.mo"
DEP="$SCRIPT_DIR/build/doom.dep"

TARGET_DEVICE=""
MOUNTPOINT=""

die() {
    echo "FOUT: $*" >&2
    exit 1
}

unmount_device() {
    local dev="$1"

    sync

    if findmnt -rn -S "$dev" >/dev/null 2>&1; then
        echo "Unmount: $dev"
        udisksctl unmount -b "$dev" >/dev/null
    fi
}

cleanup() {
    local status=$?
    trap - EXIT INT TERM

    if [[ -n "$TARGET_DEVICE" ]]; then
        unmount_device "$TARGET_DEVICE" || true
    fi

    exit "$status"
}

trap cleanup EXIT INT TERM

[[ -f "$SOURCE" ]] || die "Bronbestand ontbreekt: $SOURCE"

# Voorkom dat per ongeluk opnieuw een automatisch startende versie wordt gepusht.
python3 - "$SOURCE" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
src = path.read_text()

required = [
    "MENU_SELECT_FUNC(doom550d_start)",
    'menu_add("Games"',
]

for text in required:
    if text not in src:
        print(f"FOUT: vereiste menu-code ontbreekt: {text}", file=sys.stderr)
        sys.exit(1)

start = src.find("static unsigned int doom550d_init")
if start < 0:
    print("FOUT: doom550d_init() niet gevonden.", file=sys.stderr)
    sys.exit(1)

brace = src.find("{", start)
if brace < 0:
    print("FOUT: begin van doom550d_init() niet gevonden.", file=sys.stderr)
    sys.exit(1)

depth = 0
end = None

for pos in range(brace, len(src)):
    char = src[pos]

    if char == "{":
        depth += 1
    elif char == "}":
        depth -= 1
        if depth == 0:
            end = pos + 1
            break

if end is None:
    print("FOUT: einde van doom550d_init() niet gevonden.", file=sys.stderr)
    sys.exit(1)

init_body = src[brace:end]

if "task_create" in init_body:
    print(
        "FOUT: task_create() staat in doom550d_init(); "
        "deze versie zou automatisch starten.",
        file=sys.stderr,
    )
    sys.exit(1)

print("Broncontrole: menu-start zonder autostart")
PY

echo "Bouwen..."
make -C "$SCRIPT_DIR" clean
make -C "$SCRIPT_DIR" -j"$(nproc)"

[[ -s "$MODULE" ]] || die "Gebouwde module ontbreekt of is leeg: $MODULE"
[[ -f "$DEP" ]] || die "Dependencybestand ontbreekt: $DEP"

# Zoek niet op /dev/sdb1, /dev/sdc1, enzovoort, maar uitsluitend op kaartlabel.
mapfile -t DEVICES < <(
    lsblk -prno NAME,LABEL,TYPE |
        awk '$2 == "EOS_DIGITAL" && $3 == "part" { print $1 }'
)

((${#DEVICES[@]} > 0)) || die "Geen partitie met label EOS_DIGITAL gevonden."

for dev in "${DEVICES[@]}"; do
    mountpoint="$(findmnt -rn -S "$dev" -o TARGET | head -n1 || true)"
    mounted_by_script=0

    if [[ -z "$mountpoint" ]]; then
        echo "Mount: $dev"

        udisksctl mount -b "$dev" >/dev/null ||
            die "Kon $dev niet mounten."

        mounted_by_script=1
        mountpoint="$(findmnt -rn -S "$dev" -o TARGET | head -n1 || true)"
    fi

    [[ -n "$mountpoint" ]] || die "Mountpunt van $dev niet gevonden."

    if [[ -f "$mountpoint/ML/MODULES/550D_109.sym" ]]; then
        TARGET_DEVICE="$dev"
        MOUNTPOINT="$mountpoint"
        break
    fi

    echo "Overslaan: $dev heeft wel label EOS_DIGITAL, maar geen 550D_109.sym."

    if ((mounted_by_script)); then
        unmount_device "$dev"
    fi
done

[[ -n "$TARGET_DEVICE" ]] ||
    die "Geen Canon 550D Magic Lantern-kaart met 550D_109.sym gevonden."

SYM="$MOUNTPOINT/ML/MODULES/550D_109.sym"
DEST="$MOUNTPOINT/ML/MODULES/doom.mo"

echo "Kaart:      $TARGET_DEVICE"
echo "Mountpunt: $MOUNTPOINT"

missing="$(
    comm -23 \
        <(sort -u "$DEP") \
        <(awk '{print $2}' "$SYM" | sort -u)
)"

if [[ -n "$missing" ]]; then
    echo "Ontbrekende symbolen:" >&2
    echo "$missing" >&2
    die "Module is niet compatibel met deze Magic Lantern-build."
fi

echo "Imports: OK"

# -f voorkomt de overschrijfvraag.
cp -f -- "$MODULE" "$DEST"
sync

source_hash="$(sha256sum "$MODULE" | awk '{print $1}')"
dest_hash="$(sha256sum "$DEST" | awk '{print $1}')"

[[ "$source_hash" == "$dest_hash" ]] ||
    die "Hashcontrole mislukt: bestand op de kaart wijkt af."

echo "Kopie:   OK"
echo "SHA-256: $dest_hash"
echo "Doel:    $DEST"
echo "Klaar. De kaart wordt nu veilig ge-unmount."
