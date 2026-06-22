#!/usr/bin/env python3
# ============================================================================
# generate_pdf.py — embed the compiled game JS into a self-contained PDF.
#
#   python3 generate_pdf.py <input.js> <output.pdf>
#
# Builds a PDF whose page Open-action runs <input.js> (pokemon_pre.js + game.js).
# The "screen" is a 360x200 grid of text form-fields (field_0..field_199) that
# pokemon_pre.js fills with ASCII each frame; on-screen buttons + a text field
# feed input to pokemon_key(). This is the doompdf technique, adapted to
# Pokemon's controls and grid. Derived from ading2210/doompdf (AGPL) generate.py.
#
# Grid (W=360, H=200) MUST match Gfx::GRID_W/H in patch/Graphics.cpp and the
# dimensions used by update_framebuffer() in patch/pokemon_pre.js.
# ============================================================================
import sys

from pdfrw import PdfWriter
from pdfrw.objects.pdfname import PdfName
from pdfrw.objects.pdfstring import PdfString
from pdfrw.objects.pdfdict import PdfDict
from pdfrw.objects.pdfarray import PdfArray

# Must match Gfx::GRID_W/H in patch/Graphics.cpp. Higher = sharper/more readable
# (more text fields to repaint per frame, so slightly slower).
GRID_W = 480
GRID_H = 270


def create_script(js):
    action = PdfDict()
    action.S = PdfName.JavaScript
    action.JS = js
    return action


def create_page(width, height):
    page = PdfDict()
    page.Type = PdfName.Page
    page.MediaBox = PdfArray([0, 0, width, height])
    page.Resources = PdfDict()
    page.Resources.Font = PdfDict()
    page.Resources.Font.F1 = PdfDict()
    page.Resources.Font.F1.Type = PdfName.Font
    page.Resources.Font.F1.Subtype = PdfName.Type1
    page.Resources.Font.F1.BaseFont = PdfName.Courier
    return page


def create_field(name, x, y, width, height, value="", f_type=PdfName.Tx):
    a = PdfDict()
    a.Type = PdfName.Annot
    a.Subtype = PdfName.Widget
    a.FT = f_type
    a.Ff = 2
    a.Rect = PdfArray([x, y, x + width, y + height])
    a.T = PdfString.encode(name)
    a.V = PdfString.encode(value)
    a.BS = PdfDict()
    a.BS.W = 0
    return a


def create_button(name, x, y, width, height, value):
    b = create_field(name, x, y, width, height, f_type=PdfName.Btn)
    b.Ff = 65536
    b.MK = PdfDict()
    b.MK.BG = PdfArray([0.90])
    b.MK.CA = value
    return b


def create_key_buttons(keys_info):
    buttons = []
    for info in keys_info:
        b = create_button(info["name"] + "_button", info["x"], info["y"],
                          info["width"], info["height"], info["label"])
        b.AA = PdfDict()
        # Pulse on mouse-down so a quick tap reliably registers one step.
        b.AA.D = create_script("key_btn('%s')" % info["key"])
        buttons.append(b)
    return buttons


def create_text(x, y, size, txt):
    return "\nBT\n/F1 %d Tf\n%d %d Td (%s) Tj\nET\n" % (size, x, y, txt)


if __name__ == "__main__":
    with open(sys.argv[1]) as f:
        js = f.read()

    scale = 2
    width, height = GRID_W, GRID_H

    writer = PdfWriter()
    page = create_page(width * scale - 8, height * scale + 220)
    page.AA = PdfDict()
    page.AA.O = create_script("try {" + js + "} catch (e) { app.alert(e.stack || e) }")

    fields = []
    for i in range(0, height):
        fields.append(create_field("field_%d" % i, 0, i * scale + 220,
                                    width * scale - 8, scale, ""))
    for i in range(0, 25):
        fields.append(create_field("console_%d" % i, 8, 8 + i * 8, 300, 8, ""))

    inp = create_field("key_input", 470, 70, 220, 60,
                       "Type here to play (W/A/S/D, space, enter).")
    inp.AA = PdfDict()
    inp.AA.K = create_script("key_pressed(event.change)")
    fields.append(inp)

    # Pokemon controls. The D-pad tokens (padu/padd/padl/padr) drive both the
    # overworld (z/q/s/d) and menu arrows; ok=space+enter, cancel=esc.
    fields += create_key_buttons([
        {"name": "up",     "key": "padu",   "label": "^", "x": 360, "y": 150, "width": 34, "height": 34},
        {"name": "left",   "key": "padl",   "label": "<", "x": 322, "y": 112, "width": 34, "height": 34},
        {"name": "down",   "key": "padd",   "label": "v", "x": 360, "y": 112, "width": 34, "height": 34},
        {"name": "right",  "key": "padr",   "label": ">", "x": 398, "y": 112, "width": 34, "height": 34},
        {"name": "ok",     "key": "ok",     "label": "OK",  "x": 470, "y": 150, "width": 50, "height": 30},
        {"name": "cancel", "key": "cancel", "label": "ESC", "x": 524, "y": 150, "width": 50, "height": 30},
        {"name": "team",   "key": "team",   "label": "TEAM", "x": 578, "y": 150, "width": 54, "height": 30},
        {"name": "bag",    "key": "bag",    "label": "BAG",  "x": 636, "y": 150, "width": 50, "height": 30},
    ])

    page.Contents = PdfDict()
    page.Contents.stream = "\n".join([
        create_text(330, 195, 22, "Pokemon Grey - in a PDF"),
        create_text(470, 140, 9, "Move: D-pad or W/A/S/D.  OK=space/enter."),
        create_text(470, 128, 9, "TEAM=t  BAG=e  ESC=esc.  Shift to run."),
        create_text(330, 40, 8, "Only works in Chromium-based browsers (Chrome/Edge)."),
    ])
    page.Annots = PdfArray(fields)
    writer.addpage(page)
    writer.write(sys.argv[2])
    print("[*] wrote", sys.argv[2])
