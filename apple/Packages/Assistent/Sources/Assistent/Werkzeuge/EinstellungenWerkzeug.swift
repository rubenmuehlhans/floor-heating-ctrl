import Foundation
import FoundationModels
import Anlage
import Geraeteschnittstelle

/// Lesendes Werkzeug: die Einstellungen der Geräte mit Bedeutung, Einheit, Bereich und Vorgabe
/// aus dem Parameterkatalog.
///
/// Netzwerk, MQTT, Kennwörter, Benutzer und die Adressen der Relais fehlen; sie gehen die KI
/// nichts an und stehen ihr nicht zur Änderung offen.
public struct EinstellungenWerkzeug: Tool {
    public let name = "einstellungen_lesen"
    public let description = """
        Liest die Einstellungen der Geräte aus der zuletzt gelesenen Konfiguration: Werte je \
        Gerät, je Raum, Kanal, Heizkreis und Fühler, dazu unter parameter Bezeichnung, Einheit, \
        Bereich und Vorgabe und ob die KI eine Änderung vorschlagen darf (ki). Die Schlüssel \
        sind dieselben wie für aenderung_vorschlagen. Netzwerk, MQTT, Kennwörter und \
        Relaisadressen sind nicht enthalten.
        """

    @Generable
    public struct Argumente {
        @Guide(description: "Nur dieses Gerät: Kennung oder Ort wie Erdgeschoss, Kessel, Pufferspeicher; leer für alle")
        public var geraet: String?
        @Guide(description: "Nur diese Gruppe", .anyOf(["raum", "kanal", "betrieb", "tasten", "zeit", "bus", "bedarf", "brenner", "speicher", "kkp", "heizkreis", "fuehler", "geraet"]))
        public var gruppe: String?
        @Guide(description: "true: die Hilfetexte zu den Einstellungen mitliefern")
        public var erlaeuterungen: Bool?
    }

    let zugriff: any Anlagenzugriff

    public init(zugriff: any Anlagenzugriff) {
        self.zugriff = zugriff
    }

    @concurrent public func call(arguments: Argumente) async throws -> String {
        let staende = await zugriff.staende()
        let bild = await zugriff.bild()
        return Self.ausgabe(arguments, staende: staende, bild: bild).ohneZugangsdaten().kompakt
    }

    /// Einstellungen, die die KI weder liest noch vorschlagen darf
    static func ausgeschlossen(_ p: Parameter) -> Bool {
        if ["netz", "mqtt"].contains(p.gruppe) || p.istKennwort { return true }
        let relais = ["host", "topic", "user", "pass"]
        return p.pfad.contains { relais.contains($0) }
    }

