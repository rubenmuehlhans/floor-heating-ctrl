import Foundation
import Geraeteschnittstelle

/// Macht aus den Ständen der einzelnen Geräte ein Bild der ganzen Anlage.
///
/// Welches Heizungsgerät wofür maßgeblich ist, folgt aus den Kennzeichen der Firmware und nicht
/// aus der Reihenfolge: Brenner, Protokolle und Verbrauchslinie vom Gerät mit eigenem
/// Abgasfühler (`burner.remote == false`), die Ladung vom Gerät mit eigenem Pufferfühler
/// (`charge.puffer_remote == false`), Heizkreise und Kesselkreispumpe von dem Gerät, bei dem sie
/// eingerichtet sind. Beide Geräte rechnen Brenner und Ladung mit, das jeweils andere aber nur
/// aus Fremdwerten.
public enum Zusammenfuehrung {
    /// `sicherungen`: letzte Sicherung je Gerät, für den Befund einer fehlenden oder alten Sicherung.
    /// `verlauf`: der schon umgerechnete Verlauf der Heizungsgeräte; er ändert sich nur, wenn ein
    /// Gerät ihn neu liefert, und ist der teuerste Teil des Bildes.
    public static func bild(_ staende: [Geraetestand], jetzt: Date = .now, sicherungen: [String: Date]? = nil,
                            verlauf vorhanden: Kurzverlauf? = nil) -> Anlagenbild {
        let verteiler = staende.filter { $0.geraet.art == .verteiler }
            .sorted { (etagenrang($0.ort), $0.ort) < (etagenrang($1.ort), $1.ort) }
        let heizung = staende.filter { $0.geraet.art == .heizung }
        let kessel = kesselgeraet(heizung)
        let speicher = speichergeraet(heizung)
        let fuehler = fuehlerwerte(heizung)
        let orte = Dictionary(staende.map { ($0.geraet.id, $0.ort) }, uniquingKeysWith: { a, _ in a })
        let bedarf = bedarfJeVerteiler(heizung)

        let etagen = verteiler.map { etage($0, bedarf: bedarf[$0.geraet.id] ?? false) }
        // Heizkreise erlaubt die Firmware nur mit eigenem Pufferfühler, also am Speicher-Gerät.
        let kreise = speicher.map { heizkreise($0, fuehler: fuehler, orte: orte) } ?? []
        let pumpe = heizung.lazy.compactMap { kesselkreispumpe($0) }.first
        let protokollgeraet = kessel ?? speicher

        var bild = Anlagenbild(
            stand: staende.compactMap(\.letzterKontakt).max() ?? jetzt,
            aussen: aussenwerte(verteiler: verteiler, heizung: heizung),
            etagen: etagen,
            kessel: kessel.map { kesselbild($0, fuehler: fuehler) },
            speicher: speicher.map { speicherbild($0, rueckstroemung: kessel ?? $0, fuehler: fuehler) },
            heizkreise: kreise,
            kesselkreispumpe: pumpe,
            befunde: [],
            geraete: geraete(verteiler: verteiler, heizung: heizung, kessel: kessel, speicher: speicher),
            tage: protokollgeraet.map(tagessaetze) ?? [],
            ladungen: protokollgeraet.map(ladungssaetze) ?? [],
            verlauf: vorhanden ?? verlauf(heizung, jetzt: jetzt),
            verbrauchslinie: protokollgeraet.map(verbrauchslinie)
        )
        bild.befunde = befunde(staende, bild: bild, jetzt: jetzt, sicherungen: sicherungen)
        return bild
    }

    // MARK: - Rollen der Heizungsgeräte

    static func kesselgeraet(_ heizung: [Geraetestand]) -> Geraetestand? {
        heizung.first { $0.heizgeraet?.brenner?.fremdgemessen == false && $0.heizgeraet?.brenner?.erkannt != false }
            ?? heizung.first { hatRolle($0, "abgas") }
    }

    static func speichergeraet(_ heizung: [Geraetestand]) -> Geraetestand? {
        heizung.first { $0.heizgeraet?.ladung?.pufferFremd == false }
            ?? heizung.first { hatRolle($0, "puffer") }
            ?? heizung.first { !($0.heizgeraet?.heizkreise ?? []).isEmpty }
    }

    private static func hatRolle(_ stand: Geraetestand, _ rolle: String) -> Bool {
        (stand.heizgeraet?.fuehler ?? []).contains { $0.rolle == rolle && $0.zugeordnet != false }
    }

