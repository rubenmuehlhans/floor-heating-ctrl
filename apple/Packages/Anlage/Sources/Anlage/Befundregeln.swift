import Foundation
import Geraeteschnittstelle

/// Die Prüfungen der App. Sie sehen, was kein einzelnes Gerät sieht, und übernehmen die Befunde
/// der Heizungsgeräte. Alle Regeln prüfen nur den jetzigen Stand; wie lange ein Befund schon
/// ansteht, weiß das Befundgedächtnis. Eine `mindestdauer` verlangt, dass ein Befund so lange
/// ununterbrochen ansteht, bevor er erscheint.
extension Zusammenfuehrung {
    /// Letzte Sicherung je Gerät; `nil`, wenn die App nicht sichern kann.
    static func befunde(_ staende: [Geraetestand], bild: Anlagenbild, jetzt: Date,
                        sicherungen: [String: Date]? = nil) -> [Befund] {
        var liste: [Befund] = []

        // Nur Geräte, deren Abfrage fehlschlug: Bis zur ersten Antwort nach dem Start gilt ein
        // Gerät ebenfalls als nicht erreichbar, ohne ausgefallen zu sein.
        for s in staende where !s.erreichbar && s.fehler != nil {
            let seit = s.letzterKontakt.map { " Die letzte Antwort liegt \(Befundtext.dauer(Int(jetzt.timeIntervalSince($0)))) zurück." } ?? ""
            liste.append(Befund(
                id: "offline:\(s.geraet.id)", schwere: .stoerung, titel: "\(s.geraet.bezeichnung) nicht erreichbar",
                text: "Das Gerät unter \(s.geraet.adresse.host() ?? "") antwortet nicht.\(seit) Solange zeigt die App seine letzten Werte nicht an.",
                ort: s.ort, quelle: "Prüfung der App"))
        }

        // Beide Heizungsgeräte rechnen Verbrauchslinie und Abgasauswertung aus eigenen
        // Protokollen; maßgeblich ist das Gerät mit dem Abgasfühler. Gleiche Meldungen zweier
        // Geräte erscheinen einmal.
        let heizung = staende.filter { $0.geraet.art == .heizung }
        let protokollgeraet = kesselgeraet(heizung) ?? speichergeraet(heizung)
        let speicher = speichergeraet(heizung)
        // Meldet zuerst, wer den Befund sieht: Heizkreise führt das Speichergerät, Abgas und
        // Protokolle das Kesselgerät. So nennt der Befund das Gerät, an dem die Fühler hängen.
        let reihenfolge = staende.filter(\.erreichbar).sorted { a, b in
            (a.geraet.id == speicher?.geraet.id ? 0 : 1) < (b.geraet.id == speicher?.geraet.id ? 0 : 1)
        }
        var gesehen: Set<String> = []
        for s in reihenfolge {
            for (i, f) in (s.heizgeraet?.befunde ?? []).enumerated() {
                if ["day_above_trend", "flue_gap_rising"].contains(f.code ?? ""), let p = protokollgeraet, s.geraet.id != p.geraet.id {
                    continue
                }
                guard gesehen.insert("\(f.code ?? "")|\(f.ort ?? "")").inserted else { continue }
                liste.append(geraetebefund(f, geraet: s, index: i))
            }
        }

        liste += netzbefunde(staende)
        liste += raumbefunde(staende)
        liste += kanalbefunde(bild)
        liste += kreisbefunde(staende, bild: bild, speicher: speicher)
        liste += speicherbefunde(heizung, bild: bild, speicher: speicher, protokollgeraet: protokollgeraet)
        if let sicherungen {
            liste += sicherungsbefunde(staende, sicherungen: sicherungen, jetzt: jetzt)
        }
        return liste
    }

    // MARK: Netz und Firmware

