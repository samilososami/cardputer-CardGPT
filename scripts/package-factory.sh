#!/usr/bin/env bash
# Package build outputs, or the already-approved application, without a device.
set -euo pipefail
source "$(dirname "$0")/env.sh"
CARDGPT_APP="${1:-$CARDGPT_ROOT/build/firmware/CardGPT.ino.bin}"
CARDGPT_OUTPUT="${2:-$CARDGPT_ROOT/dist}"
CARDGPT_ESPTOOL="$ARDUINO_DIRECTORIES_DATA/packages/esp32/tools/esptool_py/5.3.1/esptool"
[[ -x "$CARDGPT_ESPTOOL" ]] || { echo 'Falta esptool 5.3.1: ejecuta scripts/setup.sh.' >&2; exit 1; }
mkdir -p "$CARDGPT_OUTPUT"
install -m 0644 "$CARDGPT_APP" "$CARDGPT_OUTPUT/CardGPT.bin"
"$CARDGPT_ESPTOOL" --chip esp32s3 merge-bin \
  --output "$CARDGPT_OUTPUT/CardGPT-full.bin" --pad-to-size 8MB \
  --flash-mode keep --flash-freq keep --flash-size keep \
  0x0 "$CARDGPT_ROOT/build/firmware/CardGPT.ino.bootloader.bin" \
  0x8000 "$CARDGPT_ROOT/build/firmware/CardGPT.ino.partitions.bin" \
  0xe000 "$CARDGPT_ROOT/build/firmware/boot_app0.bin" \
  0x10000 "$CARDGPT_APP"
python3 "$CARDGPT_ROOT/scripts/validate_factory.py" "$CARDGPT_OUTPUT/CardGPT-full.bin" --app "$CARDGPT_APP"
(cd "$CARDGPT_OUTPUT" && sha256sum CardGPT.bin CardGPT-full.bin > SHA256SUMS)