    /// Temperaturen nach Rolle: eigene Fühler zuerst, Fremdwerte nur, wo kein Gerät selbst misst.
    static func fuehlerwerte(_ heizung: [Geraetestand]) -> [String: Double] {
        var werte: [String: Double] = [:]
        for stand in heizung {
            for f in stand.heizgeraet?.fuehler ?? [] where f.zugeordnet != false {
                if let rolle = f.rolle, !rolle.isEmpty, let t = f.temperaturC {
                    werte[rolle] = t
                }
            }
        }
        for stand in heizung {
            for (rolle, wert) in stand.heizgeraet?.fremdwerte ?? [:] where werte[rolle] == nil {
                werte[rolle] = wert
            }
        }
        return werte
    }

    private static func bedarfJeVerteiler(_ heizung: [Geraetestand]) -> [String: Bool] {
        var bedarf: [String: Bool] = [:]
        for stand in heizung {
            for quelle in stand.heizgeraet?.bedarfsquellen ?? [] {
                guard let id = quelle.id else { continue }
                bedarf[id] = (bedarf[id] ?? false) || quelle.bedarf == true
            }
        }
        return bedarf
    }

    // MARK: - Verteiler

    /// Übliche Reihenfolge der Geschosse; unbekannte Namen danach, alphabetisch.
    static func etagenrang(_ name: String) -> Int {
        let n = name.lowercased()
        let reihe: [(String, Int)] = [
            ("keller", 0), ("untergeschoss", 0), ("souterrain", 0), ("erdgeschoss", 1), ("parterre", 1),
            ("obergeschoss", 2), ("1. og", 2), ("2. og", 3), ("dachgeschoss", 4), ("spitzboden", 5),
        ]
        return reihe.first { n.contains($0.0) }?.1 ?? 9
    }

    static func etage(_ stand: Geraetestand, bedarf: Bool) -> Etage {
        let id = stand.geraet.id
        let name = stand.ort
        let z = stand.verteiler
        let k = stand.konfiguration
        let konfigRaeume = k?["rooms"]?.alsListe ?? []
        let namen = thermometernamen(stand)

        var raumnummern: [Int] = (z?.raeume ?? []).compactMap(\.id)
        for r in konfigRaeume {
            if let n = r["id"]?.alsGanzzahl, !raumnummern.contains(n) { raumnummern.append(n) }
        }
        let raeume: [Raum] = raumnummern.map { nummer in
            let zr = z?.raeume?.first { $0.id == nummer }
            let kr = konfigRaeume.first { $0["id"]?.alsGanzzahl == nummer }
            let mac = kr?["sensor_mac"]?.alsText.flatMap { $0.isEmpty ? nil : $0 }
            let zugeordnet = zr?.thermometerZugeordnet ?? (mac != nil)
            let thermometer: String? = zugeordnet ? (mac.flatMap { namen[normalisiert($0)] } ?? mac ?? "zugeordnet") : nil
            let modus = zr?.betriebsart ?? kr?["mode"]?.alsText
            return Raum(
                id: "\(id)/\(nummer)",
                nummer: nummer,
                name: zr?.name ?? kr?["name"]?.alsText ?? "Raum \(nummer)",
                etage: name,
                betriebsart: modus == "off" ? .aus : .heizen,
                soll: zr?.sollC ?? kr?["target_c"]?.alsZahl ?? 20,
                ist: zr?.messwertGueltig == true ? zr?.temperaturC : nil,
                feuchte: zr?.messwertGueltig == true ? zr?.feuchte : nil,
                batterie: zr?.batterie,
                messwertAlter: zr?.messwertAlterS,
                thermometer: thermometer,
                kanaele: zr?.kanaele ?? kr?["channels"]?.alsListe?.compactMap(\.alsGanzzahl) ?? [],
                zielstellung: zr?.zielstellung ?? 0,
                naechstePruefung: zr?.naechstePruefungS ?? 0,
                regelung: Regelparameter(
                    proportionalband: kr?["p_band_k"]?.alsZahl ?? Regelparameter.vorgabe.proportionalband,
                    pruefintervall: kr?["interval_s"]?.alsGanzzahl ?? Regelparameter.vorgabe.pruefintervall,
                    raster: kr?["step"]?.alsZahl ?? Regelparameter.vorgabe.raster,
                    mindestaenderung: kr?["min_delta"]?.alsZahl ?? Regelparameter.vorgabe.mindestaenderung
                )
            )
        }

        let konfigKanaele = k?["channels"]?.alsListe ?? []
        var nummern: [Int] = (z?.kanaele ?? []).compactMap(\.id)
        for c in konfigKanaele {
            if let n = c["id"]?.alsGanzzahl, !nummern.contains(n) { nummern.append(n) }
        }
        let kanaele: [Kanal] = nummern.sorted().map { n in
            let zk = z?.kanaele?.first { $0.id == n }
            let kk = konfigKanaele.first { $0["id"]?.alsGanzzahl == n }
            let bewegung: Kanal.Bewegung = switch zk?.vorgang {
            case "opening": .oeffnet
            case "closing": .schliesst
            default: .steht
            }
            return Kanal(
                nummer: n,
                raum: raeume.first { $0.kanaele.contains(n) }?.name,
                gruppe: zk?.gruppe ?? kk?["bemf_group"]?.alsGanzzahl ?? (n + 1) / 2,
                stellung: zk?.stellung ?? 0,
                bekannt: zk?.stellungBekannt ?? false,
                kalibriert: zk?.kalibriert ?? kk?["calibrated"]?.alsBool ?? false,
                handbetrieb: zk?.handbetrieb ?? false,
                bewegung: bewegung,
                gegenspannung: Int((zk?.gegenspannungMv ?? 0).rounded()),
                fahrzeitAuf: sekunden(kk?["open_ms"]),
                fahrzeitZu: sekunden(kk?["close_ms"]),
                maximal: sekunden(kk?["max_ms"]),
                sperrzeit: sekunden(kk?["blank_ms"]),
                schwelle: kk?["bemf_mv"]?.alsGanzzahl ?? 0,
                hysterese: kk?["bemf_hyst_mv"]?.alsGanzzahl ?? 0,
                belegt: zk?.belegt ?? false,
                befehlWartet: zk?.befehlWartet ?? false
            )
        }

        let sf = z?.schutzfahrt
        let wochentag = sf?.wochentag ?? k?["seize_weekday"]?.alsGanzzahl
        return Etage(
            id: id,
            name: name,
            raeume: raeume,
            kanaele: kanaele,
            bedarf: bedarf,
            schutzfahrt: Schutzfahrt(
                wochentag: wochentag.flatMap { (0...6).contains($0) ? $0 : nil },
                stunde: sf?.stunde ?? k?["seize_hour"]?.alsGanzzahl ?? 0,
                tageBis: sf?.tageBisFaellig,
                laeuft: sf?.laeuft ?? false
            ),
            traegtAussenfuehler: z?.aussen?.zugeordnet ?? !(k?["outdoor_mac"]?.alsText ?? "").isEmpty,
            messfahrt: messfahrt(stand, raeume: raeume)
        )
    }

