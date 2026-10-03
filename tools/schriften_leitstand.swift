// Erzeugt die Schriften der Leitstand-Anzeige aus Inter (SIL Open Font License 1.1).
//
// M5GFX bringt nur Schriften mit, deren Zeichenvorrat bei 0x7E endet: Umlaute,
// Eszett, Gradzeichen und Minus fehlen. Dieses Werkzeug rendert die noetigen
// Zeichen mit CoreText und schreibt sie im Format von Adafruit GFX, das M5GFX
// ohne Umweg zeichnet. Zeichen jenseits von Latin-1 kommen ueber
// Zeichenbereiche hinzu, damit die Tabelle nicht jede Luecke dazwischen fuehrt.
//
//   swift tools/schriften_leitstand.swift <Verzeichnis mit Inter> <Zieldatei>
//
// Erwartet die statischen Schnitte von Inter 4 (Inter_18pt-Regular.ttf usw.).

import CoreGraphics
import CoreText
import Foundation

let bereiche: [(UInt32, UInt32)] = [
    (0x20, 0x7E),     // ASCII
    (0xA0, 0xFF),     // Latin-1: Umlaute, Eszett, Grad, Mittelpunkt
    (0x2013, 0x2014), // Halbgeviert- und Geviertstrich
    (0x2026, 0x2026), // Auslassungspunkte
    (0x2039, 0x203A), // einfache Winkel fuer die Tastenbeschriftung
    (0x2192, 0x2192), // Pfeil nach rechts
    (0x2212, 0x2212), // Minus
]

// Name, Datei, Pixelgroesse
let schnitte: [(String, String, CGFloat)] = [
    ("inter_12", "Inter_18pt-Regular.ttf", 12),
    ("inter_14", "Inter_18pt-Regular.ttf", 14),
    ("inter_m12", "Inter_18pt-Medium.ttf", 12),
    ("inter_s16", "Inter_18pt-SemiBold.ttf", 16),
    ("inter_s22", "Inter_24pt-SemiBold.ttf", 22),
]

// Ab diesem Deckungsgrad gilt ein Bildpunkt als gesetzt. Etwas unter der
// Haelfte, damit duenne Striche bei 12 px nicht ausfallen.
let schwelle: UInt8 = 110

struct Zeichen {
    var versatz: Int
    var breite: Int
    var hoehe: Int
    var vorschub: Int
    var x: Int
    var y: Int
}

func schrift(_ pfad: String, _ groesse: CGFloat) -> CTFont {
    let url = URL(fileURLWithPath: pfad) as CFURL
    guard let d = (CTFontManagerCreateFontDescriptorsFromURL(url) as? [CTFontDescriptor])?.first else {
        fatalError("Schrift nicht lesbar: \(pfad)")
    }
    // Ziffern gleicher Breite: Messwerte springen beim Wechsel sonst seitlich.
    let tnum: [CFString: Any] = [kCTFontOpenTypeFeatureTag: "tnum", kCTFontOpenTypeFeatureValue: 1]
    let mitZiffern = CTFontDescriptorCreateCopyWithAttributes(
        d, [kCTFontFeatureSettingsAttribute: [tnum]] as CFDictionary)
    return CTFontCreateWithFontDescriptor(mitZiffern, groesse, nil)
}

