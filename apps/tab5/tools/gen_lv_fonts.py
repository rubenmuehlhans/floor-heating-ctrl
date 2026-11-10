#!/usr/bin/env python3
"""LVGL-Schriften und Symbole der Bedientafel.

    cd apps/tab5/tools && npm install
    ~/panelvenv/bin/python3 gen_lv_fonts.py      # braucht fontTools

Erzeugt nach `apps/tab5/main/gen/`:
  - je Schriftrolle eine `f_*.c` (Inter bzw. JetBrains Mono, 4 bpp),
  - je Symbolgröße eine `f_sym*.c` aus Lucide,
  - `schriften.h` mit den Deklarationen,
  - `symbole.c/.h` mit der Zuordnung Symbolname → Zeichen.

Vorbild ist der Tab5 von camperSense (satellite-display/tab5/tools/gen_lv_fonts.py).

⚠️ Die Grade folgen den Schriftrollen der App (apple/App/Ansichten/Gemeinsam/
Gestaltung.swift) mit rund 1,5 px je Punkt; die kleinsten sind auf 17 px
angehoben, damit sie am Glas aus Armlänge lesbar bleiben.

⚠️ Inter steht für SF Pro, JetBrains Mono für die monospaced-Werte der App
(`.monospaced()`). Beide unter der SIL Open Font License (assets/fonts).

⚠️ Große Ziffernschriften tragen NUR Ziffern und Satzzeichen. Wer dort ein Wort
setzt, bekommt Kästchen; `fmt::nur_ziffern()` entscheidet in der Firmware.
"""
import json
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
TAFEL = HERE.parent
FONTS = TAFEL / "assets" / "fonts"
OUT = TAFEL / "main" / "gen"
CONV = HERE / "node_modules" / ".bin" / "lv_font_conv"
LUCIDE = HERE / "node_modules" / "lucide-static" / "font"

# Text: ASCII, Latin-1 und die Zeichen, die App, Firmware und Assistent setzen.
TEXT = sorted(set(range(0x20, 0x7F)) | (set(range(0xA0, 0x100)) - {0xAD}) | {
    0x2013, 0x2014,          # – —
    0x2212,                  # − Minuszeichen
    0x2018, 0x2019, 0x201A,  # ‘ ’ ‚
    0x201C, 0x201D, 0x201E,  # “ ” „
    0x2022, 0x2026,          # • …
    0x2039, 0x203A,          # ‹ ›
    0x2190, 0x2191, 0x2192, 0x2193, 0x2194,  # ← ↑ → ↓ ↔
    0x2248, 0x2260, 0x2264, 0x2265,          # ≈ ≠ ≤ ≥
    0x20AC,                  # €
    0x0394,                  # Δ (Spreizung)
    0x2122,                  # ™
})
# Große Textschriften (ab 30 px): ASCII und das Deutsche; der volle Vorrat
# wäre bei 46 px über 500 KB.
GROSS = sorted(set(range(0x20, 0x7F)) | {ord(c) for c in "äöüÄÖÜß°–−·…„“"} | {0xA0})
# Ziffernschriften: Werte, Uhrzeit, Prozent. Kein Buchstabe.
ZAHLEN = sorted({ord(c) for c in "0123456789,.:+-% °/"} | {0x2212, 0x2013, 0xA0})

# Inter: optische Größen 18/24/28 pt der statischen Schnitte
I18R = "Inter_18pt-Regular.ttf"
I18S = "Inter_18pt-SemiBold.ttf"
I24M = "Inter_24pt-Medium.ttf"
I24S = "Inter_24pt-SemiBold.ttf"
I28S = "Inter_28pt-SemiBold.ttf"
I28B = "Inter_28pt-Bold.ttf"
JBM = "JetBrainsMono-Medium.ttf"
JBB = "JetBrainsMono-Bold.ttf"

