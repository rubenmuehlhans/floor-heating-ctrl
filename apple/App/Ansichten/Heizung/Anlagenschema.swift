import SwiftUI
import Anlage

/// Anlagenschema nach dem Vorbild der Weboberfläche: Ölkessel, Pufferspeicher, Mischer und
/// Heizkreise; Messwerte stehen dort, wo der Fühler sitzt. Quer auf iPad und Mac, hochkant auf
/// dem iPhone, damit die Beschriftung lesbar bleibt.
struct Anlagenschema: View {
    let anlage: Anlagenbild
    @Environment(\.horizontalSizeClass) private var klasse

    private var quer: Bool { klasse != .compact }
    private var entwurf: CGSize { quer ? CGSize(width: 780, height: 410) : CGSize(width: 340, height: 500) }

    var body: some View {
        Canvas { ctx, groesse in
            let s = groesse.width / entwurf.width
            ctx.scaleBy(x: s, y: s)
            if quer { zeichneQuer(&ctx) } else { zeichneHoch(&ctx) }
        }
        .aspectRatio(entwurf.width / entwurf.height, contentMode: .fit)
        .frame(maxWidth: quer ? 880 : 460)
        .frame(maxWidth: .infinity)
        .accessibilityElement()
        .accessibilityLabel("Anlagenschema")
        .accessibilityValue(beschreibung)
    }

    private var beschreibung: String {
        let k = anlage.kessel
        let s = anlage.speicher
        let kreise = anlage.heizkreise.map { "\($0.name) Vorlauf \(Format.temperatur($0.vorlauf)), Rücklauf \(Format.temperatur($0.ruecklauf))" }
        var teile: [String] = []
        if let k {
            teile.append("Brenner \(k.brennerLaeuft ? "läuft" : "aus"), Abgas \(Format.temperatur(k.abgas)), Kesselvorlauf \(Format.temperatur(k.vorlauf)), Kesselrücklauf \(Format.temperatur(k.ruecklauf))")
        }
        if let s {
            teile.append("Speicher \(Format.temperatur(s.temperatur)), Ladung \(Format.prozent(s.ladung))")
        }
        return (teile + kreise).joined(separator: ". ")
    }

    // MARK: Quer, wie im Browser

