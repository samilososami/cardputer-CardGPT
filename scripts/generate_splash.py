#!/usr/bin/env python3
"""Create the monochrome CardGPT boot bitmap and compact header logo."""

from pathlib import Path

from PIL import Image, ImageOps
import qrcode
from qrcode.constants import ERROR_CORRECT_M


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets" / "cardgpt-splash-source-v2.png"
PREVIEW = ROOT / "assets" / "cardgpt-splash-240x135.png"
QR_PREVIEW = ROOT / "assets" / "cardgpt-device-login-qr.png"
HEADER = ROOT / "firmware" / "CardGPT" / "SplashBitmap.h"


def monochrome(image: Image.Image, size: tuple[int, int]) -> Image.Image:
    gray = ImageOps.grayscale(image).resize(size, Image.Resampling.LANCZOS)
    return gray.point(lambda value: 255 if value >= 178 else 0, mode="1")


def xbm_bytes(image: Image.Image) -> list[int]:
    """Pack black pixels as XBM foreground bits (LSB first)."""
    width, height = image.size
    pixels = image.load()
    result: list[int] = []
    for y in range(height):
        for byte_x in range((width + 7) // 8):
            value = 0
            for bit in range(8):
                x = byte_x * 8 + bit
                if x < width and pixels[x, y] == 0:
                    value |= 1 << bit
            result.append(value)
    return result


def format_array(name: str, data: list[int]) -> str:
    lines = []
    for offset in range(0, len(data), 16):
        lines.append("  " + ", ".join(f"0x{byte:02x}" for byte in data[offset : offset + 16]))
    return f"inline constexpr uint8_t {name}[] PROGMEM = {{\n" + ",\n".join(lines) + "\n};\n"


def main() -> None:
    source = Image.open(SOURCE).convert("RGB")
    splash = monochrome(source, (240, 135))
    splash.convert("L").save(PREVIEW)

    # Bounding box of the knot mark in the generated source artwork.
    logo_source = source.crop((534, 70, 1135, 667))
    logo = monochrome(logo_source, (14, 14))

    qr = qrcode.QRCode(version=4, error_correction=ERROR_CORRECT_M, box_size=1, border=0)
    qr.add_data("https://auth.openai.com/codex/device")
    qr.make(fit=False)
    matrix = qr.get_matrix()
    qr_source = Image.new("1", (len(matrix), len(matrix)), 1)
    qr_pixels = qr_source.load()
    for y, row in enumerate(matrix):
        for x, filled in enumerate(row):
            qr_pixels[x, y] = 0 if filled else 1
    qr_image = qr_source.resize((len(matrix) * 2, len(matrix) * 2), Image.Resampling.NEAREST)
    qr_preview = Image.new("1", (qr_image.width + 16, qr_image.height + 16), 1)
    qr_preview.paste(qr_image, (8, 8))
    qr_preview.convert("L").save(QR_PREVIEW)

    content = """#pragma once

#include <Arduino.h>

inline constexpr int CARDGPT_SPLASH_WIDTH = 240;
inline constexpr int CARDGPT_SPLASH_HEIGHT = 135;
inline constexpr int CARDGPT_LOGO_WIDTH = 14;
inline constexpr int CARDGPT_LOGO_HEIGHT = 14;
inline constexpr int CARDGPT_QR_WIDTH = 66;
inline constexpr int CARDGPT_QR_HEIGHT = 66;

"""
    content += format_array("CARDGPT_SPLASH_XBM", xbm_bytes(splash))
    content += "\n"
    content += format_array("CARDGPT_LOGO_XBM", xbm_bytes(logo))
    content += "\n"
    content += format_array("CARDGPT_DEVICE_QR_XBM", xbm_bytes(qr_image))
    HEADER.write_text(content, encoding="utf-8")
    print(f"Wrote {HEADER}")
    print(f"Wrote {PREVIEW}")
    print(f"Wrote {QR_PREVIEW}")


if __name__ == "__main__":
    main()

