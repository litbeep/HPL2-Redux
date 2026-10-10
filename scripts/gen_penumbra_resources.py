#!/usr/bin/env python3
"""Generate the Penumbra product's engine files into penumbra/resources.

Redux takes a few engine files from an Amnesia install. The Penumbra product
runs with no Amnesia install present, so it makes its own; nothing here is
copied or derived from Amnesia or Penumbra:

  core/textures/core_dissolve.tga      128x128 grey noise for the dissolve fade
                                       (GlobalManagedSets binds it; .r is used)
  core/textures/colorgrading_base.png  identity colour-grading table, 32 x 1024
                                       (PostEffect_ColorGrading's fallback)
  fonts/penumbra_default.fnt (+ _0.tga) AngelCode BMFont rasterised from DejaVu
                                       Sans (Bitstream Vera licence, see
                                       fonts/LICENSE-DejaVu.txt)
  gui/penumbra_default.skin (+ icons)  a flat GUI skin with every element
                                       cGui::GenerateSkinTypeStrings knows

The startup configs (config/, resources.cfg) are written by hand. The
outputs are committed, so building needs neither this script nor Pillow;
run it again after changing it:

  python3 scripts/gen_penumbra_resources.py [--font /path/to/DejaVuSans.ttf]

The noise, table, icons and skin are byte-identical on every run. The font
depends on the DejaVu Sans and FreeType versions it is rendered with.
"""

import argparse
import random
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_OUT = ROOT / "penumbra" / "resources"
FONT_SEARCH = [
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans.ttf",
    "/usr/local/share/fonts/DejaVuSans.ttf",
    "/Library/Fonts/DejaVuSans.ttf",
]
FONT_LICENSE_SEARCH = [
    "/usr/share/doc/fonts-dejavu-core/copyright",
    "/usr/share/licenses/dejavu-fonts/LICENSE",
    "/usr/share/doc/dejavu-fonts/LICENSE",
]


# ---------------------------------------------------------------------------
# Image writers (no dependencies)

def write_tga(path, width, height, pixels, channels):
    """Uncompressed TGA, bottom-left origin. pixels: rows top to bottom, each
    a bytes of width*channels in RGB(A) order."""
    assert channels in (3, 4)
    header = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0,
                         width, height, channels * 8, 8 if channels == 4 else 0)
    body = bytearray()
    for row in reversed(pixels):
        for x in range(width):
            px = row[x * channels:(x + 1) * channels]
            body += bytes((px[2], px[1], px[0])) + px[3:]  # BGR(A)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(header + bytes(body))


def write_png_rgb(path, width, height, pixels):
    """8-bit RGB PNG. pixels: rows top to bottom, each bytes of width*3."""
    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data +
                struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff))
    raw = b"".join(b"\x00" + row for row in pixels)
    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(raw, 9)) +
           chunk(b"IEND", b""))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


# ---------------------------------------------------------------------------
# core_dissolve.tga

