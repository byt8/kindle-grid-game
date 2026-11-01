"""Print the extracted Rockman tiles as ASCII art to check 1-bit legibility."""
import re

src = open('src/rockman_data.h').read()
m = re.search(r'rm_tiles\[0x2D\]\[16\]\s*=\s*\{(.*?)\n\};', src, re.S)
rows = re.findall(r'\{([^{}]*)\}', m.group(1))
tiles = [[int(x, 16) for x in r.split(',') if x.strip()] for r in rows]

names = {
    0x05: '0', 0x06: '1', 0x07: '2', 0x08: '3', 0x09: '4', 0x0A: '5',
    0x0B: '6', 0x0C: '7', 0x0D: '8', 0x0E: '9', 0x0F: 'SC', 0x10: 'HI',
    0x11: 'unused', 0x12: 'BRICK', 0x13: 'DIRT', 0x14: 'EMPTY', 0x15: 'ROCK',
    0x16: 'FALL1', 0x17: 'FALL2', 0x18: 'FALL3', 0x19: 'DIAMOND', 0x1A: 'TRAP',
    0x1B: 'PL_UD1', 0x1C: 'PL_UD2', 0x1D: 'PL_L1', 0x1E: 'PL_L2',
    0x1F: 'PL_R1', 0x20: 'PL_R2', 0x21: 'MON_A1', 0x22: 'MON_A2',
    0x23: 'MON_B1', 0x24: 'MON_B2', 0x25: 'MON_C1', 0x26: 'MON_C2',
    0x29: 'EXPLODE1', 0x2A: 'EXPLODE2', 0x2B: 'EXPLODE3', 0x2C: 'EXPLODE4',
}
want = [0x12, 0x13, 0x14, 0x15, 0x19, 0x1A, 0x1B, 0x1D, 0x1F, 0x21, 0x23,
        0x25, 0x29, 0x05, 0x0F]
for t in want:
    g = tiles[t]
    print(f'--- {names[t]} (0x{t:02X}) ---')
    for line in g[:16]:
        print(''.join('#' if line & (0x80 >> i) else '.' for i in range(8)),
              f'  {line:02X}')