    private static func sekunden(_ wert: JSONWert?) -> Double {
        (wert?.alsZahl ?? 0) / 1000
    }

    public static func normalisiert(_ mac: String) -> String {
        mac.uppercased().filter { $0.isHexDigit }
    }

    /// Anzeigename je MAC aus der Liste der empfangenen Thermometer
    private static func thermometernamen(_ stand: Geraetestand) -> [String: String] {
        var namen: [String: String] = [:]
        for t in stand.thermometer {
            guard let mac = t.mac else { continue }
            namen[normalisiert(mac)] = t.name.flatMap { $0.isEmpty ? nil : $0 } ?? mac
        }
        return namen
    }

    static func messfahrt(_ stand: Geraetestand, raeume: [Raum]) -> Messfahrt? {
        guard let c = stand.verteiler?.messfahrt, let kanal = c.kanal, kanal > 0 else { return nil }
        let zustand: Messfahrt.Zustand
        switch c.zustand {
        case "running": zustand = .laeuft
        case "done": zustand = .fertig
        case "failed": zustand = .fehlgeschlagen
        default: return nil
        }
        let v = c.vorschlag
        let vorschlag = v.flatMap { v -> Kalibriervorschlag? in
            guard let zu = v.zuMs, let auf = v.aufMs, let max = v.maximalMs, let schwelle = v.schwelleMv else { return nil }
            return Kalibriervorschlag(
                fahrzeitZu: Double(zu) / 1000, fahrzeitAuf: Double(auf) / 1000, maximal: Double(max) / 1000,
                schwelle: schwelle, hysterese: v.hystereseMv ?? 0)
        }
        // Die Öffnungsfahrt beginnt am Merkpunkt `open_from`; solange er fehlt, schließt der Antrieb.
        let oeffnenAb = c.marken?.aufVon.flatMap { $0 > 0 ? $0 : nil }
        return Messfahrt(
            zustand: zustand,
            kanal: kanal,
            raum: raeume.first { $0.kanaele.contains(kanal) }?.name ?? "Kanal \(kanal)",
            abtastung: Double(c.abtastMs ?? 50) / 1000,
            werte: stand.messreihe.map { Int($0.rounded()) },
            oeffnenAb: oeffnenAb,
            vorschlag: zustand == .fertig ? vorschlag : nil,
            meldung: c.meldung.flatMap { $0.isEmpty ? nil : $0 }
        )
    }

    // MARK: - Außen

