#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/env.sh"
mkdir -p "$CARDGPT_ROOT/build/firmware" "$CARDGPT_ROOT/dist"
arduino-cli compile \
  --fqbn 'esp32:esp32:m5stack_cardputer:FlashSize=8M,PartitionScheme=default_8MB,PSRAM=disabled,USBMode=hwcdc,CDCOnBoot=cdc' \
  --libraries "$CARDGPT_ROOT/lib" \
  --build-path "$CARDGPT_ROOT/build/firmware" \
  --warnings default "$CARDGPT_ROOT/firmware/CardGPT"
install -m 0644 "$CARDGPT_ROOT/build/firmware/CardGPT.ino.bin" "$CARDGPT_ROOT/dist/CardGPT.bin"
python3 - "$CARDGPT_ROOT/dist/CardGPT.bin" <<'PY'
import hashlib, pathlib, sys
p = pathlib.Path(sys.argv[1])
data = p.read_bytes()
if data[0] != 0xe9 or len(data) > 0x200000:
    raise SystemExit('Imagen inválida o mayor de 2 MB; no instalar.')
print(f'{p}: {len(data)} bytes; SHA-256 {hashlib.sha256(data).hexdigest()}')
PY
