import Foundation
import Anlage
import Diagnose
import Geraeteschnittstelle
import Verlauf
@testable import Assistent

/// Die Aufnahmen liegen bei der Geräteschnittstelle; hier werden sie nur gelesen.
enum Aufnahme {
    static let ordner = URL(fileURLWithPath: #filePath)
        .deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
        .appending(path: "Geraeteschnittstelle/Tests/GeraeteschnittstelleTests/Fixtures")

    /// `floor-heating-ctrl/docs`
    static let dokumente = URL(fileURLWithPath: #filePath)
        .deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
        .deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
        .appending(path: "docs")

    static func lesen<T: Decodable>(_ name: String, als: T.Type = T.self) throws -> T {
        try JSONDecoder().decode(T.self, from: Data(contentsOf: ordner.appending(path: name)))
    }

    static func json(_ name: String) throws -> JSONWert {
        try JSONWert.lesen(Data(contentsOf: ordner.appending(path: name)))
    }
}

/// Eine Anlage aus den Aufnahmen der Attrappen: zwei Verteiler, Kessel und Pufferspeicher.
/// Sie schreibt in ihre eigene Konfiguration, wie es die Firmware täte, und merkt sich jeden
/// Schreib- und Sicherungsvorgang.
actor Testanlage: Vorschlagsausfuehrung {
    var staendeWert: [Geraetestand]
    var vorschlaege: [Vorschlag] = []
    var protokoll: [Aenderung] = []
    var geschrieben: [(geraet: String, neu: Bool)] = []
    var gesichert: [String] = []
    var ausgefuehrt: [Vorschlag.Aktion] = []
    var scheitertBeimSchreiben: Set<String> = []
    var verlaufWert: Verlaufsauszug?
    var befundlageWert = Befundlage(offen: [], erledigt: [])
    var aenderungenWert: [Aenderung] = []
    var ereignisseWert: [Ereignis] = []
    var feinverlaufWert: Leitstand.Protokollreihe?
    var feinverlaufAnfragen: [(geraet: String, schluessel: [String], raster: Int)] = []

    init() throws {
        let verteiler: Verteilerzustand = try Aufnahme.lesen("attrappe/verteiler-state.json")
        let vKonfig = try Aufnahme.json("attrappe/verteiler-config.json")
        var og = verteiler
        og.geraet?.id = "fbh_d4e5f6"
        og.geraet?.ort = "Obergeschoss"
        var ogKonfig = vKonfig
        ogKonfig["site"] = "Obergeschoss"

        var sKonfig = try Aufnahme.json("attrappe/speicher-config.json")
        sKonfig["buffer"] = ["voll_c": 62, "leer_c": 51.5, "warn_c": 40, "volumen_l": 800, "leer_lernen": true,
                             "lern_drop_k": 3, "zapf_drop_k": 2, "zapf_win_s": 900, "spread_full_k": 8, "spread_hold_s": 300, "kessel_hot_c": 60]
        sKonfig["burner"] = ["delta_on_k": 12, "delta_off_k": 6, "swing_k": 6, "on_hold_s": 60, "off_hold_s": 300, "duese_l_h": 2.2]
        sKonfig["demand_poll_s"] = 5
        sKonfig["demand_timeout_s"] = 180
        var kKonfig = sKonfig
        kKonfig["site"] = "Kessel"
        kKonfig["circuits"] = []
        kKonfig["probes"] = [["rom": "28AA000000000001", "role": "abgas", "name": "Abgas", "offset_k": 0],
                             ["rom": "28AA000000000002", "role": "kessel_vl", "name": "Kessel Vorlauf", "offset_k": 0],
                             ["rom": "28AA000000000003", "role": "kessel_rl", "name": "Kessel Rücklauf", "offset_k": 0]]
        kKonfig["boiler_pump"]?["enabled"] = true
        kKonfig["boiler_pump"]?["on_k"] = 3
        kKonfig["boiler_pump"]?["off_k"] = 2

        func geraet(_ id: String, _ ort: String) -> BekanntesGeraet {
            BekanntesGeraet(id: id, art: id.hasPrefix("fbh_") ? .verteiler : .heizung, ort: ort, adresse: URL(string: "http://127.0.0.1")!)
        }
        staendeWert = [
            Geraetestand(geraet: geraet("fbh_a1b2c3", "Erdgeschoss"), erreichbar: true, letzterKontakt: .now,
                         verteiler: verteiler, rohzustand: try Aufnahme.json("attrappe/verteiler-state.json"), konfiguration: vKonfig),
            Geraetestand(geraet: geraet("fbh_d4e5f6", "Obergeschoss"), erreichbar: true, letzterKontakt: .now,
                         verteiler: og, konfiguration: ogKonfig),
            Geraetestand(geraet: geraet("heiz_3f21ac", "Pufferspeicher"), erreichbar: true, letzterKontakt: .now,
                         heizgeraet: try Aufnahme.lesen("attrappe/speicher-state.json"), konfiguration: sKonfig),
            Geraetestand(geraet: geraet("heiz_9a1b2c", "Kessel"), erreichbar: true, letzterKontakt: .now,
                         heizgeraet: try Aufnahme.lesen("attrappe/kessel-state.json"), konfiguration: kKonfig),
        ]
    }

    func stand(_ id: String) -> Geraetestand? { staendeWert.first { $0.geraet.id == id } }

    func aendern(_ id: String, _ aenderung: (inout Geraetestand) -> Void) {
        guard let i = staendeWert.firstIndex(where: { $0.geraet.id == id }) else { return }
        aenderung(&staendeWert[i])
    }

    func scheitern(_ id: String) { scheitertBeimSchreiben.insert(id) }

    // MARK: Anlagenzugriff

    func bild() async -> Anlagenbild { Zusammenfuehrung.bild(staendeWert) }
    func staende() async -> [Geraetestand] { staendeWert }
    func befundlage() async -> Befundlage { befundlageWert }
    func verlauf(von: Date, bis: Date, schritt: TimeInterval, schluessel: Set<String>?) async -> Verlaufsauszug? { verlaufWert }
    func aenderungen() async -> [Aenderung] { aenderungenWert + protokoll }
    func vorschlagAnlegen(_ vorschlag: Vorschlag) async { vorschlaege.append(vorschlag) }
    func ereignisse(von: Date, bis: Date) async -> [Ereignis] { ereignisseWert.filter { $0.zeit >= von && $0.zeit <= bis } }
    func leitstandKennung() async -> String? { "lst_c0ffee" }
    func feinverlauf(geraet: String, schluessel: [String], von: Date, bis: Date, raster: Int) async throws -> Leitstand.Protokollreihe {
        feinverlaufAnfragen.append((geraet, schluessel, raster))
        guard let f = feinverlaufWert else { throw Feinverlaufsfehler.keinLeitstand }
        return f
    }
    func setzeEreignisse(_ e: [Ereignis]) { ereignisseWert = e }
    func setzeFeinverlauf(_ f: Leitstand.Protokollreihe?) { feinverlaufWert = f }

    // MARK: Vorschlagsausführung

    func sichern(_ geraet: String) async throws -> String? {
        gesichert.append(geraet)
        return "\(geraet)_sicherung"
    }

    func frischerStand(_ geraet: String) async throws -> Geraetestand {
        guard let s = stand(geraet) else { throw Befehlsfehler.geraetUnbekannt }
        return s
    }

    func schreiben(_ ziele: [Vorschlag.Ziel], neu: Bool) async throws {
        guard let id = ziele.first?.geraet, let s = stand(id) else { throw Befehlsfehler.geraetUnbekannt }
        if neu, scheitertBeimSchreiben.contains(id) { throw Befehlsfehler.nichtUebernommen("Das Gerät hat nicht alle Werte übernommen.") }
        if let k = s.konfiguration, let teil = Zielwerte.teil(ziele, neu: neu, konfiguration: k) {
            aendern(id) { $0.konfiguration = Schreibweg.zusammengefuehrt(k, teil) }
        }
        for z in ziele {
            let w = neu ? z.neu : z.bisher
            switch Schreibart.fuer(z.parameter) {
            case .sollwert:
                aendern(id) { s in
                    guard let i = s.verteiler?.raeume?.firstIndex(where: { $0.id == z.eintrag?.alsGanzzahl }) else { return }
                    s.verteiler?.raeume?[i].sollC = w.alsZahl
                }
            case .heizkreisbetrieb:
                aendern(id) { s in
                    guard let i = s.heizgeraet?.heizkreise?.firstIndex(where: { $0.id == z.eintrag?.alsGanzzahl }) else { return }
                    s.heizgeraet?.heizkreise?[i].betriebsart = w.alsText
                }
            default:
                break
            }
        }
        geschrieben.append((id, neu))
    }

    func ausfuehren(_ aktion: Vorschlag.Aktion, _ ziel: Vorschlag.Ziel) async throws -> String? {
        ausgefuehrt.append(aktion)
        return aktion == .relaispruefung ? "Relais antwortet, Kanal 1 aus" : nil
    }

    func protokollieren(_ aenderungen: [Aenderung]) async { protokoll += aenderungen }
}
