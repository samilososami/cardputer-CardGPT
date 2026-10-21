#!/usr/bin/env python3
"""Validate an offline, blank-data Arduino-ESP32 default_8MB factory package.

No serial ports, credentials, flash dumps or devices are accessed.
"""
import argparse
import hashlib
from pathlib import Path
import struct

FLASH_SIZE = 0x800000
APP_OFFSET = 0x10000


def require(condition, message):
    if not condition:
        raise ValueError(message)


def esp_image_length(data, offset):
    require(data[offset] == 0xE9, 'Cabecera ESP inválida')
    require(struct.unpack_from('<H', data, offset + 12)[0] == 9, 'Se requiere ESP32-S3')
    require(data[offset + 3] >> 4 == 3, 'La imagen debe declarar 8 MB')
    count = data[offset + 1]
    require(0 < count <= 16, 'Número de segmentos inválido')
    at = offset + 24
    checksum = 0xEF
    for _ in range(count):
        _, size = struct.unpack_from('<II', data, at)
        at += 8
        require(at + size < len(data), 'Segmento fuera de la imagen')
        for byte in data[at:at + size]:
            checksum ^= byte
        at += size
    end = at + (16 - (at - offset) % 16)
    require(data[end - 1] == checksum, 'Checksum ESP incorrecto')
    require(data[offset + 23] == 1, 'Falta el SHA-256 de la imagen ESP')
    require(hashlib.sha256(data[offset:end]).digest() == data[end:end + 32], 'SHA ESP incorrecto')
    return end + 32 - offset


def validate(data, app=None):
    require(len(data) == FLASH_SIZE, 'Se requiere una imagen completa de 8 MB')
    boot_length = esp_image_length(data, 0)
    require(boot_length < 0x8000, 'El bootloader invade la tabla')
    app_length = esp_image_length(data, APP_OFFSET)
    require(app_length <= 0x330000, 'La aplicación no cabe en app0')
    if app is not None:
        require(len(app) == app_length and data[APP_OFFSET:APP_OFFSET + app_length] == app,
                'La aplicación empaquetada no coincide byte a byte con el original')
    expected = [
        ('nvs', 1, 2, 0x9000, 0x5000),
        ('otadata', 1, 0, 0xE000, 0x2000),
        ('app0', 0, 0x10, 0x10000, 0x330000),
        ('app1', 0, 0x11, 0x340000, 0x330000),
        ('spiffs', 1, 0x82, 0x670000, 0x180000),
        ('coredump', 1, 3, 0x7F0000, 0x10000),
    ]
    for i, (label, kind, subtype, offset, size) in enumerate(expected):
        entry = struct.unpack_from('<HBBII16sI', data, 0x8000 + i * 32)
        require(entry == (0x50AA, kind, subtype, offset, size, label.encode().ljust(16, b'\0'), 0),
                f'Tabla default_8MB inesperada: {label}')
    md5_at = 0x8000 + len(expected) * 32
    require(data[md5_at:md5_at + 16] == b'\xeb\xeb' + b'\xff' * 14, 'Falta el MD5 de la tabla')
    require(hashlib.md5(data[0x8000:md5_at]).digest() == data[md5_at + 16:md5_at + 32],
            'MD5 incorrecto en la tabla')
    # Standard Arduino selector: OTA slot 0. No device-derived content allowed.
    require(hashlib.sha256(data[0xE000:0x10000]).hexdigest() ==
            'f94c5d786a7a8fab06ac5d10e33bf37711a6697636dc037559ea19cc410a17f0',
            'Selector de arranque inesperado')
    for start, end in [(boot_length, 0x8000), (md5_at + 32, 0xE000),
                       (APP_OFFSET + app_length, FLASH_SIZE)]:
        require(all(byte == 0xFF for byte in data[start:end]),
                f'La imagen contiene datos ajenos o no vacíos en 0x{start:x}–0x{end:x}')
    return app_length


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('--app', type=Path)
    args = parser.parse_args()
    content = args.image.read_bytes()
    length = validate(content, args.app.read_bytes() if args.app else None)
    print(f'Factory válido: {len(content):,} bytes; aplicación {length:,} bytes en 0x10000')
    print('Bootloader, tabla, selector, checksums y zonas vacías verificados.')
    print(f'SHA-256: {hashlib.sha256(content).hexdigest()}')