    static func aussenwerte(verteiler: [Geraetestand], heizung: [Geraetestand]) -> Aussenwerte? {
        for v in verteiler {
            guard let a = v.verteiler?.aussen, a.zugeordnet == true, a.gueltig == true, let t = a.temperaturC else { continue }
            return Aussenwerte(temperatur: t, feuchte: a.feuchte, quelle: "Außenfühler am Verteiler \(v.ort)", alter: a.alterS ?? 0)
        }
        for h in heizung {
            guard let a = h.heizgeraet?.aussen, let t = a.temperaturC else { continue }
            let quelle = a.quelle.flatMap { $0.isEmpty ? nil : "über \($0)" } ?? "Fühler am \(h.ort)"
            return Aussenwerte(temperatur: t, feuchte: nil, quelle: quelle, alter: a.alterS ?? 0)
        }
        return nil
    }

    // MARK: - Wärmeerzeugung

    static func kesselbild(_ stand: Geraetestand, fuehler: [String: Double]) -> Kessel {
        let b = stand.heizgeraet?.brenner
        let flue = stand.heizgeraet?.abgas
        let wartung = flue?.wartungEpoch ?? stand.konfiguration?["burner"]?["wartung_epoch"]?.alsGanzzahl ?? 0
        let reinigung = wartung > 0 ? Date(timeIntervalSince1970: TimeInterval(wartung)) : nil
        return Kessel(
            brennerLaeuft: b?.laeuft ?? false,
            brennerSeit: b?.seitS ?? 0,
            abgas: b?.abgasC ?? fuehler["abgas"],
            bezugslinie: b?.bezugslinieC,
            vorlauf: fuehler["kessel_vl"],
            ruecklauf: fuehler["kessel_rl"],
            laufzeitHeute: b?.laufzeitHeuteS ?? 0,
            startsHeute: b?.startsHeute ?? 0,
            literHeute: b?.literHeute ?? 0,
            laufzeitGestern: b?.laufzeitGesternS ?? 0,
            startsGestern: b?.startsGestern ?? 0,
            taktbetrieb: b?.taktet ?? false,
            duese: stand.konfiguration?["burner"]?["duese_l_h"]?.alsZahl,
            abgasAbstand: AbgasAbstand(
                ladungen: flue?.ladungen ?? 0, reinigung: reinigung, jetzt: flue?.jetztK, bezug: flue?.bezugK,
                bezugLadungen: flue?.bezugLadungen, uebersprungen: flue?.uebersprungen ?? 0)
        )
    }

    static func speicherbild(_ stand: Geraetestand, rueckstroemung: Geraetestand, fuehler: [String: Double]) -> Speicher {
        let c = stand.heizgeraet?.ladung
        let puffer = stand.konfiguration?["buffer"]
        let korrektur = stand.konfiguration?["probes"]?.alsListe?
            .first { $0["role"]?.alsText == "puffer" }?["offset_k"]?.alsZahl
        return Speicher(
            temperatur: fuehler["puffer"],
            korrektur: korrektur,
            ladung: c?.fuellstand,
            phase: c?.phase ?? "unbekannt",
            phaseSeit: c?.seitS,
            warmwasserKnapp: c?.warmwasserWarnung ?? false,
            voll: puffer?["voll_c"]?.alsZahl,
            leer: puffer?["leer_c"]?.alsZahl,
            leerGelernt: puffer?["leer_lernen"]?.alsBool == true && (puffer?["leer_epoch"]?.alsGanzzahl ?? 0) > 0,
            warngrenze: puffer?["warn_c"]?.alsZahl,
            hoechstwertLadung: c?.kalibrierung?.hoechstwertC,
            zapfungenHeute: stand.heizgeraet?.zapfung?.anzahl ?? 0,
            rueckstroemungen: rueckstroemung.heizgeraet?.rueckstroemung?.ereignisse ?? 0,
            volumen: puffer?["volumen_l"]?.alsZahl
        )
    }

    static func pumpenbetriebsart(_ modus: String?) -> Pumpenbetriebsart {
        switch modus {
        case "ein": .ein
        case "aus": .aus
        default: .automatik
        }
    }

    static func relais(konfiguration p: JSONWert?, zustand r: Heizgeraetezustand.Relais?, weg: String?) -> Relais {
        let mqtt = weg == "mqtt"
        let host = (p?["host"]?.alsText ?? "")
            .replacingOccurrences(of: "http://", with: "")
            .replacingOccurrences(of: "https://", with: "")
        let wegname = switch weg {
        case "mqtt": "MQTT"
        case "http": "HTTP"
        default: "kein Weg"
        }
        return Relais(
            adresse: mqtt ? (p?["topic"]?.alsText ?? "") : host,
            kanal: p?["relay"]?.alsGanzzahl ?? 1,
            weg: wegname,
            erreichbar: r?.erreichbar ?? false,
            ein: r?.ein ?? false
        )
    }

