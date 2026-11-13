#!/usr/bin/env python3
"""Write the CRC-32 (zlib / IEEE 802.3) of the flash image into its last word (.fw_crc).
The firmware recomputes it over 0x08000000 .. _fw_crc at every power-on (self-test item FW IMAGE,
kit-test item 1.1). Gaps are filled with 0xFF, the value erased flash reads, so the result is the
same whether the board is flashed from the .hex (gaps left erased) or from the .bin."""
import subprocess, sys, zlib, tempfile, os
raw, out = sys.argv[1], sys.argv[2]
FLASH = 0x08000000
syms = subprocess.check_output(['arm-none-eabi-nm', raw], text=True)
addr = next(int(l.split()[0], 16) for l in syms.splitlines() if l.endswith(' _fw_crc'))
with tempfile.TemporaryDirectory() as d:
    b = os.path.join(d, 'img.bin')
    subprocess.check_call(['arm-none-eabi-objcopy', '-O', 'binary', '--gap-fill', '0xFF', raw, b])
    img = open(b, 'rb').read()
    n = addr - FLASH
    if n + 4 != len(img):
        sys.exit(f'patch_crc: .fw_crc at offset {n} is not the last word of a {len(img)}-byte image')
    crc = zlib.crc32(img[:n]) & 0xFFFFFFFF
    c = os.path.join(d, 'crc.bin')
    open(c, 'wb').write(crc.to_bytes(4, 'little'))
    subprocess.check_call(['arm-none-eabi-objcopy', '--update-section', f'.fw_crc={c}', raw, out])
print(f'image CRC-32 0x{crc:08X} over {n} bytes')