# (C-Name, Datei, Pixel, Zeichen, Rolle der App)
JOBS = [
    ("f_kicker", I18S, 17, TEXT, "kicker: caption2 semibold, Versalien"),
    ("f_cap", I18R, 19, TEXT, "caption"),
    ("f_cap_m", I24M, 19, TEXT, "caption medium: Zustandsmarken"),
    ("f_sub", I18R, 22, TEXT, "subheadline, erklaerung"),
    ("f_sub_m", I24M, 22, TEXT, "subheadline medium: Befundtitel"),
    ("f_sub_s", I24S, 22, TEXT, "subheadline semibold: Raumname in Kacheln"),
    ("f_callout", I18R, 24, TEXT, "callout: Einheiten, Rueckmeldung"),
    ("f_body", I18R, 26, TEXT, "body: Fliesstext"),
    ("f_body_s", I24S, 26, TEXT, "headline, kartentitel"),
    ("f_t3", I24M, 30, GROSS, "title3 medium: Kurztext des Lageberichts"),
    ("f_t3_s", I28S, 30, GROSS, "title3 semibold"),
    ("f_titel", I28B, 40, GROSS, "Seitentitel (largeTitle der Navigation)"),
    ("f_tafel", I28B, 46, GROSS, "Anzeigetafel: Textwerte"),
    ("f_mono_k", JBM, 18, TEXT, "datenKlein: caption monospaced"),
    ("f_mono", JBM, 20, TEXT, "daten: footnote monospaced"),
    ("f_wert", JBB, 26, TEXT, "wert: body monospaced semibold"),
    ("f_messwert", JBB, 33, GROSS, "messwert: title2 monospaced semibold"),
    ("f_gross", JBB, 50, ZAHLEN, "messwertGross: largeTitle monospaced bold"),
    ("f_tafelwert", JBB, 64, ZAHLEN, "Anzeigetafel: Werte"),
    ("f_raum", JBB, 82, ZAHLEN, "Raumdetail: Messwert 56 pt"),
]

# Symbole: Lucide-Namen. Die Zuordnung der SF-Symbole der App steht in
# main/ui/stil.cpp (sf → lucide); hier nur, was dort vorkommt.
ICONS = sorted(set("""
house layout-grid sparkles flame cpu octagon-x circle-check sliders-horizontal triangle-alert
refresh-cw chevron-right chevron-left chevron-down chevron-up activity plus check circle-x square
package circle-plus key hand undo-2 router wifi-off wifi drama rotate-ccw circle-dot circle-play
info hourglass settings circle-alert file-up cylinder history circle badge-check chart-line
chart-scatter bell-off circle-arrow-right arrow-right move-horizontal wrench wand-sparkles
thermometer-snowflake thermometer message-square-text toggle-left stethoscope square-pen download
sliders-vertical radar crosshair ruler power plug lock list fan gauge hard-drive ellipsis droplet
radio file-text file-search shield-check chart-column brain bell bell-ring arrow-up-to-line
circle-arrow-up arrow-up repeat arrow-down-to-line antenna eye clock-alert arrow-left x send
delete arrow-big-up eye-off sun moon monitor waves zap clock calendar trash-2 cloud cloud-off
map-pin droplets snowflake
""".split()))
ICON_JOBS = [
    ("f_sym19", 19, "in Zeilen und Marken"),
    ("f_sym24", 24, "Kartenkoepfe, Listen"),
    ("f_sym32", 32, "Leiste"),
    ("f_sym48", 48, "Hinweise, leere Seiten"),
]


def ranges_arg(cps):
    out, start, prev = [], None, None
    for cp in cps:
        if start is None:
            start = prev = cp
        elif cp == prev + 1:
            prev = cp
        else:
            out.append((start, prev))
            start = prev = cp
    if start is not None:
        out.append((start, prev))
    args = []
    for a, b in out:
        args += ["-r", f"0x{a:X}" if a == b else f"0x{a:X}-0x{b:X}"]
    return args