    private func zeichneQuer(_ c: inout GraphicsContext) {
        let k = anlage.kessel
        // Abgas
        linie(&c, [CGPoint(x: 150, y: 150), CGPoint(x: 150, y: 92)], Farbe.waerme)
        kicker(&c, "Abgas", CGPoint(x: 168, y: 84), anker: .leading)
        wert(&c, Format.temperatur(k?.abgas), CGPoint(x: 168, y: 106), Farbe.waerme, anker: .leading)
        // Kessel
        kessel(&c, CGRect(x: 75, y: 150, width: 150, height: 160))
        kicker(&c, "Ölkessel", CGPoint(x: 150, y: 334))
        // Kesselkreis
        linie(&c, [CGPoint(x: 225, y: 185), CGPoint(x: 385, y: 185)], Farbe.waerme)
        kicker(&c, "Vorlauf", CGPoint(x: 312, y: 168))
        wert(&c, Format.temperatur(k?.vorlauf), CGPoint(x: 312, y: 206), Farbe.waerme)
        linie(&c, [CGPoint(x: 385, y: 275), CGPoint(x: 225, y: 275)], Farbe.kaelte)
        kicker(&c, "Rücklauf", CGPoint(x: 312, y: 258))
        wert(&c, Format.temperatur(k?.ruecklauf), CGPoint(x: 312, y: 296), Farbe.kaelte)
        pumpe(&c, CGPoint(x: 248, y: 275), laeuft: anlage.kesselkreispumpe?.laeuft ?? false, gestoert: anlage.kesselkreispumpe.map { $0.relais.gestoert } ?? false)
        // Speicher
        speicher(&c, CGRect(x: 385, y: 95, width: 110, height: 250))
        kicker(&c, "Pufferspeicher", CGPoint(x: 440, y: 76))
        kicker(&c, "Oben", CGPoint(x: 440, y: 148))
        wert(&c, Format.temperatur(anlage.speicher?.temperatur), CGPoint(x: 440, y: 170), Farbe.waerme)
        kicker(&c, "Ladung", CGPoint(x: 440, y: 262))
        wert(&c, Format.prozent(anlage.speicher?.ladung), CGPoint(x: 440, y: 284), Farbe.tinte)
        // Heizkreise
        for (i, kreis) in anlage.heizkreise.prefix(2).enumerated() {
            let vl: CGFloat = i == 0 ? 150 : 290
            let rl = vl + 50
            linie(&c, [CGPoint(x: 495, y: vl), CGPoint(x: 680, y: vl)], Farbe.waerme)
            linie(&c, [CGPoint(x: 680, y: rl), CGPoint(x: 495, y: rl)], Farbe.kaelte)
            linie(&c, [CGPoint(x: 568, y: vl + 14), CGPoint(x: 568, y: rl)], Farbe.kaelte)
            mischer(&c, CGPoint(x: 568, y: vl))
            pumpe(&c, CGPoint(x: 630, y: vl), laeuft: kreis.pumpeLaeuft, gestoert: kreis.relais.gestoert)
            kicker(&c, "Vorlauf", CGPoint(x: 690, y: vl - 12), anker: .leading)
            wert(&c, Format.temperatur(kreis.vorlauf), CGPoint(x: 690, y: vl + 9), Farbe.waerme, anker: .leading)
            kicker(&c, "Rücklauf", CGPoint(x: 690, y: rl - 12), anker: .leading)
            wert(&c, Format.temperatur(kreis.ruecklauf), CGPoint(x: 690, y: rl + 9), Farbe.kaelte, anker: .leading)
            kicker(&c, kreis.versorgteVerteiler.joined(separator: " und "), CGPoint(x: 588, y: rl + 24))
            if kreis.relais.gestoert {
                kicker(&c, "Relais nicht erreichbar", CGPoint(x: 588, y: rl + 40), farbe: Farbe.stoerung)
            } else if kreis.gesperrt {
                kicker(&c, "von der Kesselregelung gesperrt", CGPoint(x: 588, y: rl + 40), farbe: Farbe.warnung)
            }
        }
    }

    // MARK: Hochkant, für das iPhone