    static func ausgabe(_ a: Argumente, staende: [Geraetestand], bild: Anlagenbild) -> JSONWert {
        let auswahl = Aufloesung.geraete(a.geraet, in: staende, bild: bild)
            .sorted { ($0.geraet.art.rawValue, $0.ort) < ($1.geraet.art.rawValue, $1.ort) }
        if auswahl.isEmpty {
            return ["fehler": .text("Kein Gerät „\(a.geraet ?? "")“. Bekannt sind: \(staende.map { "\($0.ort) (\($0.geraet.id))" }.joined(separator: ", ")).")]
        }
        let orte = Dictionary(staende.map { ($0.geraet.id, $0.ort) }, uniquingKeysWith: { x, _ in x })
        var benutzt: [String: Parameter] = [:]
        var geraete: [JSONWert] = []
        for s in auswahl {
            guard let k = s.konfiguration else {
                geraete.append(["geraet": .text(s.geraet.id), "ort": .text(s.ort), "fehler": "Konfiguration noch nicht gelesen"])
                continue
            }
            let parameter = Parameterkatalog.alle.filter { p in
                p.geraet == s.geraet.art && !ausgeschlossen(p) && (a.gruppe == nil || p.gruppe == a.gruppe)
            }
            var e: JSONWert = ["geraet": .text(s.geraet.id), "art": .text(s.geraet.art.rawValue), "ort": .text(s.ort)]
            if let rolle = bild.geraete.first(where: { $0.id == s.geraet.id })?.art, rolle == .kessel || rolle == .speicher {
                e["rolle"] = .text(rolle == .kessel ? "Kessel" : "Pufferspeicher")
            }
            var werte: [String: JSONWert] = [:]
            for p in parameter where p.ort == .geraet {
                guard let w = Zielwerte.wert(p, eintrag: nil, in: k) else { continue }
                werte[p.schluessel] = w
                benutzt[p.id] = p
            }
            if !werte.isEmpty { e["werte"] = .objekt(werte) }

            for (ort, name) in [(Parameter.Ort.raum, "raeume"), (.kanal, "kanaele"), (.heizkreis, "heizkreise"), (.fuehler, "fuehler")] {
                let felder = parameter.filter { $0.ort == ort }
                guard !felder.isEmpty || (ort == .heizkreis && a.gruppe == "heizkreis"), let liste = ort.liste,
                      let eintraege = k[liste]?.alsListe, !eintraege.isEmpty else { continue }
                e[name] = .liste(eintraege.map { eintrag in
                    var z: [String: JSONWert] = [:]
                    if ort != .fuehler { z["nr"] = eintrag["id"] ?? .null }
                    if let n = eintrag["name"], ort != .kanal { z["name"] = n }
                    switch ort {
                    case .raum:
                        z["kanaele"] = eintrag["channels"] ?? .liste([])
                        z["thermometer"] = .bool(!(eintrag["sensor_mac"]?.alsText ?? "").isEmpty)
                    case .heizkreis:
                        z["versorgt"] = .liste((eintrag["peers"]?.alsListe ?? []).map { p in .text(p.alsText.map { orte[$0] ?? $0 } ?? p.kompakt) })
                    case .kanal:
                        z["kalibriert"] = eintrag["calibrated"] ?? .null
                    default:
                        break
                    }
                    for p in felder {
                        guard let w = p.pfad.reduce(Optional(eintrag), { acc, t in Int(t).map { acc?[$0] } ?? acc?[t] }) else { continue }
                        z[p.pfad.joined(separator: ".")] = w
                        benutzt[p.id] = p
                    }
                    return .objekt(z)
                })
            }
            geraete.append(e)
        }
        var beschreibung: [String: JSONWert] = [:]
        for (id, p) in benutzt {
            var m: [String: JSONWert] = ["name": .text(p.bezeichnung)]
            if !p.einheit.isEmpty { m["einheit"] = .text(p.einheit) }
            switch p.art {
            case .zahl(let b, let schritt, _): m["bereich"] = [.zahl(b.lowerBound), .zahl(b.upperBound)]; m["schritt"] = .zahl(schritt)
            case .ganzzahl(let b, let schritt): m["bereich"] = [.zahl(Double(b.lowerBound)), .zahl(Double(b.upperBound))]; m["schritt"] = .zahl(Double(schritt))
            case .millisekunden(let b, _): m["bereich_s"] = [.zahl(b.lowerBound), .zahl(b.upperBound)]; m["einheit"] = "ms"
            case .auswahl(let wahl): m["werte"] = .objekt(Dictionary(wahl.map { ($0.wert.alsText ?? $0.wert.kompakt, JSONWert.text($0.bezeichnung)) }, uniquingKeysWith: { x, _ in x }))
            case .schalter, .text, .kennwort: break
            }
            m["vorgabe"] = p.vorgabe
            if p.kiFreigegeben { m["ki"] = true }
            if p.anlagenweit { m["auf_allen_heizungsgeraeten_gleich"] = true }
            if p.wirkung != .sofort { m["wirkt"] = .text(p.wirkung == .neustart ? "nach Neustart" : "nach Neuverbindung") }
            if a.erlaeuterungen == true { m["hilfe"] = .text(p.hilfe) }
            beschreibung[id] = .objekt(m)
        }
        return ["geraete": .liste(geraete), "parameter": .objekt(beschreibung)]
    }
}
