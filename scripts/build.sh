#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/env.sh"
mkdir -p "$CARDGPT_ROOT/build/firmware" "$CARDGPT_ROOT/dist"
arduino-cli compile \
  --fqbn 'esp32:esp32:m5stack_cardputer:FlashSize=8M,PartitionScheme=default_8MB,PSRAM=disabled,USBMode=hwcdc,CDCOnBoot=cdc' \
  --libraries "$CARDGPT_ROOT/lib" \
  --build-path "$CARDGPT_ROOT/build/firmware" \
  --warnings default "$CARDGPT_ROOT/firmware/CardGPT"
bash "$CARDGPT_ROOT/scripts/package-factory.sh"
