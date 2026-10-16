#!/usr/bin/env bash
# Renders the README screenshots (docs/mobile/*.jpg) for every layout size.
#
#   mobile/tools/update-screenshots.sh [RADIO_IP]
#
# With RADIO_IP the operate pages show that FlexRadio (receive only: nothing
# is keyed or changed on the radio); without it they use the built-in demo.
# The LSB passband shot always uses the demo, so a real radio's mode is
# never touched. Needs a configured AETHER_MOBILE build in build-mobile/.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build-mobile}"
OUT="$ROOT/docs/mobile"
RADIO="${1:-}"
DELAY="${SCREENSHOT_DELAY_MS:-40000}"
APP="$BUILD/mobile/aether-mobile"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

cmake --build "$BUILD" --target aether-mobile --parallel >/dev/null
mkdir -p "$OUT"

shot() { # name size args...
    local name="$1" size="$2"
    shift 2
    QT_QPA_PLATFORM=offscreen timeout 90 "$APP" --size "$size" \
        --screenshot "$TMP/$name.png" --screenshot-delay "$DELAY" "$@" >/dev/null 2>&1
    echo "  $name ($size)"
}

if [ -n "$RADIO" ]; then
    source_args=(--connect "$RADIO")
else
    source_args=(--demo)
fi

echo "Rendering screenshots (${RADIO:-demo radio})..."
shot ipad-landscape 1366x1024 "${source_args[@]}" --controls visible
shot ipad-mini-portrait 744x1133 "${source_args[@]}" --controls visible
shot iphone-portrait 440x956 "${source_args[@]}" --controls visible
shot iphone-landscape 956x440 "${source_args[@]}" --controls visible
shot iphone-landscape-drawer 956x440 "${source_args[@]}" --controls drawer
shot passband-lsb 1366x1024 --demo --mode LSB --controls visible

# Strip metadata and compress for the README.
magick "$TMP/ipad-landscape.png" -resize 1100x -strip -quality 85 "$OUT/ipad-landscape.jpg"
magick "$TMP/ipad-mini-portrait.png" -resize 600x -strip -quality 85 "$OUT/ipad-mini-portrait.jpg"
magick "$TMP/iphone-portrait.png" -strip -quality 88 "$OUT/iphone-portrait.jpg"
magick "$TMP/iphone-landscape.png" -strip -quality 88 "$OUT/iphone-landscape.jpg"
magick "$TMP/iphone-landscape-drawer.png" -strip -quality 88 "$OUT/iphone-landscape-drawer.jpg"
magick "$TMP/passband-lsb.png" -crop 990x760+0+0 -resize 700x -strip -quality 85 "$OUT/passband-lsb.jpg"
echo "Updated $OUT"