    static func heizkreise(_ stand: Geraetestand, fuehler: [String: Double], orte: [String: String]) -> [Heizkreis] {
        let konfig = stand.konfiguration?["circuits"]?.alsListe ?? []
        return (stand.heizgeraet?.heizkreise ?? []).compactMap { c in
            guard let id = c.id else { return nil }
            let k = konfig.first { $0["id"]?.alsGanzzahl == id }
            let vlRolle = k?["vl_role"]?.alsText ?? "hk\(id)_vl"
            let rlRolle = k?["rl_role"]?.alsText ?? "hk\(id)_rl"
            let peers = k?["peers"]?.alsListe?.compactMap(\.alsText) ?? []
            return Heizkreis(
                nummer: id,
                name: c.name ?? k?["name"]?.alsText ?? "Heizkreis \(id)",
                betriebsart: pumpenbetriebsart(c.betriebsart ?? k?["mode"]?.alsText),
                pumpeLaeuft: c.pumpeEin ?? false,
                grund: c.grund ?? "",
                bedarf: c.bedarf ?? false,
                vorlauf: c.vorlaufC ?? fuehler[vlRolle],
                ruecklauf: c.ruecklaufC ?? fuehler[rlRolle],
                // Nicht eingebundene Verteiler tragen den Namen, unter dem das Heizungsgerät sie sieht.
                versorgteVerteiler: peers.map { id in
                    orte[id] ?? stand.nachbarn.first { $0.id == id }?.ort.flatMap { $0.isEmpty ? nil : $0 } ?? id
                },
                relais: relais(konfiguration: k?["pump"], zustand: c.relais, weg: c.weg),
                nachlauf: k?["overrun_s"]?.alsGanzzahl ?? 0,
                mindestlaufzeit: k?["min_run_s"]?.alsGanzzahl ?? 0,
                mindestpause: k?["min_pause_s"]?.alsGanzzahl ?? 0,
                mindestSpeicher: k?["min_buffer_c"]?.alsZahl ?? 0,
                frostgrenze: k?["frost_c"]?.alsZahl ?? 0,
                bedarfVeraltet: c.veraltet ?? false,
                keinVerteilerErreicht: c.abnehmerGesehen == false && !peers.isEmpty,
                vorlaufRolle: vlRolle, ruecklaufRolle: rlRolle
            )
        }
    }

    /// Gründe der Kesselkreispumpe wie in der Weboberfläche; das Gerät sendet sie nur in ASCII.
    static func pumpengrund(_ schluessel: String?) -> String? {
        switch schluessel {
        case "transfer": "der Kessel gibt Wärme ab"
        case "burner": "der Brenner läuft"
        case "no_transfer": "Kessel kaum wärmer als der Speicher, Fördern brächte keine Wärme in den Speicher"
        case "emergency": "Notabfuhr, Kesselvorlauf über der Grenze"
        case "no_reading": "keine Kesselwerte, sie läuft sicherheitshalber"
        case "hold": "Haltezeit, die Bedingung hat eben erst gewechselt"
        case "min_run": "Mindestlaufzeit"
        case "min_pause": "Mindestpause"
        case "manual": "Handbetrieb"
        case "disabled": "keine Kesselkreispumpe eingerichtet"
        default: nil
        }
    }

    static func kesselkreispumpe(_ stand: Geraetestand) -> Pumpe? {
        guard let p = stand.heizgeraet?.kesselkreispumpe, p.aktiv == true else { return nil }
        let k = stand.konfiguration?["boiler_pump"]
        return Pumpe(
            name: "Kesselkreispumpe",
            betriebsart: pumpenbetriebsart(p.betriebsart),
            laeuft: p.ein ?? false,
            grund: pumpengrund(p.grundSchluessel) ?? p.grund ?? "",
            relais: relais(konfiguration: k, zustand: p.relais, weg: p.weg),
            einschaltschwelle: k?["on_k"]?.alsZahl ?? 0,
            ausschaltschwelle: k?["off_k"]?.alsZahl ?? 0,
            haltezeit: k?["hold_s"]?.alsGanzzahl ?? 0,
            notgrenze: k?["emergency_c"]?.alsZahl ?? 0
        )
    }

    // MARK: - Geräte

    static func geraete(verteiler: [Geraetestand], heizung: [Geraetestand], kessel: Geraetestand?, speicher: Geraetestand?) -> [Geraet] {
        var liste: [Geraet] = []
        for h in heizung.sorted(by: { ($0.geraet.id == kessel?.geraet.id ? 0 : 1) < ($1.geraet.id == kessel?.geraet.id ? 0 : 1) }) {
            let art: Geraet.Art = h.geraet.id == kessel?.geraet.id && h.geraet.id != speicher?.geraet.id ? .kessel
                : h.geraet.id == speicher?.geraet.id ? .speicher
                : (Ortsregel.istSpeicher(h.ort) ? .speicher : .kessel)
            liste.append(heizgeraetbild(h, art: art))
        }
        liste += verteiler.map(verteilerbild)
        liste += relaisgeraete(heizung)
        return liste
    }

