#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Regenerate the built-in assembler include from the preserved upstream HEX."""
import argparse
from pathlib import Path


def generate(source):
    image = {}
    eof = False
    for line in source.read_text().splitlines():
        if not line.startswith(':') or eof:
            raise ValueError('expected Intel HEX records followed by one EOF')
        record = bytes.fromhex(line[1:])
        if len(record) < 5 or len(record) != record[0] + 5 or sum(record) % 256:
            raise ValueError('invalid Intel HEX length or checksum')
        address = int.from_bytes(record[1:3], 'big')
        if record[3] == 1 and record[0] == 0:
            eof = True
        elif record[3] == 0:
            for offset, byte in enumerate(record[4:-1]):
                target = address + offset
                if target in image or not 0x8000 <= target <= 0xffff:
                    raise ValueError('overlapping or out-of-range interpreter data')
                image[target] = byte
        else:
            raise ValueError('unsupported upstream record type')
    if not eof or not image or min(image) != 0x8000 or len(image) != max(image) - min(image) + 1:
        raise ValueError('expected a contiguous TinyBASIC image at 8000h')
    lines = [
        '; Tiny BASIC V2.3: public-domain image published by PJRC.',
        '; Generated from tb51.hex; original source and provenance are in this directory.',
        '; Keep byte-for-byte equivalent to tb51.hex.',
    ]
    for address in range(min(image), max(image) + 1, 16):
        lines.append(f'        org {address:05x}h')
        lines.append('        db ' + ','.join(
            f'0{image[target]:02x}h' for target in range(address, min(address + 16, max(image) + 1))))
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='check the include without rewriting it')
    args = parser.parse_args()
    vendor = Path(__file__).resolve().parents[2] / 'third_party' / 'tb51'
    output = generate(vendor / 'tb51.hex')
    destination = vendor / 'image.inc'
    if args.check:
        if destination.read_text() != output:
            raise SystemExit('TinyBASIC include differs from upstream HEX; regenerate it')
        print('TinyBASIC include matches the upstream HEX')
    else:
        destination.write_text(output)
        print('Regenerated TinyBASIC image.inc')


if __name__ == '__main__':
    main()
