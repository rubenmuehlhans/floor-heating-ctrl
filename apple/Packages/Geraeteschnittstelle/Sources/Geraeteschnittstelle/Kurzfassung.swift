import Foundation

/// Kurzfassung eines Gerätezustands für die KI: nur, was für Beurteilung und Rat zählt, Zahlen
/// gerundet, deutsche Schlüssel mit Einheit im Namen. Rohwerte wie Gegenspannungen und
/// Tastenmesswerte bleiben weg; wer sie braucht, fragt die Rohdaten ab.
extension Verteilerzustand {
    public var kurzfassung: JSONWert {
        var o = Kurz()
        o["geraet"] = geraet?.id.map(JSONWert.text)
        o["ort"] = geraet?.ort.map(JSONWert.text)
        o["firmware"] = version.map(JSONWert.text)
        o["signal_dbm"] = Kurz.zahl(netz?.signal)
        o["laufzeit_h"] = Kurz.zahl(laufzeitSekunden.map { Double($0) / 3600 }, stellen: 1)
        if let a = aussen, a.zugeordnet == true {
            var aus = Kurz()
            aus["c"] = a.gueltig == true ? Kurz.zahl(a.temperaturC) : nil
            aus["feuchte"] = Kurz.zahl(a.feuchte, stellen: 0)
            aus["alter_s"] = Kurz.zahl(a.alterS)
            aus["gueltig"] = a.gueltig.map(JSONWert.bool)
            o["aussen"] = aus.wert
        }
        o["raeume"] = raeume.map { r in .liste(r.map(\.kurzfassung)) }
        o["kanaele"] = kanaele.map { k in .liste(k.map(\.kurzfassung)) }
        if let m = messfahrt, let z = m.zustand, z != "idle" {
            o["messfahrt"] = ["zustand": .text(z), "kanal": Kurz.zahl(m.kanal) ?? .null]
        }
        return o.wert
    }
}

extension Verteilerzustand.Raum {
    var kurzfassung: JSONWert {
        var o = Kurz()
        o["id"] = Kurz.zahl(id)
        o["name"] = name.map(JSONWert.text)
        o["betrieb"] = betriebsart.map(JSONWert.text)
        o["soll_c"] = Kurz.zahl(sollC)
        if thermometerZugeordnet == false {
            o["thermometer"] = "keines zugeordnet"
        } else if messwertGueltig == true {
            o["ist_c"] = Kurz.zahl(temperaturC)
            o["messwert_alter_s"] = Kurz.zahl(messwertAlterS)
            o["feuchte"] = Kurz.zahl(feuchte, stellen: 0)
            o["batterie"] = Kurz.zahl(batterie)
        } else {
            o["thermometer"] = "kein gültiger Messwert"
        }
        o["zielstellung"] = Kurz.zahl(zielstellung, stellen: 2)
        o["kanaele"] = kanaele.map { .liste($0.map { .zahl(Double($0)) }) }
        return o.wert
    }
}

extension Verteilerzustand.Kanal {
    var kurzfassung: JSONWert {
        var o = Kurz()
        o["nr"] = Kurz.zahl(id)
        o["stellung"] = Kurz.zahl(stellung, stellen: 2)
        o["handbetrieb"] = handbetrieb.map(JSONWert.bool)
        o["kalibriert"] = kalibriert.map(JSONWert.bool)
        o["stellung_bekannt"] = stellungBekannt.map(JSONWert.bool)
        if let v = vorgang, v != "idle" { o["vorgang"] = .text(v) }
        if belegt == true { o["messfahrt"] = true }
        return o.wert
    }
}