    static func netzbefunde(_ staende: [Geraetestand]) -> [Befund] {
        var liste: [Befund] = []
        for s in staende where s.erreichbar {
            let name = s.geraet.bezeichnung
            if (s.verteiler?.netz?.uhrzeitGueltig ?? s.heizgeraet?.netz?.uhrzeitGueltig) == false {
                liste.append(Befund(
                    id: "uhrzeit:\(s.geraet.id)", schwere: .warnung, titel: "\(name) hat keine gültige Uhrzeit",
                    text: "Ohne Uhrzeit entfallen der tägliche Neustart und die Schutzfahrt, und Protokolle und Verlauf des Geräts tragen keine Zeit. Meist erreicht das Gerät den Zeitserver im Internet nicht.",
                    ort: s.ort, quelle: "Prüfung der App", mindestdauer: 600))
            }
            if let rssi = s.verteiler?.netz?.signal ?? s.heizgeraet?.netz?.signal, rssi < 0, rssi <= -80 {
                liste.append(Befund(
                    id: "wlan:\(s.geraet.id)", schwere: rssi <= -87 ? .warnung : .hinweis, titel: "Schwaches WLAN an \(name)",
                    text: "Das Gerät empfängt das WLAN mit \(rssi)\u{00A0}dBm. Unterhalb von etwa −80\u{00A0}dBm gehen Antworten verloren, und das Gerät verbindet sich häufiger neu. Abhilfe schafft ein Repeater oder ein anderer Standort des Zugangspunkts.",
                    ort: s.ort, quelle: "Prüfung der App", mindestdauer: 900))
            }
        }
        for (art, schluessel, name) in [(Geraeteart.verteiler, "verteiler", "den Verteilern"), (.heizung, "heizung", "den Heizungsgeräten")] {
            let versionen = staende.filter { $0.geraet.art == art && $0.erreichbar }.compactMap { s -> (String, String)? in
                guard let v = s.verteiler?.version ?? s.heizgeraet?.version, !v.isEmpty else { return nil }
                return (s.ort, v)
            }
            if Set(versionen.map(\.1)).count > 1 {
                liste.append(Befund(
                    id: "firmware:\(schluessel)", schwere: .hinweis, titel: "Unterschiedliche Firmware an \(name)",
                    text: "\(versionen.map { "\($0.0) \($0.1)" }.joined(separator: ", ")). Gleiche Fassungen schließen aus, dass sich Geräte derselben Art in Schnittstelle und Verhalten unterscheiden.",
                    ort: versionen.map(\.0).joined(separator: ", "), quelle: "Prüfung der App"))
            }
        }
        return liste
    }

    // MARK: Räume und Fühler der Verteiler

    static func raumbefunde(_ staende: [Geraetestand]) -> [Befund] {
        var liste: [Befund] = []
        for s in staende where s.erreichbar && s.geraet.art == .verteiler {
            guard let z = s.verteiler else { continue }
            let etage = s.ort
            for r in z.raeume ?? [] {
                guard let n = r.id else { continue }
                let name = r.name.flatMap { $0.isEmpty ? nil : $0 } ?? "Raum \(n)"
                let heizt = r.betriebsart != "off"
                if heizt, r.thermometerZugeordnet == false {
                    liste.append(Befund(
                        id: "thermometer:\(s.geraet.id)/\(n)", schwere: .warnung, titel: "\(name) hat kein Thermometer",
                        text: "Ohne Messwert setzt der Verteiler die Regelung des Raums aus; die Ventile bleiben in ihrer letzten Stellung. Ein Thermometer ordnen Sie unter Geräte › \(etage) › Räume und Kanäle zu.",
                        ort: "\(name), \(etage)", quelle: "Prüfung der App"))
                } else if heizt, r.thermometerZugeordnet == true, r.messwertGueltig == false {
                    let alter = r.messwertAlterS ?? 0
                    let seit = alter > 0 ? "Das Thermometer hat sich seit \(Befundtext.dauer(alter)) nicht gemeldet." : "Vom Thermometer ist noch kein Messwert eingegangen."
                    liste.append(Befund(
                        id: "messwert:\(s.geraet.id)/\(n)", schwere: .warnung, titel: "Messwert von \(name) veraltet",
                        text: "\(seit) Bis wieder ein Messwert kommt, setzt der Verteiler die Regelung aus; die Ventile bleiben in ihrer letzten Stellung. Häufige Ursachen sind eine leere Batterie und eine zu große Entfernung zum Verteiler.",
                        ort: "\(name), \(etage)", quelle: "Prüfung der App", mindestdauer: 300))
                }
                if let b = r.batterie, r.thermometerZugeordnet == true, b <= 15 {
                    liste.append(Befund(
                        id: "batterie:\(s.geraet.id)/\(n)", schwere: b <= 5 ? .warnung : .hinweis,
                        titel: "Batterie des Thermometers in \(name) fast leer",
                        text: "Noch \(b)\u{00A0}%. Ist sie leer, meldet das Thermometer keine Werte mehr, und der Verteiler setzt die Regelung des Raums aus.",
                        ort: "\(name), \(etage)", quelle: "Prüfung der App"))
                }
            }
            if let a = z.aussen, a.zugeordnet == true {
                if a.gueltig == false {
                    liste.append(Befund(
                        id: "aussen:\(s.geraet.id)", schwere: .hinweis, titel: "Außenfühler meldet sich nicht",
                        text: "Der Außenfühler am Verteiler \(etage) sendet keine Werte. Ohne Außentemperatur fehlen den Heizungsgeräten die Heizgradtage für die Verbrauchslinie.",
                        ort: etage, quelle: "Prüfung der App", mindestdauer: 3600))
                }
                if let mv = a.batterieMv, mv > 0, mv < 2400 {
                    liste.append(Befund(
                        id: "batterie-aussen:\(s.geraet.id)", schwere: .hinweis, titel: "Batterie des Außenfühlers schwach",
                        text: "\(Befundtext.zahl(Double(mv) / 1000, stellen: 2))\u{00A0}V. Unterhalb von etwa 2,3\u{00A0}V sendet der Fühler unzuverlässig.",
                        ort: etage, quelle: "Prüfung der App"))
                }
            }
            if z.bordfuehler?.klima?.gueltig == false {
                liste.append(Befund(
                    id: "bordfuehler:\(s.geraet.id)", schwere: .hinweis, titel: "Klimafühler der Platine \(etage) ohne Messwert",
                    text: "Der Fühler für Temperatur und Luftfeuchte im Gehäuse des Verteilers liefert keinen gültigen Wert.",
                    ort: etage, quelle: "Prüfung der App", mindestdauer: 600))
            }
            let einwire = z.bordfuehler?.einwire ?? []
            let ungueltig = einwire.filter { $0.gueltig == false }
            if !ungueltig.isEmpty {
                liste.append(Befund(
                    id: "vorlauffuehler:\(s.geraet.id)", schwere: .hinweis, titel: "Vorlauffühler am Verteiler \(etage) ohne Messwert",
                    text: "\(ungueltig.count) von \(einwire.count) Fühlern am 1-Wire-Anschluss der Platine liefern keinen gültigen Wert.",
                    ort: etage, quelle: "Prüfung der App", mindestdauer: 600))
            }
        }
        return liste
    }

