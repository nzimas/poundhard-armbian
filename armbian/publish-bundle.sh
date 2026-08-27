#!/usr/bin/env bash
# Publish a built bundle as a GitHub release asset.
#
#   ./armbian/publish-bundle.sh [tag] [bundle-dir]
#
# install.sh downloads these when there is no local bundle, so the asset names
# have to match what it asks for: the bundle's relative paths with '/' replaced
# by '_' (GitHub release assets have a flat namespace).
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
TAG="${1:-armbian-bundle-v1}"
BUNDLE="${2:-$HERE/bundle/armbian}"
REPO="${PH_REPO:-nzimas/poundhard-armbian}"

[ -f "$BUNDLE/rootfs.tar.gz" ] || { echo "no bundle at $BUNDLE" >&2; exit 1; }
[ -f "$BUNDLE/SHA256SUMS" ]    || { echo "no SHA256SUMS in $BUNDLE" >&2; exit 1; }

echo "== verifying the bundle before publishing it"
( cd "$BUNDLE" && shasum -a 256 -c SHA256SUMS --quiet ) || { echo "checksums do not match" >&2; exit 1; }

KVER=$(sed -n 's/^kernel=//p' "$BUNDLE/MANIFEST")
if ! gh release view "$TAG" --repo "$REPO" >/dev/null 2>&1; then
    echo "== creating release $TAG"
    gh release create "$TAG" --repo "$REPO" \
        --title "Armbian bundle ($KVER)" \
        --notes "Armbian rootfs and boot payload for the Ableton Move, captured from a
working machine and used by \`install.sh\` to convert a stock Move.

Kernel: \`$KVER\`

You do not download these by hand — \`./install.sh <move-host>\` fetches and
checksum-verifies them when it finds no local bundle.

Contains no Ableton software: \`/opt/move\` and \`jack_move.so\` are taken from
your own device during the conversion, never redistributed here. Contains no
credentials, ssh host keys or machine-id — those are generated per device."
fi

echo "== uploading assets"
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
( cd "$BUNDLE" && find . -type f | sed 's|^\./||' ) | while read -r f; do
    flat=$(echo "$f" | tr '/' '_')
    ln "$BUNDLE/$f" "$STAGE/$flat" 2>/dev/null || cp "$BUNDLE/$f" "$STAGE/$flat"
    echo "   $f  ->  $flat"
done
gh release upload "$TAG" --repo "$REPO" --clobber "$STAGE"/*
echo "== published: https://github.com/$REPO/releases/tag/$TAG"