extension Heizgeraetezustand {
    public var kurzfassung: JSONWert {
        var o = Kurz()
        o["geraet"] = geraet?.id.map(JSONWert.text)
        o["ort"] = geraet?.ort.map(JSONWert.text)
        o["firmware"] = version.map(JSONWert.text)
        o["signal_dbm"] = Kurz.zahl(netz?.signal)
        o["fuehler"] = fuehler.map { f in
            .liste(f.filter { $0.zugeordnet != false }.map { p in
                var e = Kurz()
                e["rolle"] = p.rolle.map(JSONWert.text)
                e["c"] = Kurz.zahl(p.temperaturC)
                e["alter_s"] = Kurz.zahl(p.alterS)
                if let fehler = p.fehler, fehler > 0 { e["fehler"] = .zahl(Double(fehler)) }
                return e.wert
            })
        }
        o["fremdwerte_c"] = fremdwerte.map { w in .objekt(w.mapValues { Kurz.zahl($0) ?? .null }) }
        if let b = brenner {
            var e = Kurz()
            e["laeuft"] = b.laeuft.map(JSONWert.bool)
            e["erkannt"] = b.erkannt.map(JSONWert.bool)
            e["abgas_c"] = Kurz.zahl(b.abgasC)
            e["bezugslinie_c"] = Kurz.zahl(b.bezugslinieC)
            e["seit_min"] = Kurz.zahl(b.seitS.map { Double($0) / 60 }, stellen: 0)
            e["laufzeit_heute_min"] = Kurz.zahl(b.laufzeitHeuteS.map { Double($0) / 60 }, stellen: 0)
            e["starts_heute"] = Kurz.zahl(b.startsHeute)
            e["laufzeit_gestern_min"] = Kurz.zahl(b.laufzeitGesternS.map { Double($0) / 60 }, stellen: 0)
            e["starts_gestern"] = Kurz.zahl(b.startsGestern)
            e["oel_heute_l_geschaetzt"] = Kurz.zahl(b.literHeute, stellen: 2)
            if b.taktet == true { e["taktet"] = true }
            o["brenner"] = e.wert
        }
        if let l = ladung {
            var e = Kurz()
            e["phase"] = l.phase.map(JSONWert.text)
            e["fuellstand"] = Kurz.zahl(l.fuellstand, stellen: 2)
            e["spreizung_k"] = Kurz.zahl(l.spreizungK)
            if l.warmwasserWarnung == true { e["warmwasser_warnung"] = true }
            if l.begrenzt == true { e["begrenzt"] = true }
            e["kalibrierung_hoechstwert_c"] = Kurz.zahl(l.kalibrierung?.hoechstwertC)
            o["ladung"] = e.wert
        }
        o["heizkreise"] = heizkreise.map { h in
            .liste(h.map { k in
                var e = Kurz()
                e["id"] = Kurz.zahl(k.id)
                e["name"] = k.name.map(JSONWert.text)
                e["betrieb"] = k.betriebsart.map(JSONWert.text)
                e["pumpe_ein"] = k.pumpeEin.map(JSONWert.bool)
                e["grund"] = k.grund.map(JSONWert.text)
                e["bedarf"] = k.bedarf.map(JSONWert.bool)
                e["vorlauf_c"] = Kurz.zahl(k.vorlaufC)
                e["ruecklauf_c"] = Kurz.zahl(k.ruecklaufC)
                e["relais_erreichbar"] = k.relais?.erreichbar.map(JSONWert.bool)
                if k.relais?.abweichung == true { e["relais_abweichung"] = true }
                return e.wert
            })
        }
        if let p = kesselkreispumpe, p.aktiv == true {
            var e = Kurz()
            e["betrieb"] = p.betriebsart.map(JSONWert.text)
            e["ein"] = p.ein.map(JSONWert.bool)
            e["grund"] = p.grund.map(JSONWert.text)
            e["relais_erreichbar"] = p.relais?.erreichbar.map(JSONWert.bool)
            o["kesselkreispumpe"] = e.wert
        }
        o["befunde"] = befunde.map { b in
            .liste(b.map { f in
                var e = Kurz()
                e["code"] = f.code.map(JSONWert.text)
                e["ort"] = f.ort.map(JSONWert.text)
                e["text"] = f.text.map(JSONWert.text)
                e["ereignisse"] = Kurz.zahl(f.ereignisse)
                e["anstieg_k"] = Kurz.zahl(f.anstiegK)
                return e.wert
            })
        }
        if let a = aussen {
            o["aussen"] = ["c": Kurz.zahl(a.temperaturC) ?? .null, "quelle": a.quelle.map(JSONWert.text) ?? .null]
        }
        return o.wert
    }
}

/// Objekt, in dem fehlende Werte gar nicht erst erscheinen.
private struct Kurz {
    private var felder: [String: JSONWert] = [:]

    subscript(schluessel: String) -> JSONWert? {
        get { felder[schluessel] }
        set { if let newValue, newValue != .null { felder[schluessel] = newValue } }
    }

    var wert: JSONWert { .objekt(felder) }

    static func zahl(_ wert: Double?, stellen: Int = 1) -> JSONWert? {
        guard let wert, wert.isFinite else { return nil }
        let faktor = pow(10, Double(stellen))
        return .zahl((wert * faktor).rounded() / faktor)
    }

    static func zahl(_ wert: Int?) -> JSONWert? {
        wert.map { .zahl(Double($0)) }
    }
}
