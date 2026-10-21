#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/env.sh"
arduino-cli core update-index --additional-urls "$CARDGPT_BOARD_URL"
arduino-cli core install esp32:esp32@3.3.11 --additional-urls "$CARDGPT_BOARD_URL"
arduino-cli lib update-index
arduino-cli lib install 'M5Unified@0.2.27' 'M5GFX@0.2.34' 'ArduinoJson@7.4.2'
echo 'Dependencias preparadas. Ejecuta bash scripts/build.sh.'