    // MARK: Kanäle

    static func kanalbefunde(_ bild: Anlagenbild) -> [Befund] {
        var liste: [Befund] = []
        for etage in bild.etagen {
            let ohne = etage.belegteKanaele.filter { !$0.kalibriert }
            if !ohne.isEmpty {
                liste.append(Befund(
                    id: "kalibrierung:\(etage.id)", schwere: .hinweis,
                    titel: ohne.count == 1 ? "Ein belegter Kanal ohne Messfahrt" : "\(ohne.count) belegte Kanäle ohne Messfahrt",
                    text: "\(etage.name): Kanal \(ohne.map { "\($0.nummer)" }.joined(separator: ", ")). Sie fahren nach den Vorgabezeiten statt nach gemessenen Fahrzeiten; die geschätzte Stellung weicht deshalb ab. Eine Messfahrt je Kanal starten Sie unter Geräte › \(etage.name) › Messfahrt.",
                    ort: etage.name, quelle: "Prüfung der App"))
            }
            for k in etage.kanaele where k.handbetrieb {
                liste.append(Befund(
                    id: "handbetrieb:\(etage.id)/\(k.nummer)", schwere: .hinweis,
                    titel: "Kanal \(k.nummer)\(k.raum.map { " (\($0))" } ?? "") seit langem im Handbetrieb",
                    text: "Der Kanal folgt der Regelung nicht, bis er zurückgegeben wird: Geräte › \(etage.name) › Kanal \(k.nummer) › Wieder an die Regelung übergeben.",
                    ort: etage.name, quelle: "Prüfung der App", mindestdauer: 12 * 3600))
            }
        }
        return liste
    }

    // MARK: Heizkreise und Relais