    private static func grunddaten(_ s: Geraetestand) -> (adresse: String, hostname: String) {
        let host = s.geraet.adresse.host() ?? ""
        let adresse = s.geraet.adresse.port.map { "\(host):\($0)" } ?? host
        let hostname = s.konfiguration?["wifi"]?["hostname"]?.alsText ?? ""
        return (adresse, hostname)
    }

    /// Ein empfangenes Thermometer für die Anzeige; gemeinsam für Verteiler und Leitstand.
    public static func funkthermometer(_ t: Thermometerliste.Funkthermometer, zuordnung: String?) -> Funkthermometer? {
        guard let mac = t.mac else { return nil }
        // BTHome-Sender wie der Climate-Sat haben im Rundruf keinen Platz für einen Namen.
        let name = (t.name ?? "").isEmpty ? mac : t.name ?? mac
        return Funkthermometer(
            mac: mac, name: name, temperatur: t.temperaturC, feuchte: t.feuchte,
            // 0 % heißt beim RuuviTag und bei einem Thermometer ohne Schlüssel: keine Angabe.
            batterie: (t.batterie ?? 0) > 0 ? t.batterie : nil, batterieMillivolt: t.batterieMv ?? 0, signal: t.signal ?? -100,
            format: t.format ?? "", zuordnung: zuordnung,
            verschluesselt: t.verschluesselt ?? false, schluessel: t.schluessel.flatMap(Schluesselzustand.init(rawValue:)))
    }

    static func verteilerbild(_ s: Geraetestand) -> Geraet {
        let (adresse, hostname) = grunddaten(s)
        let z = s.verteiler
        let raeume = s.konfiguration?["rooms"]?.alsListe ?? []
        let aussenMac = s.konfiguration?["outdoor_mac"]?.alsText.map(normalisiert)
        let thermometer: [Funkthermometer] = s.thermometer.compactMap { t in
            guard let mac = t.mac else { return nil }
            let n = normalisiert(mac)
            let raum = raeume.first { normalisiert($0["sensor_mac"]?.alsText ?? "") == n }?["name"]?.alsText
            return funkthermometer(t, zuordnung: raum ?? (n == aussenMac && !n.isEmpty ? "Außenfühler" : nil))
        }
        let bord = z?.bordfuehler.map { b in
            Bordfuehler(
                gueltig: b.klima?.gueltig ?? false,
                temperatur: b.klima?.temperaturC ?? 0,
                feuchte: b.klima?.feuchte ?? 0,
                vorlauffuehler: (b.einwire ?? []).filter { $0.gueltig == true }.compactMap(\.temperaturC))
        }
        let schwellen = s.konfiguration?["touch"]?["thresholds"]?.alsListe?.compactMap(\.alsGanzzahl) ?? []
        let tasten = (z?.tastenRohwerte ?? []).enumerated().map { i, roh in
            Taste(name: tastenname(i), rohwert: Int(roh.rounded()), schwelle: i < schwellen.count ? schwellen[i] : 0)
        }
        return Geraet(
            id: s.geraet.id, art: .verteiler, ort: s.ort, adresse: adresse, hostname: hostname,
            firmware: z?.version ?? s.geraet.firmware ?? "", signal: z?.netz?.signal,
            laufzeit: z?.laufzeitSekunden ?? 0, erreichbar: s.erreichbar, freierSpeicher: z?.freierSpeicher,
            funkthermometer: thermometer, funkschluessel: s.thermometerSchluessel, bordfuehler: bord, tasten: tasten)
    }

    /// Namen wie in der Weboberfläche des Verteilers (`TOUCH_NAMES`)
    static func tastenname(_ index: Int) -> String {
        let namen = ["Sollwert senken", "Sollwert erhöhen", "Raum wählen"]
        return namen.indices.contains(index) ? namen[index] : "Taste \(index + 1)"
    }