    private func zeichneHoch(_ c: inout GraphicsContext) {
        let k = anlage.kessel
        linie(&c, [CGPoint(x: 70, y: 70), CGPoint(x: 70, y: 38)], Farbe.waerme)
        kicker(&c, "Abgas", CGPoint(x: 84, y: 30), anker: .leading)
        wert(&c, Format.temperatur(k?.abgas), CGPoint(x: 84, y: 50), Farbe.waerme, anker: .leading)
        kessel(&c, CGRect(x: 20, y: 70, width: 100, height: 110))
        kicker(&c, "Ölkessel", CGPoint(x: 70, y: 198))

        // Der Speicher reicht bis unter die Heizkreise, die seitlich abgehen.
        speicher(&c, CGRect(x: 222, y: 40, width: 98, height: 412))
        kicker(&c, "Pufferspeicher", CGPoint(x: 271, y: 24))
        kicker(&c, "Oben", CGPoint(x: 271, y: 78))
        wert(&c, Format.temperatur(anlage.speicher?.temperatur), CGPoint(x: 271, y: 98), Farbe.waerme)
        kicker(&c, "Ladung", CGPoint(x: 271, y: 156))
        wert(&c, Format.prozent(anlage.speicher?.ladung), CGPoint(x: 271, y: 176), Farbe.tinte)

        linie(&c, [CGPoint(x: 120, y: 96), CGPoint(x: 222, y: 96)], Farbe.waerme)
        kicker(&c, "Vorlauf", CGPoint(x: 176, y: 81))
        wert(&c, Format.temperatur(k?.vorlauf), CGPoint(x: 176, y: 113), Farbe.waerme)
        linie(&c, [CGPoint(x: 222, y: 156), CGPoint(x: 120, y: 156)], Farbe.kaelte)
        kicker(&c, "Rücklauf", CGPoint(x: 188, y: 141))
        wert(&c, Format.temperatur(k?.ruecklauf), CGPoint(x: 186, y: 173), Farbe.kaelte)
        pumpe(&c, CGPoint(x: 134, y: 156), laeuft: anlage.kesselkreispumpe?.laeuft ?? false, gestoert: anlage.kesselkreispumpe.map { $0.relais.gestoert } ?? false)

        // Heizkreise wie im Browser, nur gespiegelt: Vorlauf aus dem Speicher über Mischer und
        // Pumpe nach links, Rücklauf zurück, der Mischer mischt aus dem Rücklauf bei. So kreuzt
        // keine Leitung eine andere.
        for (i, kreis) in anlage.heizkreise.prefix(2).enumerated() {
            let vl: CGFloat = i == 0 ? 250 : 384
            let rl = vl + 50
            linie(&c, [CGPoint(x: 222, y: vl), CGPoint(x: 96, y: vl)], Farbe.waerme)
            linie(&c, [CGPoint(x: 96, y: rl), CGPoint(x: 222, y: rl)], Farbe.kaelte)
            linie(&c, [CGPoint(x: 178, y: vl + 14), CGPoint(x: 178, y: rl)], Farbe.kaelte)
            mischer(&c, CGPoint(x: 178, y: vl))
            pumpe(&c, CGPoint(x: 128, y: vl), laeuft: kreis.pumpeLaeuft, gestoert: kreis.relais.gestoert)
            kicker(&c, "Vorlauf", CGPoint(x: 48, y: vl - 12))
            wert(&c, Format.temperatur(kreis.vorlauf), CGPoint(x: 48, y: vl + 10), Farbe.waerme)
            kicker(&c, "Rücklauf", CGPoint(x: 48, y: rl - 12))
            wert(&c, Format.temperatur(kreis.ruecklauf), CGPoint(x: 48, y: rl + 10), Farbe.kaelte)
            kicker(&c, kreis.versorgteVerteiler.joined(separator: " und "), CGPoint(x: 118, y: rl + 36))
            if kreis.relais.gestoert {
                kicker(&c, "Relais nicht erreichbar", CGPoint(x: 118, y: rl + 52), farbe: Farbe.stoerung)
            } else if kreis.gesperrt {
                kicker(&c, "von der Kesselregelung gesperrt", CGPoint(x: 118, y: rl + 52), farbe: Farbe.warnung)
            }
        }
    }

    // MARK: Zeichenbausteine

    private func linie(_ c: inout GraphicsContext, _ punkte: [CGPoint], _ farbe: Color, breite: CGFloat = 3) {
        var p = Path()
        p.addLines(punkte)
        c.stroke(p, with: .color(farbe), style: StrokeStyle(lineWidth: breite, lineCap: .round, lineJoin: .round))
    }

    private func kicker(_ c: inout GraphicsContext, _ text: String, _ punkt: CGPoint, anker: UnitPoint = .center, farbe: Color = Farbe.gedaempft) {
        let t = Text(text.uppercased())
            .font(.system(size: 10.5, weight: .semibold))
            .tracking(1.2)
            .foregroundStyle(farbe)
        c.draw(t, at: punkt, anchor: anker)
    }

    private func wert(_ c: inout GraphicsContext, _ text: String, _ punkt: CGPoint, _ farbe: Color, anker: UnitPoint = .center) {
        let t = Text(text)
            .font(.system(size: 16, weight: .semibold, design: .monospaced))
            .foregroundStyle(farbe)
        c.draw(t, at: punkt, anchor: anker)
    }

    private func kessel(_ c: inout GraphicsContext, _ r: CGRect) {
        let form = Path(roundedRect: r, cornerRadius: 10)
        c.fill(form, with: .color(Farbe.vertieft))
        c.stroke(form, with: .color(Farbe.linieStark), lineWidth: 1.5)
        let laeuft = anlage.kessel?.brennerLaeuft ?? false
        var flamme = c.resolve(Image(systemName: laeuft ? "flame.fill" : "flame"))
        flamme.shading = .color(laeuft ? Farbe.waerme : Farbe.waerme.opacity(0.75))
        let seite = min(r.width, r.height) * 0.3
        c.draw(flamme, in: CGRect(x: r.midX - seite / 2, y: r.midY - seite / 2, width: seite, height: seite))
    }