    static func kreisbefunde(_ staende: [Geraetestand], bild: Anlagenbild, speicher: Geraetestand?) -> [Befund] {
        var liste: [Befund] = []
        // Ein stromloses Relais ist keine Störung: Die vorgeschaltete Regelung hat den Pumpenausgang
        // abgeschaltet, an dem es hängt.
        let gestoert = bild.heizkreise.filter { $0.relais.gestoert && $0.relais.weg != "kein Weg" }.map { ($0.name, $0.relais.adresse) }
            + (bild.kesselkreispumpe.flatMap { $0.relais.gestoert ? [($0.name, $0.relais.adresse)] : nil } ?? [])
        if !gestoert.isEmpty {
            let adressen = gestoert.map(\.1).filter { !$0.isEmpty }.joined(separator: ", ")
            liste.append(Befund(
                id: "relais", schwere: .stoerung,
                titel: gestoert.count == 1 ? "Relais \(gestoert[0].0) nicht erreichbar" : "Relais der Pumpen nicht erreichbar",
                text: "Die Relais\(adressen.isEmpty ? "" : " unter \(adressen)") antworten nicht. Die Pumpen folgen der Steuerung deshalb nicht. Bleibt das Lebenszeichen aus, schaltet die Ausfallregel im Relais die Pumpe nach 15 Minuten ein, sofern sie eingerichtet ist.",
                ort: gestoert.map(\.0).joined(separator: ", "), quelle: "Heizungsgeräte", mindestdauer: 600))
        }

        for kreis in bild.heizkreise where kreis.gesperrt {
            liste.append(Befund(
                id: "kreis-gesperrt:\(kreis.nummer)", schwere: .hinweis,
                titel: "\(kreis.name): Wärmebedarf, aber die Kesselregelung gibt die Pumpe nicht frei",
                text: "Die Räume fordern Wärme an, das Relais der Pumpe ist aber ohne Strom. Die vorgeschaltete Regelung am Kessel hat ihren Pumpenausgang abgeschaltet, etwa in der Nachtabsenkung. Soll diese Steuerung über die Pumpe entscheiden, stellen Sie den Ausgang der Kesselregelung auf Dauerbetrieb.",
                ort: kreis.name, quelle: "Prüfung der App", mindestdauer: 3600))
        }

        // Die versorgten Verteiler stehen in der Konfiguration; bis sie gelesen ist, wären alle
        // Kreise ohne Verteiler.
        let konfigurationBekannt = speicher == nil || speicher?.konfiguration != nil
        for kreis in bild.heizkreise where konfigurationBekannt && kreis.versorgteVerteiler.isEmpty {
            liste.append(Befund(
                id: "kreis-ohne-verteiler:\(kreis.nummer)", schwere: .warnung, titel: "\(kreis.name) versorgt keinen Verteiler",
                text: "Dem Heizkreis ist kein Verteiler zugeordnet. Seine Pumpe läuft deshalb nie auf Bedarf, nur auf „Ein“ oder zum Frostschutz.",
                ort: kreis.name, quelle: "Prüfung der App"))
        }

        guard let s = speicher, s.erreichbar else { return liste }
        let konfig = s.konfiguration?["circuits"]?.alsListe ?? []
        for c in s.heizgeraet?.heizkreise ?? [] {
            guard let id = c.id else { continue }
            let name = c.name.flatMap { $0.isEmpty ? nil : $0 } ?? "Heizkreis \(id)"
            let peers = konfig.first { $0["id"]?.alsGanzzahl == id }?["peers"]?.alsListe ?? []
            if let kreis = bild.heizkreise.first(where: { $0.nummer == id }), kreis.pumpeLaeuft,
               let b = durchflussbefund(kreis, verteiler: peers.compactMap(\.alsText), staende: staende, bild: bild, quelle: s.ort) {
                liste.append(b)
            }
            if c.relais?.abweichung == true {
                liste.append(Befund(
                    id: "relais-abweichung:\(id)", schwere: .warnung, titel: "Pumpe \(name) folgt dem Schaltbefehl nicht",
                    text: "Das Relais meldet einen anderen Schaltzustand, als das Heizungsgerät verlangt. Mögliche Ursachen: Das Relais wurde von Hand geschaltet, eine Regel im Relais greift, oder der Befehl ging verloren.",
                    ort: name, quelle: s.ort, mindestdauer: 300))
            }
            if c.abnehmerGesehen == false, !peers.isEmpty {
                liste.append(Befund(
                    id: "kreis-ohne-bedarf:\(id)", schwere: .warnung, titel: "\(name) erreicht keinen seiner Verteiler",
                    text: "Das Heizungsgerät hat keinen der versorgten Verteiler erreicht. Die Pumpe läuft deshalb nur auf „Ein“ oder zum Frostschutz.",
                    ort: name, quelle: s.ort, mindestdauer: 300))
            } else if c.veraltet == true {
                liste.append(Befund(
                    id: "kreis-bedarf-veraltet:\(id)", schwere: .warnung, titel: "\(name): ein Verteiler antwortet nicht",
                    text: "Mindestens ein versorgter Verteiler antwortet dem Heizungsgerät nicht. Das liegt meist an einer Störung im WLAN oder am Verteiler selbst.",
                    ort: name, quelle: s.ort, mindestdauer: 300))
            }
        }
        if let p = s.heizgeraet?.kesselkreispumpe, p.aktiv == true, p.relais?.abweichung == true {
            liste.append(Befund(
                id: "relais-abweichung:kkp", schwere: .warnung, titel: "Kesselkreispumpe folgt dem Schaltbefehl nicht",
                text: "Das Relais meldet einen anderen Schaltzustand, als das Heizungsgerät verlangt.",
                ort: "Kesselkreispumpe", quelle: s.ort, mindestdauer: 300))
        }

        // Verteiler, deren Bedarf keine Pumpe schaltet. Nur mit gelesener Konfiguration: Sonst
        // wären alle Verteiler ohne Kreis. Ein Verteiler ohne Räume meldet nie Bedarf -- etwa
        // eine Platine, die nur ein Funkthermometer empfängt --, und braucht deshalb keinen.
        if s.konfiguration != nil, !konfig.isEmpty {
            let versorgt = Set(konfig.flatMap { ($0["peers"]?.alsListe ?? []).compactMap(\.alsText) })
            for etage in bild.etagen where !versorgt.contains(etage.id) && !etage.raeume.isEmpty {
                liste.append(Befund(
                    id: "verteiler-ohne-kreis:\(etage.id)", schwere: .warnung, titel: "\(etage.name) ist keinem Heizkreis zugeordnet",
                    text: "Meldet der Verteiler Wärmebedarf, schaltet dadurch keine Pumpe. Die Zuordnung steht unter Heizung › Heizkreis › Versorgte Verteiler.",
                    ort: etage.name, quelle: "Prüfung der App"))
            }
        }
        return liste
    }

