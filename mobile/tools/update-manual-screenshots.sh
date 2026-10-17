#!/usr/bin/env bash
# Renders the manual's screenshots (docs/mobile/manual/*.jpg).
#
#   mobile/tools/update-manual-screenshots.sh [RADIO_IP]
#
# As update-screenshots.sh: with RADIO_IP the pages show that FlexRadio,
# receive only (nothing is keyed or changed on the radio); without it the
# built-in demo. Choosers are opened with --popup, drawer tabs with
# --controls-tab. Needs a configured AETHER_MOBILE build in build-mobile/.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build-mobile}"
OUT="$ROOT/docs/mobile/manual"
RADIO="${1:-}"
DELAY="${SCREENSHOT_DELAY_MS:-40000}"
APP="$BUILD/mobile/aether-mobile"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

cmake --build "$BUILD" --target aether-mobile --parallel >/dev/null
mkdir -p "$OUT"

if [ -n "$RADIO" ]; then
    src=(--connect "$RADIO")
else
    src=(--demo)
fi

shot() { # name size args...
    local name="$1" size="$2"
    shift 2
    QT_QPA_PLATFORM=offscreen timeout 90 "$APP" --size "$size" \
        --screenshot "$TMP/$name.png" --screenshot-delay "$DELAY" "$@" >/dev/null 2>&1
    echo "  $name ($size)"
}
# jpg NAME [CROP] [RESIZE]: write docs/mobile/manual/NAME.jpg
jpg() {
    local name="$1" crop="${2:-}" resize="${3:-}"
    local args=()
    [ -n "$crop" ] && args+=(-crop "$crop" +repage)
    [ -n "$resize" ] && args+=(-resize "$resize")
    magick "$TMP/$name.png" "${args[@]}" -strip -quality 85 "$OUT/$name.jpg"
}

echo "Rendering manual screenshots (${RADIO:-demo radio})..."
shot connect 1366x1024
shot overview 1366x1024 "${src[@]}" --controls visible
shot phone-portrait 440x956 "${src[@]}" --controls visible
shot phone-landscape 956x440 "${src[@]}" --controls visible
shot filter 1366x1024 "${src[@]}" --controls visible --popup filterChooser
shot agc 1366x1024 "${src[@]}" --controls visible --popup agcChooser
shot antenna 1366x1024 "${src[@]}" --controls visible --popup rxAntennaChooser
shot mode 1366x1024 "${src[@]}" --controls visible --popup modeChooser
shot meter 1366x1024 "${src[@]}" --controls visible --popup meterChooser
shot frequency 1366x1024 "${src[@]}" --controls visible --frequency-dialog
shot noslice 1366x1024 "${src[@]}" --controls visible --preview-no-slice
shot network 1366x1024 "${src[@]}" --controls visible --popup networkDialog
shot setup 1366x1024 "${src[@]}" --controls visible --popup radioSetup
shot tnf 1366x1024 "${src[@]}" --controls visible --popup tnfPopup
shot memory-edit 440x956 "${src[@]}" --controls drawer --controls-tab 6 --popup memoryEditDialog
shot tx-processing 440x956 "${src[@]}" --controls drawer --controls-tab 2 --controls-scroll 500
tabs=(dsp audio tx cw view profiles memory)
for i in "${!tabs[@]}"; do
    shot "tab-${tabs[$i]}" 440x956 "${src[@]}" --controls drawer --controls-tab "$i"
done

jpg connect "" 900x
jpg overview "" 1100x
jpg phone-portrait
jpg phone-landscape
cp "$TMP/overview.png" "$TMP/infopanel.png"
cp "$TMP/overview.png" "$TMP/slicerow.png"
cp "$TMP/overview.png" "$TMP/spectrum.png"
jpg infopanel 500x95+485+45
jpg slicerow 985x140+0+205
jpg spectrum 985x600+0+340 800x
jpg filter 620x420+470+40
jpg agc 560x270+470+40
jpg antenna 620x270+470+40
jpg mode 520x230+320+40
jpg meter 380x600+0+200
jpg frequency 700x720+333+150 500x
jpg noslice 985x600+0+340 800x
jpg network 470x450+448+288
jpg setup 640x600+363+50
jpg tnf 967x492+13+354 800x
jpg memory-edit 440x340+0+308
jpg tx-processing 440x520+0+436
for t in "${tabs[@]}"; do
    jpg "tab-$t" 440x520+0+436
done
echo "Updated $OUT"
