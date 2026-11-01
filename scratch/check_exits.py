"""Check room-map top-exit cells against the up-exit table (one-off analysis)."""
import re

src = open('src/rockman_data.h').read()
m = re.search(r'rm_room_maps\[22\]\[28\]\s*=\s*\{(.*?)\n\};', src, re.S)
rooms = []
for rm in re.finditer(r'\{([^{}]*)\}', m.group(1)):
    rooms.append([int(x, 16) for x in rm.group(1).split(',') if x.strip()])
print("rooms parsed:", len(rooms), "bytes:", sorted({len(r) for r in rooms}))


def cell(r, c):
    b = rooms[r][c // 8]
    return (b >> (7 - (c % 8))) & 1


up = [0x10, 0x11, 0x12, 0, 0, 0x01, 0x02, 0x03, 0, 0,
      0x06, 0, 0x08, 0x09, 0, 0, 0, 0x0D, 0x0E, 0x0F]
print("room top(0,7) top(0,8) up-exit")
bad = 0
for i in range(1, 21):
    a, b = cell(i, 7), cell(i, 8)
    flag = ""
    if (a == 0 or b == 0) and up[i - 1] == 0:
        flag = "  <-- dirt top but NO up exit (room 0 bug)"
        bad += 1
    if (a == 1 and b == 1) and up[i - 1]:
        flag = "  <-- rock top but HAS up exit (exit unreachable)"
        bad += 1
    print(i, a, b, hex(up[i - 1]), flag)
print("inconsistencies:", bad)

# also: does any play room (1-20) have rock at the other three edges?
for name, c in (("left", 0), ("right", 15)):
    print(name, "dirt cells in rows 0-13:",
          sum(1 for i in range(1, 21) for r in range(14) if cell(i, c) == 0))
print("bottom row (13) dirt cells:",
      sum(1 for i in range(1, 21) for c in range(16) if cell(i, 13 * 16 // 16) == 0))
for i in range(1, 21):
    print("room", i, "row13 dirt count:", sum(1 for c in range(16) if cell(i, 13 * 16 + c) == 0),
          "col0 dirt:", sum(1 for r in range(14) if cell(i, r * 16 + 0) == 0),
          "col15 dirt:", sum(1 for r in range(14) if cell(i, r * 16 + 15) == 0))