def gen_dissolve(out):
    # White noise with an exactly flat histogram: every grey level appears
    # 64 times, so a dissolve threshold sweeping 0..1 removes the same share
    # of pixels per step. Fixed seed: the same file on every run.
    size = 128
    values = [i * 256 // (size * size) for i in range(size * size)]
    random.Random(0x50454E55).shuffle(values)
    rows = []
    for y in range(size):
        row = bytearray()
        for v in values[y * size:(y + 1) * size]:
            row += bytes((v, v, v))
        rows.append(bytes(row))
    write_tga(out / "core" / "textures" / "core_dissolve.tga", size, size, rows, 3)


# ---------------------------------------------------------------------------
# colorgrading_base.png

def gen_colorgrading(out):
    # N slices of N x N stacked vertically. As posteffect_color_grading.frag
    # samples it: green runs along x, red along the rows of a slice, blue
    # selects the slice. An identity table returns its lookup colour.
    n = 32
    level = [round(i * 255 / (n - 1)) for i in range(n)]
    rows = []
    for b in range(n):
        for r in range(n):
            rows.append(bytes(c for g in range(n) for c in (level[r], level[g], level[b])))
    write_png_rgb(out / "core" / "textures" / "colorgrading_base.png", n, n * n, rows)


# ---------------------------------------------------------------------------
# Font

FONT_NAME = "penumbra_default"
FONT_PX = 20
# Printable ASCII and Latin-1: the languages Penumbra shipped in.
FONT_CHARS = list(range(32, 127)) + list(range(160, 256))


def gen_font(out, font_path):
    from PIL import Image, ImageFont

    font = ImageFont.truetype(str(font_path), FONT_PX)
    ascent, descent = font.getmetrics()
    line_height = ascent + descent

    glyphs = []
    for code in FONT_CHARS:
        ch = chr(code)
        left, top, right, bottom = font.getbbox(ch, anchor="la")
        glyphs.append((code, ch, left, top, max(0, right - left), max(0, bottom - top),
                       round(font.getlength(ch))))

    pad = 1
    for page_w, page_h in ((256, 256), (512, 256), (512, 512)):
        placed, x, y, row_h = [], pad, pad, 0
        for g in glyphs:
            w, h = g[4], g[5]
            if x + w + pad > page_w:
                x, y, row_h = pad, y + row_h + pad, 0
            if y + h + pad > page_h:
                break
            placed.append((g, x, y))
            x += w + pad
            row_h = max(row_h, h)
        if len(placed) == len(glyphs):
            break
    else:
        sys.exit("font does not fit on one 512x512 page")

    coverage = Image.new("L", (page_w, page_h), 0)
    for (code, ch, left, top, w, h, adv), x, y in placed:
        if w and h:
            glyph = Image.new("L", (w, h), 0)
            from PIL import ImageDraw
            ImageDraw.Draw(glyph).text((-left, -top), ch, font=font, fill=255, anchor="la")
            coverage.paste(glyph, (x, y))

    # White glyphs, coverage in alpha.
    data = coverage.tobytes()
    rows = []
    for py in range(page_h):
        row = bytearray()
        for a in data[py * page_w:(py + 1) * page_w]:
            row += bytes((255, 255, 255, a))
        rows.append(bytes(row))
    page_file = f"{FONT_NAME}_0.tga"
    write_tga(out / "fonts" / page_file, page_w, page_h, rows, 4)

    lines = [
        '<?xml version="1.0"?>',
        "<font>",
        f'  <info face="DejaVu Sans" size="{FONT_PX}" bold="0" italic="0" charset="" unicode="1" '
        'stretchH="100" smooth="1" aa="1" padding="0,0,0,0" spacing="1,1" />',
        f'  <common lineHeight="{line_height}" base="{ascent}" scaleW="{page_w}" scaleH="{page_h}" '
        'pages="1" packed="0" />',
        "  <pages>",
        f'    <page id="0" file="{page_file}" />',
        "  </pages>",
        f'  <chars count="{len(placed)}">',
    ]
    for (code, ch, left, top, w, h, adv), x, y in placed:
        lines.append(f'    <char id="{code}" x="{x}" y="{y}" width="{w}" height="{h}" '
                     f'xoffset="{left}" yoffset="{top}" xadvance="{adv}" page="0" chnl="15" />')
    lines += ["  </chars>", "</font>", ""]
    (out / "fonts" / f"{FONT_NAME}.fnt").write_text("\n".join(lines), encoding="utf-8")


def copy_font_license(out, license_path):
    dest = out / "fonts" / "LICENSE-DejaVu.txt"
    text = Path(license_path).read_text(encoding="utf-8", errors="replace")
    dest.write_text(
        "fonts/penumbra_default.fnt and penumbra_default_0.tga are a bitmap\n"
        "rendering of DejaVu Sans made by scripts/gen_penumbra_resources.py.\n"
        "DejaVu's licence follows.\n\n" + text, encoding="utf-8")


# ---------------------------------------------------------------------------
# GUI icons and skin

def gen_icon(out, name, size, shapes):
    """A white-on-transparent icon from polygons (lists of (x, y))."""
    from PIL import Image, ImageDraw
    img = Image.new("L", size, 0)
    draw = ImageDraw.Draw(img)
    for shape in shapes:
        if shape[0] == "line":
            draw.line(shape[1], fill=255, width=shape[2])
        else:
            draw.polygon(shape, fill=255)
    data = img.tobytes()
    w, h = size
    rows = [bytes(c for a in data[y * w:(y + 1) * w] for c in (255, 255, 255, a)) for y in range(h)]
    write_tga(out / "gui" / f"{name}.tga", w, h, rows, 4)
    return f"{name}.tga"


def gen_icons(out):
    icons = {}
    icons["pointer"] = gen_icon(out, "penumbra_gui_pointer", (12, 18),
                                [[(0, 0), (0, 15), (4, 11), (7, 17), (9, 16), (6, 10), (11, 10)]])
    icons["text"] = gen_icon(out, "penumbra_gui_pointer_text", (7, 15),
                             [("line", [(0, 0), (6, 0)], 1), ("line", [(3, 0), (3, 14)], 1),
                              ("line", [(0, 14), (6, 14)], 1)])
    icons["check"] = gen_icon(out, "penumbra_gui_check", (10, 10),
                              [("line", [(1, 5), (4, 8), (9, 1)], 2)])
    icons["up"] = gen_icon(out, "penumbra_gui_arrow_up", (8, 8), [[(0, 6), (7, 6), (3.5, 1)]])
    icons["down"] = gen_icon(out, "penumbra_gui_arrow_down", (8, 8), [[(0, 1), (7, 1), (3.5, 6)]])
    icons["left"] = gen_icon(out, "penumbra_gui_arrow_left", (8, 8), [[(6, 0), (6, 7), (1, 3.5)]])
    icons["right"] = gen_icon(out, "penumbra_gui_arrow_right", (8, 8), [[(1, 0), (1, 7), (6, 3.5)]])
    icons["cross"] = gen_icon(out, "penumbra_gui_cross", (8, 8),
                              [("line", [(0, 0), (7, 7)], 2), ("line", [(0, 7), (7, 0)], 2)])
    return icons


# Colours of the flat skin (r g b a).
DARK = "0.11 0.12 0.13 1"
PANEL = "0.18 0.19 0.21 1"
FIELD = "0.08 0.09 0.10 1"
EDGE = "0.36 0.38 0.42 1"
EDGE_LIT = "0.52 0.55 0.60 1"
ACCENT = "0.40 0.30 0.16 1"
ACCENT_LIT = "0.58 0.44 0.24 1"
TEXT_ICON = "0.85 0.85 0.82 1"
DIM_ICON = "0.45 0.45 0.45 1"


def frame(prefix, background, edge, border=2, extra=()):
    """The nine pieces of a bordered box: borders, corners, background."""
    items = [(f"{prefix}BorderRight", "", f"{border} 1", edge),
             (f"{prefix}BorderLeft", "", f"{border} 1", edge),
             (f"{prefix}BorderUp", "", f"1 {border}", edge),
             (f"{prefix}BorderDown", "", f"1 {border}", edge)]
    items += [(f"{prefix}Corner{c}", "", f"{border} {border}", edge) for c in ("LU", "RU", "RD", "LD")]
    if background is not None:
        items.append((f"{prefix}Background", "", "1 1", background))
    return items + list(extra)


def gen_skin(out, icons):
    gfx = [
        ("PointerNormal", icons["pointer"], None, TEXT_ICON),
        ("PointerText", icons["text"], None, TEXT_ICON, "-3 -7"),
    ]
    gfx += frame("Window", DARK, EDGE)
    gfx += [("WindowLabel", "", "1 1", ACCENT),
            ("WindowButtonCross", icons["cross"], None, TEXT_ICON)]
    gfx += frame("Frame", PANEL, EDGE, extra=[("FrameBackgroundColorPicking", "", "1 1", FIELD)])
    gfx += [("CheckBoxEnabledUnchecked", "", "12 12", FIELD),
            ("CheckBoxEnabledChecked", icons["check"], None, TEXT_ICON),
            ("CheckBoxDisabledUnchecked", "", "12 12", PANEL),
            ("CheckBoxDisabledChecked", icons["check"], None, DIM_ICON),
            ("TextBoxBackground", "", "1 1", FIELD),
            ("TextBoxSelectedTextBack", "", "1 1", ACCENT),
            ("TextBoxMarker", "", "1 1", TEXT_ICON),
            ("ListBoxBackground", "", "1 1", FIELD),
            ("ComboBoxButtonIcon", icons["down"], None, TEXT_ICON)]
    gfx += frame("ComboBox", FIELD, EDGE, border=1)
    gfx += frame("SliderHandle", PANEL, EDGE_LIT, border=1)
    gfx += [("SliderVertArrowUp", icons["up"], None, TEXT_ICON),
            ("SliderVertArrowDown", icons["down"], None, TEXT_ICON),
            ("SliderVertBackground", "", "1 1", FIELD),
            ("SliderHoriArrowUp", icons["left"], None, TEXT_ICON),
            ("SliderHoriArrowDown", icons["right"], None, TEXT_ICON),
            ("SliderHoriBackground", "", "1 1", FIELD)]
    gfx += frame("ButtonUp", PANEL, EDGE_LIT)
    gfx += frame("ButtonDown", ACCENT, ACCENT_LIT)
    gfx += [("MenuEnabledChecked", icons["check"], None, TEXT_ICON),
            ("MenuDisabledChecked", icons["check"], None, DIM_ICON),
            ("MenuSeparatorLine", "", "1 1", EDGE),
            ("MenuItemArrow", icons["right"], None, TEXT_ICON)]
    gfx += frame("TabLabel", None, EDGE, border=1)
    gfx += frame("Tab", None, EDGE, border=1)
    gfx += [("TabLabelBackground", "", "1 1", PANEL),
            ("TabLabelBackgroundActive", "", "1 1", ACCENT),
            ("TabBackground", "", "1 1", DARK),
            ("TabFrameArrowLeft", icons["left"], None, TEXT_ICON),
            ("TabFrameArrowRight", icons["right"], None, TEXT_ICON)]
    gfx += frame("Group", PANEL, EDGE, border=1)
    gfx += [("FilePickerUpButton", icons["up"], None, TEXT_ICON),
            ("FilePickerIconGraphics", "", "12 12", "0.30 0.50 0.70 1"),
            ("FilePickerIconModels", "", "12 12", "0.50 0.65 0.35 1"),
            ("FilePickerIconSounds", "", "12 12", "0.70 0.55 0.30 1"),
            ("FilePickerIconText", "", "12 12", "0.75 0.75 0.75 1"),
            ("FilePickerIconOther", "", "12 12", "0.45 0.45 0.45 1"),
            ("FilePickerIconFolder", "", "12 12", ACCENT_LIT)]

    attributes = [
        ("WindowLabelTextOffset", "6 3"),
        ("WindowButtonSize", "14 14"),
        ("WindowBordersOutside", "1"),
        ("ButtonPressedContentOffset", "1 1"),
        ("SliderButtonSize", "14"),
        ("ListBoxSliderWidth", "14"),
        ("ComboBoxButtonWidth", "14"),
        ("ComboBoxSliderWidth", "14"),
        ("ContextMenuItemArrowSize", "8 8"),
        ("ContextMenuItemCheckSize", "10 10"),
        ("ContextMenuItemHPadding", "4"),
        ("ContextMenuItemVPadding", "2"),
        ("ContextMenuItemSeparation", "2"),
        ("ContextMenuItemTextLeftPadding", "18"),
        ("ContextMenuItemTextRightPadding", "18"),
        ("ContextMenuSeparatorPadding", "4"),
        ("MainMenuItemTextPadding", "8"),
        ("MainMenuItemVPadding", "2"),
        ("MainMenuItemSeparation", "2"),
        ("MenuSeparatorPadding", "4"),
        ("MenuSeparatorHeight", "1"),
        ("TabFrameButtonSize", "14 14"),
        ("TabFrameIconPadding", "4"),
        ("TabFrameTextLeftPadding", "6"),
        ("TabFrameTextRightPadding", "6"),
        ("TabFrameLabelInactiveOffset", "0 2"),
        ("TabFrameLabelActiveOffset", "0 0"),
        ("FirstTabOffset", "4"),
        ("TabSeparation", "2"),
        ("GroupHeaderOffset", "8 0"),
        ("GroupHeaderOverUpperBorder", "1"),
        ("DefaultFontHighlightColor", "1 0.85 0.55"),
    ]

    font = f"{FONT_NAME}.fnt"
    lines = [
        "<!-- Penumbra product: a flat default GUI skin, written by",
        "     scripts/gen_penumbra_resources.py. Elements without a file are",
        "     filled rectangles of their colour and active_size. -->",
        "<GuiSkin>",
        "  <Fonts>",
        f'    <Font type="Default" file="{font}" size="14 14" color="0.88 0.88 0.85 1" />',
        f'    <Font type="Disabled" file="{font}" size="14 14" color="0.45 0.45 0.45 1" />',
        f'    <Font type="WindowLabel" file="{font}" size="15 15" color="1 1 1 1" />',
        "  </Fonts>",
        "  <Attributes>",
    ]
    lines += [f'    <Attribute type="{name}" value="{value}" />' for name, value in attributes]
    lines += ["  </Attributes>", "  <GfxElements>"]
    for item in gfx:
        name, file, size, color = item[:4]
        offset = item[4] if len(item) > 4 else "0 0"
        size = size or "-1 -1"
        lines.append(f'    <Gfx type="{name}" file="{file}" offset="{offset}" active_size="{size}" '
                     f'color="{color}" material="Alpha" />')
    lines += ["  </GfxElements>", "</GuiSkin>", ""]
    (out / "gui" / "penumbra_default.skin").write_text("\n".join(lines), encoding="utf-8")


# ---------------------------------------------------------------------------

def find_first(paths):
    for p in paths:
        if Path(p).is_file():
            return Path(p)
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT,
                        help="output directory (default: penumbra/resources)")
    parser.add_argument("--font", type=Path, help="DejaVuSans.ttf to rasterise")
    parser.add_argument("--font-license", type=Path, help="DejaVu licence text to ship with it")
    args = parser.parse_args()

    font_path = args.font or find_first(FONT_SEARCH)
    license_path = args.font_license or find_first(FONT_LICENSE_SEARCH)
    if font_path is None or license_path is None:
        sys.exit("DejaVuSans.ttf or its licence not found; pass --font and --font-license")
    try:
        import PIL  # noqa: F401
    except ImportError:
        sys.exit("Pillow is required for the font and the GUI icons (pip install Pillow)")

    out = args.out
    gen_dissolve(out)
    gen_colorgrading(out)
    gen_font(out, font_path)
    copy_font_license(out, license_path)
    gen_skin(out, gen_icons(out))
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
