"""ESPotify desk stand for the Waveshare ESP32-C6-Touch-AMOLED-2.16.

A tilted cradle on a small pedestal. The USB-C plug goes down through a slot
in the cradle floor, the cable drops through the pedestal and leaves through a
groove under the base, so the stand sits flat with the cable out the back.

Measure your board and set BOARD_W / BOARD_H / BOARD_T below, then run:
    pip install manifold3d numpy
    python make_stand.py            # writes espotify-stand.stl
Print with the base flat on the bed; no supports needed.
"""
import math
import struct
import sys

import numpy as np
from manifold3d import CrossSection, JoinType, Manifold

# Board (mm). Measure yours: width and height of the front, and the total
# thickness including the parts on the back.
BOARD_W = 50.0
BOARD_H = 50.0
BOARD_T = 12.0
CLEARANCE = 0.5     # Gap around the board, per side

# Where the USB-C port sits on the bottom edge (0 = centred, + = to the right
# when looking at the screen)
USB_X = 0.0
USB_SLOT_W = 15.0   # Fits a normal USB-C plug's overmould (~12.5 x 7 mm)

# Cradle
WALL = 2.4
FLOOR = 3.0
LIP_H = 5.0         # Front lip that keeps the board in; lower it if it covers the screen
SIDE_H = 14.0       # Side walls (kept low so side buttons stay reachable)
BACK_H = 34.0       # Back wall
TILT = 15.0         # Degrees the screen leans back

# Pedestal and base
PEDESTAL_H = 28.0   # Room under the board for the plug and the cable bend
PEDESTAL_W = 32.0
PEDESTAL_D = 26.0
BASE_W = 70.0
BASE_D = 60.0
BASE_T = 11.0      # Thick enough to hide the cable groove underneath
BASE_R = 8.0        # Corner radius
BASE_Y = 4.0        # Base sits a little behind the cradle to balance the tilt
CABLE_H = 8.0       # Cable groove under the base; a USB-A or USB-C plug end fits through

BIG = 500.0


def box(x0, y0, z0, x1, y1, z1):
    return Manifold.cube([x1 - x0, y1 - y0, z1 - z0]).translate([x0, y0, z0])


def cradle():
    """Cradle in its own frame: screen faces -Y, board bottom edge on z = 0."""
    w = BOARD_W + 2 * CLEARANCE
    t = BOARD_T + 2 * CLEARANCE
    skirt = 8.0  # Extra block under the floor so the tilted cradle merges into the pedestal

    body = box(-w / 2 - WALL, -WALL, -FLOOR - skirt, w / 2 + WALL, t + WALL, BACK_H)
    body -= box(-w / 2, 0, 0, w / 2, t, BIG)                              # Board pocket
    body -= box(-BIG, -BIG, LIP_H, BIG, 0, BIG)                           # Open front above the lip
    body -= box(-BIG, -BIG, SIDE_H, BIG, t, BIG)                          # Low side walls, full back wall
    body -= box(USB_X - USB_SLOT_W / 2, 0, -BIG, USB_X + USB_SLOT_W / 2, t, 1)  # Plug slot through the floor
    return body, t


def stand():
    c, t = cradle()
    # Lean back around the middle of the cradle floor, then sit it on the pedestal
    c = c.translate([0, -t / 2, FLOOR]).rotate([-TILT, 0, 0]).translate([0, 0, PEDESTAL_H])

    rounded = CrossSection.square([BASE_W - 2 * BASE_R, BASE_D - 2 * BASE_R], center=True)
    rounded = rounded.offset(BASE_R, JoinType.Round, circular_segments=48)
    base = Manifold.extrude(rounded, BASE_T).translate([0, BASE_Y, 0])
    pedestal = box(-PEDESTAL_W / 2, -PEDESTAL_D / 2, 0, PEDESTAL_W / 2, PEDESTAL_D / 2, PEDESTAL_H + 1)

    body = base + pedestal + c

    # Cable path: up the middle of the pedestal into the floor slot, out the back under the base
    shaft_d = 14.0
    body -= box(USB_X - USB_SLOT_W / 2, -shaft_d / 2, -1, USB_X + USB_SLOT_W / 2, shaft_d / 2, PEDESTAL_H + 1)
    body -= box(USB_X - USB_SLOT_W / 2, -shaft_d / 2, -1, USB_X + USB_SLOT_W / 2, BIG, CABLE_H)
    return body


def board_ghost():
    """The board in place, for previews only."""
    w, t = BOARD_W, BOARD_T
    b = box(-w / 2, CLEARANCE, 0, w / 2, CLEARANCE + t, BOARD_H)
    tc = BOARD_T + 2 * CLEARANCE
    return b.translate([0, -tc / 2, FLOOR]).rotate([-TILT, 0, 0]).translate([0, 0, PEDESTAL_H])


def write_stl(manifold, path):
    mesh = manifold.to_mesh()
    verts = np.asarray(mesh.vert_properties)[:, :3]
    tris = np.asarray(mesh.tri_verts)
    with open(path, "wb") as f:
        f.write(b"ESPotify stand".ljust(80, b" "))
        f.write(struct.pack("<I", len(tris)))
        for a, b, c in tris:
            p0, p1, p2 = verts[a], verts[b], verts[c]
            n = np.cross(p1 - p0, p2 - p0)
            n = n / (np.linalg.norm(n) or 1)
            f.write(struct.pack("<12fH", *n, *p0, *p1, *p2, 0))
    return len(tris)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "espotify-stand.stl"
    s = stand()
    assert s.status().name == "NoError", s.status()
    n = write_stl(s, out)
    bb = s.bounding_box()
    print(f"{out}: {n} triangles, {s.volume() / 1000:.1f} cm3, "
          f"{bb[3] - bb[0]:.0f} x {bb[4] - bb[1]:.0f} x {bb[5] - bb[2]:.0f} mm, genus {s.genus()}")