func erzeuge(name: String, pfad: String, groesse: CGFloat) -> String {
    let font = schrift(pfad, groesse)
    let oben = CTFontGetAscent(font), unten = CTFontGetDescent(font), abstand = CTFontGetLeading(font)
    let breiteFlaeche = Int(groesse * 3) + 8
    let hoeheFlaeche = Int((oben + unten).rounded(.up)) + 8
    let grundlinieVonUnten = Int(unten.rounded(.up)) + 4
    let grundlinieZeile = hoeheFlaeche - grundlinieVonUnten
    let stift = 4

    var bits: [UInt8] = []
    var bitzahl = 0
    var zeichen: [Zeichen] = []
    var leerVorschub = Int(groesse / 4)

    for (von, bis) in bereiche {
        for cp in von...bis {
            var utf16 = Array(String(UnicodeScalar(cp)!).utf16)
            var glyph = [CGGlyph](repeating: 0, count: utf16.count)
            let gefunden = CTFontGetGlyphsForCharacters(font, &utf16, &glyph, utf16.count)
            var vorschubMass = CGSize.zero
            if gefunden {
                CTFontGetAdvancesForGlyphs(font, .horizontal, &glyph, &vorschubMass, 1)
            }
            let vorschub = gefunden ? Int(vorschubMass.width.rounded()) : leerVorschub
            if cp == 0x20 { leerVorschub = vorschub }

            var z = Zeichen(versatz: bitzahl / 8, breite: 0, hoehe: 0, vorschub: vorschub, x: 0, y: 0)
            if gefunden, cp != 0x20, cp != 0xA0 {
                let ctx = CGContext(data: nil, width: breiteFlaeche, height: hoeheFlaeche, bitsPerComponent: 8,
                                    bytesPerRow: breiteFlaeche, space: CGColorSpaceCreateDeviceGray(),
                                    bitmapInfo: CGImageAlphaInfo.none.rawValue)!
                ctx.setFillColor(gray: 0, alpha: 1)
                ctx.fill(CGRect(x: 0, y: 0, width: breiteFlaeche, height: hoeheFlaeche))
                ctx.setFillColor(gray: 1, alpha: 1)
                ctx.setShouldAntialias(true)
                ctx.setShouldSmoothFonts(false)
                var lage = CGPoint(x: stift, y: grundlinieVonUnten)
                CTFontDrawGlyphs(font, &glyph, &lage, 1, ctx)
                let daten = ctx.data!.bindMemory(to: UInt8.self, capacity: breiteFlaeche * hoeheFlaeche)
                var minX = breiteFlaeche, maxX = -1, minY = hoeheFlaeche, maxY = -1
                for y in 0..<hoeheFlaeche {
                    for x in 0..<breiteFlaeche where daten[y * breiteFlaeche + x] >= schwelle {
                        minX = min(minX, x); maxX = max(maxX, x)
                        minY = min(minY, y); maxY = max(maxY, y)
                    }
                }
                if maxX >= 0 {
                    z.breite = maxX - minX + 1
                    z.hoehe = maxY - minY + 1
                    z.x = minX - stift
                    z.y = minY - grundlinieZeile
                    // Adafruit GFX legt die Bits ohne Zeilenfuellung hintereinander.
                    if bitzahl % 8 != 0 {
                        bitzahl += 8 - bitzahl % 8
                    }
                    z.versatz = bitzahl / 8
                    for y in minY...maxY {
                        for x in minX...maxX {
                            if bitzahl / 8 >= bits.count { bits.append(0) }
                            if daten[y * breiteFlaeche + x] >= schwelle {
                                bits[bitzahl / 8] |= UInt8(0x80 >> (bitzahl % 8))
                            }
                            bitzahl += 1
                        }
                    }
                }
            }
            zeichen.append(z)
        }
    }
    if bitzahl % 8 != 0 { bitzahl += 8 - bitzahl % 8 }
    while bits.count < bitzahl / 8 { bits.append(0) }

    var s = "// \(name): \(CTFontCopyFullName(font) as String), \(Int(groesse)) px\n"
    s += "static const uint8_t \(name)_bits[] = {"
    for (i, b) in bits.enumerated() {
        s += (i % 20 == 0 ? "\n    " : " ") + String(format: "0x%02x,", b)
    }
    s += "\n};\n"
    s += "static const lgfx::GFXglyph \(name)_zeichen[] = {\n"
    for z in zeichen {
        s += "    {\(z.versatz), \(z.breite), \(z.hoehe), \(z.vorschub), \(z.x), \(z.y)},\n"
    }
    s += "};\n"
    s += "static const lgfx::EncodeRange \(name)_bereiche[] = {"
    var basis = 0
    for (von, bis) in bereiche {
        s += String(format: " {0x%04X, 0x%04X, %d},", von, bis, basis)
        basis += Int(bis - von + 1)
    }
    s += " };\n"
    let zeilenabstand = Int((oben + unten + abstand).rounded())
    s += "static const lgfx::GFXfont \(name)((uint8_t *)\(name)_bits, (lgfx::GFXglyph *)\(name)_zeichen, "
    s += String(format: "0x%04X, 0x%04X, %d, %d, (lgfx::EncodeRange *)%@_bereiche);\n\n",
                bereiche.first!.0, bereiche.last!.1, zeilenabstand, bereiche.count, name)
    return s
}

let args = CommandLine.arguments
guard args.count == 3 else {
    print("Aufruf: swift tools/schriften_leitstand.swift <Verzeichnis mit Inter> <Zieldatei>")
    exit(2)
}
var kopf = """
// Schriften der Leitstand-Anzeige. Erzeugt von tools/schriften_leitstand.swift,
// nicht von Hand aendern.
//
// Grundlage: Inter 4.001, Copyright 2016 The Inter Project Authors
// (https://github.com/rsms/inter), lizenziert unter der SIL Open Font License,
// Version 1.1; der Lizenztext liegt daneben in OFL.txt.
#pragma once

#include <lgfx/v1/lgfx_fonts.hpp>

namespace schrift {


"""
for (name, datei, groesse) in schnitte {
    kopf += erzeuge(name: name, pfad: (args[1] as NSString).appendingPathComponent(datei), groesse: groesse)
}
kopf += "}  // namespace schrift\n"
try! kopf.write(toFile: args[2], atomically: true, encoding: .utf8)
print("geschrieben: \(args[2])")