    static func heizgeraetbild(_ s: Geraetestand, art: Geraet.Art) -> Geraet {
        let (adresse, hostname) = grunddaten(s)
        let z = s.heizgeraet
        let korrekturen = Dictionary(
            (s.konfiguration?["probes"]?.alsListe ?? []).compactMap { p -> (String, Double)? in
                guard let rom = p["rom"]?.alsText else { return nil }
                return (rom.uppercased(), p["offset_k"]?.alsZahl ?? 0)
            }, uniquingKeysWith: { a, _ in a })
        let fuehler: [Fuehler] = (z?.fuehler ?? []).compactMap { f in
            guard let rom = f.rom else { return nil }
            let zugeordnet = f.zugeordnet ?? !(f.rolle ?? "").isEmpty
            return Fuehler(
                rom: rom,
                rolle: zugeordnet ? (f.rolle ?? "") : "",
                rollenname: zugeordnet ? (f.rollenname ?? f.rolle ?? "") : "nicht zugeordnet",
                wert: f.temperaturC ?? f.rohC ?? 0,
                aenderung30s: f.aenderung30sK ?? 0,
                korrektur: f.korrekturK ?? korrekturen[rom.uppercased()] ?? 0,
                messungen: f.messungen ?? 0,
                fehler: f.fehler ?? 0)
        }
        let bus = z?.einwire.map {
            Einwirebus(
                anschluesse: ($0.anschluesse ?? []).filter { $0 >= 0 }, gefunden: $0.gefunden ?? 0,
                zugeordnet: $0.zugeordnet ?? 0, rundeMillisekunden: $0.rundeMs ?? 0, abfrageSekunden: $0.abtastS ?? 0)
        }
        return Geraet(
            id: s.geraet.id, art: art, ort: s.ort, adresse: adresse, hostname: hostname,
            firmware: z?.version ?? s.geraet.firmware ?? "", signal: z?.netz?.signal,
            laufzeit: z?.laufzeitSekunden ?? 0, erreichbar: s.erreichbar, freierSpeicher: z?.freierSpeicher,
            fuehler: fuehler, bus: bus)
    }

    /// Die Tasmota-Relais kennt die App nur aus der Konfiguration der Heizungsgeräte.
    static func relaisgeraete(_ heizung: [Geraetestand]) -> [Geraet] {
        var liste: [Geraet] = []
        let speicher = speichergeraet(heizung)?.geraet.id
        for h in heizung {
            let konfig = h.konfiguration?["circuits"]?.alsListe ?? []
            for c in h.geraet.id == speicher ? (h.heizgeraet?.heizkreise ?? []) : [] {
                guard let id = c.id, c.weg != "keiner" else { continue }
                let k = konfig.first { $0["id"]?.alsGanzzahl == id }
                let r = relais(konfiguration: k?["pump"], zustand: c.relais, weg: c.weg)
                liste.append(Geraet(
                    id: "relais:\(h.geraet.id)/\(id)", art: .relais, ort: "Relais \(c.name ?? "Heizkreis \(id)")",
                    adresse: r.adresse, hostname: "", firmware: "Tasmota", signal: nil, laufzeit: 0, erreichbar: r.erreichbar))
            }
            if let p = h.heizgeraet?.kesselkreispumpe, p.aktiv == true, p.weg != "keiner" {
                let r = relais(konfiguration: h.konfiguration?["boiler_pump"], zustand: p.relais, weg: p.weg)
                liste.append(Geraet(
                    id: "relais:\(h.geraet.id)/kkp", art: .relais, ort: "Relais Kesselkreispumpe",
                    adresse: r.adresse, hostname: "", firmware: "Tasmota", signal: nil, laufzeit: 0, erreichbar: r.erreichbar))
            }
        }
        return liste
    }

    // MARK: - Protokolle

    static func tagessaetze(_ s: Geraetestand) -> [Tagessatz] {
        var kalender = Calendar(identifier: .gregorian)
        kalender.timeZone = .current
        return s.tage.compactMap { t in
            guard let datum = kalender.date(from: t.datum) else { return nil }
            return Tagessatz(datum: datum, laufzeit: t.laufzeitS, starts: t.starts, liter: t.liter,
                             heizgradtage: t.heizgradtage ?? 0, aussenMin: t.aussenMinC, aussenMax: t.aussenMaxC)
        }
        .sorted { $0.datum > $1.datum }
    }

    static func ladungssaetze(_ s: Geraetestand) -> [Ladungssatz] {
        s.ladungen.map { l in
            Ladungssatz(beginn: l.beginn, dauer: l.dauerS, brenner: l.brennerS, starts: l.starts,
                        speicherVorher: l.pufferVorherC ?? 0, speicherNachher: l.pufferNachherC ?? 0,
                        kesselVorlaufMax: l.kesselVorlaufMaxC ?? 0, abgasMax: l.abgasMaxC ?? 0,
                        aussenMittel: l.aussenMittelC, liter: l.liter)
        }
        .sorted { $0.beginn > $1.beginn }
    }

