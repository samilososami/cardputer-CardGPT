#!/usr/bin/env bash
# Source from setup.sh/build.sh. Keep all writable Arduino caches project-local.
CARDGPT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export ARDUINO_DIRECTORIES_DATA="${ARDUINO_DIRECTORIES_DATA:-$CARDGPT_ROOT/.arduino/data}"
export ARDUINO_DIRECTORIES_DOWNLOADS="${ARDUINO_DIRECTORIES_DOWNLOADS:-$CARDGPT_ROOT/.arduino/staging}"
export ARDUINO_DIRECTORIES_USER="${ARDUINO_DIRECTORIES_USER:-$CARDGPT_ROOT/.arduino/user}"
CARDGPT_BOARD_URL='https://espressif.github.io/arduino-esp32/package_esp32_index.json'
command -v arduino-cli >/dev/null || { echo 'Instala arduino-cli y añádelo al PATH.' >&2; exit 1; }
