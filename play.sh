#!/bin/bash
# play.sh — build the ROM if needed and run it fullscreen in mednafen.
#   ./play.sh          normal ROM (out/rom.bin)
#   ./play.sh demo     self-playing ROM (out/rom-demo.bin)
# Mednafen keys: Esc quits, Alt+Enter toggles fullscreen; pad mapping is
# mednafen's (configure with Alt+Shift+1).
set -euo pipefail
cd "$(dirname "$0")"

if [ "${1:-}" = "demo" ]; then
    make rom-demo
    ROM=out/rom-demo.bin
else
    [ -f out/rom.bin ] || make rom
    ROM=out/rom.bin
fi

exec mednafen -video.fs 1 -md.stretch aspect "$ROM"
