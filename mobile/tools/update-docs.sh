#!/usr/bin/env bash
# Refreshes everything GitHub shows of the app, then checks the docs:
#
#   mobile/tools/update-docs.sh RADIO_IP
#
# README screenshots and the manual's (against the real radio, receive only:
# nothing is keyed or changed on it), then mobile/tools/docs-check.py, which
# lists what still needs words (manual, TestFlight notes, roadmap).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
RADIO="${1:?usage: update-docs.sh RADIO_IP}"

"$ROOT/mobile/tools/update-screenshots.sh" "$RADIO"
"$ROOT/mobile/tools/update-manual-screenshots.sh" "$RADIO"
python3 "$ROOT/mobile/tools/docs-check.py" || true