    // MARK: Speicher und Brenner

    static func speicherbefunde(_ heizung: [Geraetestand], bild: Anlagenbild, speicher: Geraetestand?,
                                protokollgeraet: Geraetestand?) -> [Befund] {
        var liste: [Befund] = []
        let konfigurierte = heizung.filter { $0.konfiguration != nil }
        if konfigurierte.count >= 2 {
            for (schluessel, name) in [("voll_c", "Voll"), ("leer_c", "Leer")] {
                let werte = konfigurierte.compactMap { $0.konfiguration?["buffer"]?[schluessel]?.alsZahl }
                if let min = werte.min(), let max = werte.max(), max - min >= 0.5 {
                    liste.append(Befund(
                        id: "speichergrenze:\(schluessel)", schwere: .hinweis, titel: "Speichergrenze „\(name)“ weicht ab",
                        text: "Die Heizungsgeräte schätzen den Füllstand desselben Speichers mit unterschiedlichen Grenzen: \(konfigurierte.map { "\($0.ort) \(Befundtext.zahl($0.konfiguration?["buffer"]?[schluessel]?.alsZahl ?? 0)) °C" }.joined(separator: ", ")).",
                        ort: "Pufferspeicher", quelle: "Prüfung der App"))
                }
            }
        }

        let staende = heizung.filter(\.erreichbar).compactMap { s in s.heizgeraet?.ladung?.fuellstand.map { (s.ort, $0) } }
        if staende.count >= 2, let min = staende.map(\.1).min(), let max = staende.map(\.1).max(), max - min >= 0.15 {
            liste.append(Befund(
                id: "fuellstand-abweichung", schwere: .hinweis, titel: "Füllstand weicht zwischen den Heizungsgeräten ab",
                text: "\(staende.map { "\($0.0) \(Befundtext.zahl($0.1 * 100, stellen: 0))\u{00A0}%" }.joined(separator: ", ")). Beide Geräte bewerten denselben Speicher; meist weichen die Grenzen „voll“ und „leer“ voneinander ab.",
                ort: "Pufferspeicher", quelle: "Prüfung der App", mindestdauer: 900))
        }

        if let s = speicher, s.erreichbar {
            let voll = s.konfiguration?["buffer"]?["voll_c"]?.alsZahl
            let enden = bild.ladungen.sorted { $0.beginn > $1.beginn }.prefix(10).map(\.speicherNachher).filter { $0 > 0 }
            if let voll, enden.count >= 3, enden.filter({ $0 >= voll + 3 }).count >= 3,
               let min = enden.min(), let max = enden.max() {
                liste.append(Befund(
                    id: "voll-unter-ladungsende", schwere: .hinweis, titel: "Der Speicher gilt früher als voll, als er es ist",
                    text: "„Voll“ ist auf \(Befundtext.zahl(voll)) °C eingestellt; die letzten \(enden.count) Ladungen endeten bei \(Befundtext.zahl(min)) bis \(Befundtext.zahl(max)) °C. Der Füllstand steht dadurch lange auf 100\u{00A0}%, obwohl noch Wärme hineinpasst. Die Grenze stellen Sie unter Heizung › Pufferspeicher › Speichereinstellungen ein.",
                    ort: "Pufferspeicher", quelle: "Prüfung der App"))
            }
            if let l = s.heizgeraet?.ladung {
                if l.warmwasserWarnung == true {
                    let warn = s.konfiguration?["buffer"]?["warn_c"]?.alsZahl
                    liste.append(Befund(
                        id: "warmwasser", schwere: .warnung, titel: "Warmwasserreserve knapp",
                        text: "Der Pufferspeicher liegt unter der Warngrenze\(warn.map { " von \(Befundtext.zahl($0)) °C" } ?? "").",
                        ort: "Pufferspeicher", quelle: s.ort))
                }
                if l.begrenzt == true {
                    liste.append(Befund(
                        id: "ladung-geschaetzt", schwere: .hinweis, titel: "Ladephase nur geschätzt",
                        text: "Dem Gerät fehlen die Werte von Kesselvor- und -rücklauf. Beginn und Ende einer Ladung erkennt es deshalb nur aus der Speichertemperatur.",
                        ort: "Pufferspeicher", quelle: s.ort, mindestdauer: 1800))
                }
            }
        }

        if let p = protokollgeraet, p.erreichbar, let b = p.heizgeraet?.brenner, b.taktet == true {
            liste.append(Befund(
                id: "taktung", schwere: .hinweis, titel: "Der Brenner startet auffällig oft",
                text: "Heute \(b.startsHeute ?? 0) Starts bei \(Befundtext.dauer(b.laufzeitHeuteS ?? 0)) Laufzeit. Kurze Brennerläufe haben einen schlechteren Wirkungsgrad als lange und belasten Zündung und Düse.",
                ort: "Brenner", quelle: p.ort))
        }
        return liste
    }

