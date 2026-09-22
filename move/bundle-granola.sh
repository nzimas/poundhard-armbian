#!/usr/bin/env bash
# Package Granola for the PoundHard install: move/bundle/granola.tar.gz.
#
#   move/bundle-granola.sh [path/to/granola-move]     default: ../granola-move
#
# Granola is its own repo (github.com/nzimas/granola-move). install.sh does not reach
# into a sibling checkout at install time: it ships this tarball, so the PoundHard repo
# alone yields the whole machine. Rebuild it whenever granola-move changes.
#
# Layout (relative to the tarball root):
#   app/     -> /data/UserData/granola          code, engine, Airwindows, launch scripts
#   module/  -> .../modules/overtake/granola     module.json + ui.js (run under phhost)
# Nothing under samples/, projects/ or state/: those are the user's, and the installer
# never touches them.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$(cd "${1:-$HERE/../../granola-move}" && pwd)"
OUT="$HERE/bundle/granola.tar.gz"

die() { echo "FAILED: $*" >&2; exit 1; }
[ -f "$SRC/VERSION" ] || die "$SRC is not a granola-move checkout"
AW="$SRC/vendor/airwindows"
for f in "$AW/GranolaAirwindows.so" "$AW/airwindows-manifest.json" "$AW/fx-cost.json" \
         "$SRC/controller/vendor/yt_dlp/__init__.py" "$SRC/controller/vendor/pythonosc/__init__.py"; do
    [ -f "$f" ] || die "missing $f"
done
NSD=$(ls "$AW/synthdefs" | grep -c '\.scsyndef$' || true)
[ "$NSD" = 154 ] || die "expected 154 Airwindows synthdefs, found $NSD"

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
A="$STAGE/app"; M="$STAGE/module"
mkdir -p "$A/controller/vendor" "$A/sc" "$A/plugins" "$A/synthdefs" "$M"

cp -R "$SRC/controller/granola" "$A/controller/"
cp -R "$SRC/controller/vendor/pythonosc" "$SRC/controller/vendor/yt_dlp" "$A/controller/vendor/"
cp "$SRC/supercollider/granola-engine.scd" "$SRC/supercollider/granola-synthdefs.scd" \
   "$SRC/move/sc/gr-boot.scd" "$A/sc/"
cp "$AW/GranolaAirwindows.so" "$A/plugins/"
cp "$AW/airwindows-manifest.json" "$AW/fx-cost.json" "$A/"
cp "$AW"/synthdefs/*.scsyndef "$A/synthdefs/"
cp "$SRC"/move/run-stack.sh "$SRC"/move/run-engine.sh "$SRC"/move/run-controller.sh \
   "$SRC"/move/stop-stack.sh "$A/"
cp "$SRC/VERSION" "$A/VERSION"
cp "$SRC/move/schwung-module/granola/module.json" "$SRC/move/schwung-module/granola/ui.js" "$M/"
find "$STAGE" \( -name __pycache__ -o -name '._*' -o -name .DS_Store \) -prune -exec rm -rf {} +
chmod +x "$A"/*.sh

REV=$(git -C "$SRC" rev-parse --short HEAD)
DIRTY=$(git -C "$SRC" status --porcelain | grep -q . && echo "+dirty" || true)
echo "granola-move $REV$DIRTY version $(cat "$SRC/VERSION")" > "$STAGE/SOURCE"

if tar --no-xattrs -cf /dev/null -T /dev/null 2>/dev/null; then TF=--no-xattrs; else TF=; fi
COPYFILE_DISABLE=1 tar $TF -C "$STAGE" -czf "$OUT" SOURCE app module
echo "$OUT  ($(du -h "$OUT" | cut -f1), $(cat "$STAGE/SOURCE"))"