    private func speicher(_ c: inout GraphicsContext, _ r: CGRect) {
        let aussen = Path(roundedRect: r, cornerRadius: 14)
        c.fill(aussen, with: .color(Farbe.flaeche))
        c.stroke(aussen, with: .color(Farbe.linieStark), lineWidth: 1.5)
        let innen = r.insetBy(dx: 5, dy: 5)
        c.fill(Path(roundedRect: innen, cornerRadius: 10), with: .color(Farbe.waermeWeich))
        // Füllstand als Linie am Rand
        let hoehe = innen.height * min(max(anlage.speicher?.ladung ?? 0, 0), 1)
        let stand = CGRect(x: innen.maxX - 5, y: innen.maxY - hoehe, width: 5, height: hoehe)
        c.fill(Path(roundedRect: stand, cornerRadius: 2), with: .color(Farbe.waerme))
    }

    private func mischer(_ c: inout GraphicsContext, _ m: CGPoint) {
        let kreis = Path(ellipseIn: CGRect(x: m.x - 14, y: m.y - 14, width: 28, height: 28))
        c.fill(kreis, with: .color(Farbe.flaeche))
        c.stroke(kreis, with: .color(Farbe.linieStark), lineWidth: 1.5)
        var links = Path()
        links.addLines([CGPoint(x: m.x - 10, y: m.y - 7), CGPoint(x: m.x, y: m.y), CGPoint(x: m.x - 10, y: m.y + 7)])
        links.closeSubpath()
        var rechts = Path()
        rechts.addLines([CGPoint(x: m.x + 10, y: m.y - 7), CGPoint(x: m.x, y: m.y), CGPoint(x: m.x + 10, y: m.y + 7)])
        rechts.closeSubpath()
        c.fill(links, with: .color(Farbe.waerme))
        c.fill(rechts, with: .color(Farbe.waerme))
        var antrieb = Path()
        antrieb.addLines([CGPoint(x: m.x, y: m.y + 1), CGPoint(x: m.x - 5, y: m.y + 10), CGPoint(x: m.x + 5, y: m.y + 10)])
        antrieb.closeSubpath()
        c.stroke(antrieb, with: .color(Farbe.kaelte), lineWidth: 1.3)
    }

    private func pumpe(_ c: inout GraphicsContext, _ m: CGPoint, laeuft: Bool, gestoert: Bool) {
        let kreis = Path(ellipseIn: CGRect(x: m.x - 10, y: m.y - 10, width: 20, height: 20))
        c.fill(kreis, with: .color(Farbe.flaeche))
        let rand = gestoert ? Farbe.stoerung : (laeuft ? Farbe.waerme : Farbe.linieStark)
        c.stroke(kreis, with: .color(rand), lineWidth: gestoert || laeuft ? 2 : 1.5)
        var dreieck = Path()
        dreieck.addLines([CGPoint(x: m.x - 4, y: m.y - 6), CGPoint(x: m.x + 6, y: m.y), CGPoint(x: m.x - 4, y: m.y + 6)])
        dreieck.closeSubpath()
        c.fill(dreieck, with: .color(laeuft && !gestoert ? Farbe.waerme : Farbe.gedaempft))
        if gestoert {
            let punkt = Path(ellipseIn: CGRect(x: m.x + 5, y: m.y - 14, width: 10, height: 10))
            c.fill(punkt, with: .color(Farbe.stoerung))
            var x = Path()
            x.move(to: CGPoint(x: m.x + 8, y: m.y - 11)); x.addLine(to: CGPoint(x: m.x + 12, y: m.y - 7))
            x.move(to: CGPoint(x: m.x + 12, y: m.y - 11)); x.addLine(to: CGPoint(x: m.x + 8, y: m.y - 7))
            c.stroke(x, with: .color(.white), lineWidth: 1.4)
        }
    }
}