def pruefe_zeichen(face, cps, name):
    """⚠️ lv_font_conv überspringt fehlende Zeichen mit einer Warnung. Hier hart."""
    from fontTools import ttLib
    cmap = ttLib.TTFont(face).getBestCmap()
    fehlt = [cp for cp in cps if cp not in cmap]
    if fehlt:
        sys.exit(f"FEHLER: {name} hat keine Glyphe für " + ", ".join(f"U+{c:04X}" for c in fehlt))


def konvertiere(name, face, cps, size):
    dst = OUT / f"{name}.c"
    cmd = [str(CONV), "--font", str(face), *ranges_arg(cps), "--size", str(size),
           "--format", "lvgl", "--bpp", "4", "--no-compress", "--force-fast-kern-format",
           "--lv-include", "lvgl.h", "-o", str(dst)]
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
    return dst.stat().st_size


def utf8_c(cp):
    return "".join(f"\\x{b:02X}" for b in chr(cp).encode("utf-8"))


def main():
    if not CONV.exists() or not LUCIDE.exists():
        sys.exit(f"Werkzeuge fehlen → cd {HERE} && npm install")
    OUT.mkdir(parents=True, exist_ok=True)
    for alt in OUT.glob("f_*.c"):
        alt.unlink()

    decl = []
    for name, file, size, cps, wozu in JOBS:
        face = FONTS / file
        # Was eine Schrift nicht trägt, fällt still weg (Inter hat kein U+2009?) — hart prüfen.
        pruefe_zeichen(face, cps, name)
        n = konvertiere(name, face, cps, size)
        decl.append(f"LV_FONT_DECLARE({name});   // {size} px · {wozu}")
        print(f"  {name}.c  {len(cps):3d} Zeichen  {n // 1024:4d} KB")

    codepoints = json.loads((LUCIDE / "codepoints.json").read_text())
    fehlt = [n for n in ICONS if n not in codepoints]
    if fehlt:
        sys.exit("FEHLER: Lucide kennt diese Symbole nicht: " + ", ".join(fehlt))
    icon_cps = sorted(codepoints[n] for n in ICONS)
    for name, size, wozu in ICON_JOBS:
        n = konvertiere(name, LUCIDE / "lucide.ttf", icon_cps, size)
        decl.append(f"LV_FONT_DECLARE({name});   // {size} px · Symbole · {wozu}")
        print(f"  {name}.c  {len(icon_cps):3d} Symbole  {n // 1024:4d} KB")

    (OUT / "schriften.h").write_text(
        "// ERZEUGT von tools/gen_lv_fonts.py — nicht von Hand ändern.\n"
        "#pragma once\n#include \"lvgl.h\"\n\n#ifdef __cplusplus\nextern \"C\" {\n#endif\n"
        + "\n".join(decl) + "\n#ifdef __cplusplus\n}\n#endif\n")

    zeilen = [f'    {{"{n}", "{utf8_c(codepoints[n])}"}},' for n in ICONS]
    (OUT / "symbole.c").write_text(
        "// ERZEUGT von tools/gen_lv_fonts.py — nicht von Hand ändern.\n"
        "// Lucide-Name → Zeichen in den f_sym*-Schriften, nach Name sortiert.\n"
        "#include \"symbole.h\"\n\n"
        "const sym_eintrag_t SYMBOLE[] = {\n" + "\n".join(zeilen) + "\n};\n"
        f"const int SYMBOLE_N = {len(ICONS)};\n")
    (OUT / "symbole.h").write_text(
        "// ERZEUGT von tools/gen_lv_fonts.py — nicht von Hand ändern.\n"
        "#pragma once\n#ifdef __cplusplus\nextern \"C\" {\n#endif\n"
        "typedef struct { const char *name; const char *utf8; } sym_eintrag_t;\n"
        "extern const sym_eintrag_t SYMBOLE[];   // nach name sortiert\n"
        "extern const int SYMBOLE_N;\n"
        "#ifdef __cplusplus\n}\n#endif\n")
    print(f"  symbole.c  {len(ICONS)} Namen")


if __name__ == "__main__":
    main()