    // MARK: Sicherungen

    static func sicherungsbefunde(_ staende: [Geraetestand], sicherungen: [String: Date], jetzt: Date) -> [Befund] {
        var liste: [Befund] = []
        for s in staende {
            let name = s.geraet.bezeichnung
            if let d = sicherungen[s.geraet.id] {
                let tage = Int(jetzt.timeIntervalSince(d) / 86_400)
                if tage > 14 {
                    liste.append(Befund(
                        id: "sicherung:\(s.geraet.id)", schwere: .hinweis, titel: "Letzte Sicherung von \(name) vor \(tage) Tagen",
                        text: "Die App sichert jedes Gerät vor Änderungen und einmal in der Woche, sobald es erreichbar ist. Eine Sicherung legen Sie unter Geräte › \(s.ort) › Sicherung an.",
                        ort: s.ort, quelle: "Prüfung der App"))
                }
            } else {
                liste.append(Befund(
                    id: "sicherung:\(s.geraet.id)", schwere: .hinweis, titel: "Keine Sicherung von \(name)",
                    text: "Für dieses Gerät liegt in der App noch keine Sicherung vor. Die App sichert jedes Gerät vor Änderungen und einmal in der Woche, sobald es erreichbar ist.",
                    ort: s.ort, quelle: "Prüfung der App", mindestdauer: 3600))
            }
        }
        return liste
    }

    /// Titel eines Befunds der Firmware nach seiner Kennung; nil bei unbekannter Kennung.
    /// Auch für die Ereignisse `befund` im Protokoll des Leitstands.
    public static func geraetebefundTitel(_ code: String) -> String? {
        switch code {
        case "backflow": "Warmes Wasser strömt in den Kesselrücklauf, obwohl nichts läuft"
        case "flow_swapped": "Vorlauf und Rücklauf sind vermutlich vertauscht"
        case "probe_errors": "Ein Fühler verwirft auffällig viele Messungen"
        case "day_above_trend": "Der letzte Tag verbrauchte mehr, als zur Außenlage passt"
        case "flue_gap_rising": "Der Kessel überträgt schlechter als nach der letzten Reinigung"
        default: nil
        }
    }

