#!/usr/bin/env bash
# Source from setup.sh/build.sh. Keep all writable Arduino caches project-local.
CARDGPT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# A root invocation in a user's checkout must not leave root-owned build caches.
CARDGPT_OWNER_UID="$(stat -c '%u' "$CARDGPT_ROOT" 2>/dev/null || stat -f '%u' "$CARDGPT_ROOT")"
if [[ "$(id -u)" -eq 0 && "$CARDGPT_OWNER_UID" -ne 0 ]]; then
  exec sudo --preserve-env=ARDUINO_DIRECTORIES_DATA,ARDUINO_DIRECTORIES_DOWNLOADS,ARDUINO_DIRECTORIES_USER \
    -u "#$CARDGPT_OWNER_UID" bash "$0" "$@"
fi
export ARDUINO_DIRECTORIES_DATA="${ARDUINO_DIRECTORIES_DATA:-$CARDGPT_ROOT/.arduino/data}"
export ARDUINO_DIRECTORIES_DOWNLOADS="${ARDUINO_DIRECTORIES_DOWNLOADS:-$CARDGPT_ROOT/.arduino/staging}"
export ARDUINO_DIRECTORIES_USER="${ARDUINO_DIRECTORIES_USER:-$CARDGPT_ROOT/.arduino/user}"
CARDGPT_BOARD_URL='https://espressif.github.io/arduino-esp32/package_esp32_index.json'
command -v arduino-cli >/dev/null || { echo 'Instala arduino-cli y añádelo al PATH.' >&2; exit 1; }
