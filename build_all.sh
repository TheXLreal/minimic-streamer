#!/usr/bin/env bash
# Build the firmware for all four Pico boards. Run from the release root.
# Output: dist/minimic_streamer_<version>_<board>.uf2 (override with OUT=<dir>, VERSION=<name>)
# Needs the Pico SDK; set PICO_SDK_PATH or it defaults to ~/.pico-sdk/sdk/2.3.1 (with TinyUSB 0.20.0, see README).
# Official release builds are made by GitHub Actions (.github/workflows/build.yml) with this script.
set -e
export PICO_SDK_PATH="${PICO_SDK_PATH:-$HOME/.pico-sdk/sdk/2.3.1}"
cd "$(dirname "$0")"
SRC="firmware"
OUT="${OUT:-dist}"
VERSION="${VERSION:-$(git describe --tags --always --dirty 2>/dev/null || echo dev)}"
mkdir -p "$OUT"
for BOARD in pico pico_w pico2 pico2_w; do
    echo "=== $BOARD ==="
    rm -rf "$SRC/build_$BOARD"
    cmake -S "$SRC" -B "$SRC/build_$BOARD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD="$BOARD" >/dev/null
    cmake --build "$SRC/build_$BOARD" >/dev/null
    cp "$SRC/build_$BOARD/minimic_streamer.uf2" "$OUT/minimic_streamer_${VERSION}_$BOARD.uf2"
    rm -rf "$SRC/build_$BOARD"
    echo "  -> $OUT/minimic_streamer_${VERSION}_$BOARD.uf2"
done
echo "done. (pico/pico_w are RP2040; pico2/pico2_w are RP2350)"