    /// Titel und Texte wie in der Weboberfläche; der Text der Firmware ist ASCII und dient nur
    /// für unbekannte Kennungen.
    static func geraetebefund(_ f: Heizgeraetezustand.Befund, geraet s: Geraetestand, index: Int) -> Befund {
        let ort = f.ort.flatMap { $0.isEmpty ? nil : $0 } ?? s.ort
        // Nach Art und Ort, nicht nach Gerät oder Listenplatz: Meldet ihn einmal das andere
        // Gerät oder steht er an anderer Stelle der Liste, bleibt es derselbe Befund.
        let id = "geraet:\(f.code ?? "befund"):\(f.ort.flatMap { $0.isEmpty ? nil : $0 } ?? "\(index)")"
        let z = { (wert: Double?) in wert.map { Befundtext.zahl($0) } ?? "–" }
        let fest = "\u{00A0}"
        switch f.code {
        case "backflow":
            return Befund(
                id: id, schwere: .warnung, titel: geraetebefundTitel("backflow")!,
                text: "\(f.ereignisse.map { "\($0)-mal" } ?? "Wiederholt") beobachtet, zuletzt \(z(f.anstiegK))\(fest)K Anstieg bei ausgeschaltetem Brenner und stehender Pumpe. Das ist ohne Strömung nicht möglich, meist fehlt eine Schwerkraftbremse im Kesselrücklauf oder sie ist undicht. Jede Warmwasserzapfung schiebt so Wärme in den kalten Kessel, wo sie durch den Schornstein verloren geht.",
                ort: ort, quelle: s.ort)
        case "flow_swapped":
            return Befund(
                id: id, schwere: .warnung, titel: geraetebefundTitel("flow_swapped")!,
                text: "Seit \(f.gehaltenS.map { Befundtext.dauer($0) } ?? "längerer Zeit") durchgehend kälter im Vorlauf, obwohl die Pumpe läuft und der Speicher warm ist. Entweder sitzen die Fühler an den falschen Rohren, oder ihre Rollen sind vertauscht zugeordnet.",
                ort: ort, quelle: s.ort)
        case "probe_errors":
            let gesamt = (f.messungen ?? 0) + (f.fehler ?? 0)
            return Befund(
                id: id, schwere: .warnung, titel: geraetebefundTitel("probe_errors")!,
                text: "\(f.fehler ?? 0) von \(gesamt) Messungen verworfen, meist ein Wackelkontakt, eine zu lange Leitung oder ein zu schwacher Anschlusswiderstand.",
                ort: ort, quelle: s.ort)
        case "day_above_trend":
            return Befund(
                id: id, schwere: .hinweis, titel: geraetebefundTitel("day_above_trend")!,
                text: "\(z(f.stunden))\(fest)h Brennerlauf statt der erwarteten \(z(f.erwartetH))\(fest)h, das sind \(z(f.streuungenAbstand)) Streuungen über der Verbrauchslinie. So fällt ein offenes Fenster auf, ein klemmendes Ventil oder eine Pumpe, die durchläuft.",
                ort: ort, quelle: s.ort)
        case "flue_gap_rising":
            let prozent = f.deltaK.map { Befundtext.zahl($0 / 20) } ?? "–"
            return Befund(
                id: id, schwere: .hinweis, titel: geraetebefundTitel("flue_gap_rising")!,
                text: "\(z(f.jetztK))\(fest)K Abstand zwischen Abgas und Kesselvorlauf statt \(z(f.bezugK))\(fest)K, also \(z(f.deltaK))\(fest)K mehr. Das sind grob \(prozent) Prozentpunkte Wirkungsgrad, meist Ruß im Wärmetauscher.",
                ort: ort, quelle: s.ort)
        default:
            return Befund(id: id, schwere: .hinweis, titel: f.text ?? f.code ?? "Meldung des Geräts",
                          text: f.text ?? "", ort: ort, quelle: s.ort)
        }
    }
}