    /// Brennerlaufzeit gegen Heizgradtage. Die Gerade rechnet das Gerät (ab 14 Tagen mit genug
    /// Spannweite der Außenlage); die Punkte stammen aus seinem Tagesprotokoll, ohne den
    /// laufenden Tag.
    static func verbrauchslinie(_ s: Geraetestand) -> Verbrauchslinie {
        let trend = s.heizgeraet?.verbrauchslinie
        let gueltig = trend?.gueltig == true
        let letzter = trend?.letzterTag.map {
            Verbrauchslinie.LetzterTag(heizgradtage: $0.gradtage ?? 0, stunden: $0.stunden ?? 0,
                                       erwartet: $0.erwartetH, streuungen: $0.streuungenAbstand)
        }
        let punkte = s.tage.dropFirst().filter { $0.heizgradtage != nil }
        let tage = punkte.enumerated().map { i, t in
            Verbrauchslinie.Tag(
                nummer: i, heizgradtage: t.heizgradtage ?? 0, laufzeitStunden: Double(t.laufzeitS) / 3600,
                auffaellig: i == 0 && (letzter?.streuungen ?? 0) > 3)
        }
        let anzahl = trend?.tage ?? punkte.count
        let ohneAussen = trend?.uebersprungen ?? 0
        let grund = gueltig ? nil
            : "\(anzahl == 1 ? "1 Tag liegt" : "\(anzahl) Tage liegen") vor\(ohneAussen > 0 ? ", \(ohneAussen) davon ohne Außenwert" : ""). Die Linie braucht mindestens 14 Tage und eine Außenlage, die weit genug auseinanderliegt; im Sommer liegen alle Tage bei null Heizgradtagen."
        return Verbrauchslinie(
            gueltig: gueltig, erfassteTage: anzahl, uebersprungen: ohneAussen, grund: grund,
            steigung: trend?.steigung ?? 0, grundlast: trend?.grundlast ?? 0,
            streuung: trend?.streuung, bestimmtheit: trend?.bestimmtheit, letzterTag: letzter, tage: tage)
    }

    // MARK: - Verlauf

    static func reihe(_ rolle: String) -> Kurzverlauf.Reihe? {
        switch rolle {
        case "abgas": .abgas
        case "kessel_vl": .kesselVorlauf
        case "kessel_rl": .kesselRuecklauf
        case "puffer": .speicher
        case "hk1_vl": .hk1Vorlauf
        case "hk1_rl": .hk1Ruecklauf
        case "hk2_vl": .hk2Vorlauf
        case "hk2_rl": .hk2Ruecklauf
        case "aussen": .aussen
        default: nil
        }
    }

    /// Die Reihen beider Heizungsgeräte auf einer gemeinsamen Zeitachse. Grundlage ist das Gerät
    /// mit den meisten Punkten; die übrigen werden nach ihrem jüngsten Zeitpunkt eingepasst.
    /// Kennzeichnet die Verlaufsantworten der Heizungsgeräte; gleich heißt: `verlauf` ergäbe
    /// dasselbe.
    public static func verlaufskennung(_ staende: [Geraetestand]) -> String {
        staende.filter { $0.geraet.art == .heizung }.sorted { $0.geraet.id < $1.geraet.id }
            .map { "\($0.geraet.id):\($0.verlauf?.juengsterEpoch ?? 0):\($0.verlauf?.punkte ?? 0):\($0.verlauf?.schrittMin ?? 0)" }
            .joined(separator: "|")
    }

    public static func verlauf(_ staende: [Geraetestand], jetzt: Date) -> Kurzverlauf {
        let heizung = staende.filter { $0.geraet.art == .heizung }
        let antworten = heizung.compactMap(\.verlauf).filter { ($0.punkte ?? 0) > 0 && $0.juengsterEpoch != nil }
        guard let basis = antworten.max(by: { ($0.punkte ?? 0) < ($1.punkte ?? 0) }),
              let punkte = basis.punkte, let juengster = basis.juengsterEpoch, let schrittMin = basis.schrittMin, schrittMin > 0 else {
            return Kurzverlauf(beginn: jetzt, schritt: 600, werte: [:])
        }
        let schritt = TimeInterval(schrittMin * 60)
        let beginn = Date(timeIntervalSince1970: TimeInterval(juengster) - Double(punkte - 1) * schritt)
        var werte: [Kurzverlauf.Reihe: [Double?]] = [:]
        for antwort in antworten {
            guard let eigenerJuengster = antwort.juengsterEpoch else { continue }
            for (rolle, liste) in antwort.reihen ?? [:] {
                guard let r = reihe(rolle), werte[r] == nil else { continue }
                var ziel = [Double?](repeating: nil, count: punkte)
                let n = liste.count
                for (i, wert) in liste.enumerated() {
                    let zeit = TimeInterval(eigenerJuengster) - Double(n - 1 - i) * TimeInterval((antwort.schrittMin ?? schrittMin) * 60)
                    let index = Int(((zeit - beginn.timeIntervalSince1970) / schritt).rounded())
                    if ziel.indices.contains(index) { ziel[index] = wert }
                }
                werte[r] = ziel
            }
        }
        return Kurzverlauf(beginn: beginn, schritt: schritt, werte: werte)
    }
}

/// Ob ein Heizungsgerät am Speicher sitzt, verrät zur Not sein Ort.
enum Ortsregel {
    static func istSpeicher(_ ort: String) -> Bool {
        let o = ort.lowercased()
        return o.contains("speicher") || o.contains("puffer")
    }
}