/// Zahlen und Dauern in Befundtexten, deutsch und mit geschütztem Leerzeichen vor der Einheit.
extension Zusammenfuehrung {
    /// So weit muss der Vorlauf am Heizungsgerät über dem wärmsten Fühler am Verteiler liegen, damit
    /// dort als nicht angekommen gilt, was abgeht.
    static let durchflussVerteilerK = 10.0
    /// Ohne Fühler am Verteiler: Rücklauf so weit über den Räumen, bei höchstens so viel Spreizung.
    /// Am 9. Oktober stand Heizkreis 2 mit Luft in der Leitung bei 40,8 °C Vorlauf, 35,7 °C Rücklauf
    /// und 19,5 °C in den Räumen: 16 K über den Räumen, 5,1 K Spreizung. Mit Durchfluss lag der
    /// Rücklauf am 5. Oktober 6 bis 8 K über den Räumen.
    static let durchflussRuecklaufK = 14.0
    static let durchflussSpreizungK = 6.0

    /// Die Pumpe läuft, aber im Kreis fließt kaum Wasser -- meist Luft in der Leitung, weil der
    /// Anlagendruck nicht bis in das obere Geschoss reicht.
    static func durchflussbefund(_ kreis: Heizkreis, verteiler: [String], staende: [Geraetestand], bild: Anlagenbild,
                                 quelle: String) -> Befund? {
        guard let vl = kreis.vorlauf, let rl = kreis.ruecklauf else { return nil }
        let raeume = bild.etagen.filter { verteiler.contains($0.id) }
            .flatMap(\.raeume).filter { $0.betriebsart == .heizen }.compactMap(\.ist)
        guard !raeume.isEmpty else { return nil }
        let raum = raeume.reduce(0, +) / Double(raeume.count)
        // Ohne Wärme im Vorlauf gibt es nichts, was ankommen müsste.
        guard vl - raum >= 10 else { return nil }

        let id = "kreis-ohne-durchfluss:\(kreis.nummer)"
        let titel = "\(kreis.name): Die Pumpe läuft, die Wärme kommt nicht an"
        let rat = "Meist steckt Luft in der Leitung, oft weil der Anlagendruck nicht bis in das obere Geschoss reicht. Prüfen Sie das Manometer am Kessel und entlüften Sie nur bei warmer Anlage und ausreichendem Druck; sonst saugt der Entlüfter Luft an, statt sie abzulassen."
        // Der wärmste Fühler am Verteiler steht für dessen Vorlauf.
        let amVerteiler = staende.filter { verteiler.contains($0.geraet.id) }
            .flatMap { $0.verteiler?.bordfuehler?.einwire ?? [] }
            .filter { $0.gueltig == true }.compactMap(\.temperaturC).max()
        if let v = amVerteiler {
            guard vl - v >= durchflussVerteilerK else { return nil }
            return Befund(
                id: id, schwere: .warnung, titel: titel,
                text: "Am Heizungsgerät hat der Vorlauf \(Befundtext.zahl(vl)) °C, am Verteiler kommen nur \(Befundtext.zahl(v)) °C an. Durch den Kreis fließt kaum Wasser. \(rat)",
                ort: kreis.name, quelle: quelle, mindestdauer: 1800)
        }
        guard rl - raum >= durchflussRuecklaufK, vl - rl <= durchflussSpreizungK else { return nil }
        return Befund(
            id: id, schwere: .hinweis, titel: titel,
            text: "Der Rücklauf kommt mit \(Befundtext.zahl(rl)) °C zurück, \(Befundtext.zahl(rl - raum, stellen: 0)) K über den Räumen. Fließt das Wasser durch die Fußbodenschleifen, kühlt es dort deutlich stärker ab; vermutlich fließt kaum etwas. \(rat) Sicher erkennen lässt sich das mit einem Fühler am Vorlauf des Verteilers.",
            ort: kreis.name, quelle: quelle, mindestdauer: 1800)
    }
}

enum Befundtext {
    static let deutsch = Locale(identifier: "de_DE")

    static func zahl(_ wert: Double, stellen: Int = 1) -> String {
        wert.formatted(.number.precision(.fractionLength(stellen)).locale(deutsch))
    }

    static func dauer(_ sekunden: Int) -> String {
        let fest = "\u{00A0}"
        if sekunden < 60 { return "\(sekunden)\(fest)s" }
        let minuten = sekunden / 60
        if minuten < 60 { return "\(minuten)\(fest)min" }
        let stunden = minuten / 60
        if stunden < 48 { return "\(stunden)\(fest)h" }
        return "\(stunden / 24) Tagen"
    }
}
