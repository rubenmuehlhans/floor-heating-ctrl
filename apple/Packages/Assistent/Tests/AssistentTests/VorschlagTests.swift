import Foundation
import Testing
import Anlage
import Geraeteschnittstelle
@testable import Assistent

/// Die Prüfung der Vorschläge, bevor eine Karte entsteht
@Suite("Vorschlagsbau")
struct VorschlagsbauTests {
    func bau(_ angaben: [Vorschlagsbau.Angabe], geraet: String? = nil, _ anlage: Testanlage) async -> Vorschlagsbau.Ergebnis {
        Vorschlagsbau.aenderung(titel: "", geraet: geraet, angaben: angaben, begruendung: "Belegt durch die Ladungen.",
                                wirkung: "Die Anzeige stimmt.", staende: await anlage.staende(), bild: await anlage.bild())
    }

    func vorschlag(_ e: Vorschlagsbau.Ergebnis) throws -> Vorschlag {
        guard case .vorschlag(let v) = e else {
            if case .abgelehnt(let f) = e { Issue.record("abgelehnt: \(f)") }
            throw CancellationError()
        }
        return v
    }

    func fehler(_ e: Vorschlagsbau.Ergebnis) -> [String] {
        if case .abgelehnt(let f) = e { return f }
        return []
    }

    /// Speicherwerte gelten auf beiden Heizungsgeräten, auch wenn die KI nur eines nennt.
    @Test func speicherVollAufBeidenGeraeten() async throws {
        let anlage = try Testanlage()
        let v = try vorschlag(await bau([.init(parameter: "buffer.voll_c", wert: "68,2 °C")], geraet: "Pufferspeicher", anlage))
        #expect(Set(v.ziele.map(\.geraet)) == ["heiz_3f21ac", "heiz_9a1b2c"])
        #expect(v.ziele.allSatisfy { $0.bisher == 62 && $0.neu == 68.0 }, "auf das Raster von 0,5 K gerundet")
        #expect(v.bisher == "62,0\u{00A0}°C")
        #expect(v.neu == "68,0\u{00A0}°C")
        #expect(v.geraete == ["Pufferspeicher", "Kessel"] || v.geraete == ["Kessel", "Pufferspeicher"])
        #expect(v.titel.contains("62,0"))
    }

    @Test func bereichUndFreigabe() async throws {
        let anlage = try Testanlage()
        #expect(fehler(await bau([.init(parameter: "buffer.voll_c", wert: "120")], anlage)).first?.contains("zwischen 20 und 95") == true)
        #expect(fehler(await bau([.init(parameter: "circuits[].frost_c", eintrag: "1", wert: "4")], anlage)).first?.contains("nicht freigegeben") == true)
        #expect(fehler(await bau([.init(parameter: "wifi.ssid", wert: "Gast")], anlage)).first?.contains("nicht freigegeben") == true)
        #expect(fehler(await bau([.init(parameter: "buffer.gibtsnicht", wert: "1")], anlage)).first?.contains("Unbekannte Einstellung") == true)
        #expect(fehler(await bau([.init(parameter: "buffer.voll_c", wert: "62")], anlage)).first?.contains("bereits auf 62,0") == true)
    }

    /// „voll“ unter „leer“ lehnt schon die App ab, nicht erst die Firmware.
    @Test func gegenseitigeBedingungen() async throws {
        let anlage = try Testanlage()
        let f = fehler(await bau([.init(parameter: "buffer.voll_c", wert: "50")], anlage))
        #expect(f.contains { $0.contains("„voll“ muss über dem für „leer“") })
        // Zusammen in einem Vorschlag geht es.
        let v = try vorschlag(await bau([.init(parameter: "buffer.leer_c", wert: "45"), .init(parameter: "buffer.voll_c", wert: "50")], anlage))
        #expect(v.ziele.count == 4)
        #expect(v.bisher.contains("Leer bei"))
    }

    @Test func raeumeNachNameUndVerteiler() async throws {
        let anlage = try Testanlage()
        // „Küche“ gibt es auf beiden Verteilern der Testanlage.
        #expect(fehler(await bau([.init(parameter: "rooms[].p_band_k", eintrag: "Küche", wert: "1,5")], anlage)).first?.contains("mehreren Verteilern") == true)
        let v = try vorschlag(await bau([.init(parameter: "rooms[].p_band_k", eintrag: "kuche", wert: "1,5")], geraet: "Erdgeschoss", anlage))
        #expect(v.ziele.first?.geraet == "fbh_a1b2c3")
        #expect(v.ziele.first?.eintrag == 1)
        #expect(v.ziele.first?.bezeichnung == "Proportionalband · Küche (Erdgeschoss)")
        let soll = try vorschlag(await bau([.init(parameter: "verteiler/rooms[].target_c", eintrag: "2", wert: "21.5")], geraet: "fbh_d4e5f6", anlage))
        #expect(soll.ziele.first?.neu == 21.5)
    }

    @Test func geraetNachRolle() async throws {
        let anlage = try Testanlage()
        // Die Brennererkennung wirkt am Kessel; ohne Angabe wählt die App ihn.
        let v = try vorschlag(await bau([.init(parameter: "burner.delta_on_k", wert: "14")], anlage))
        #expect(v.ziele.map(\.geraet) == ["heiz_9a1b2c"])
        // Die Einschaltschwelle muss über der Ausschaltschwelle bleiben.
        #expect(fehler(await bau([.init(parameter: "burner.delta_off_k", wert: "13")], geraet: "Kessel", anlage)).contains { $0.contains("Einschaltschwelle") })
        // Die Verteiler haben beide einen Schutzfahrttermin: ohne Gerät keine Wahl.
        #expect(fehler(await bau([.init(parameter: "verteiler/seize_hour", wert: "14")], anlage)).first?.contains("mehreren Geräten") == true)
    }

    @Test func pumpenUndVersorgteVerteiler() async throws {
        let anlage = try Testanlage()
        let aus = try vorschlag(await bau([.init(parameter: "heizkreis.betriebsart", eintrag: "Heizkreis 2", wert: "aus")], anlage))
        #expect(aus.ziele.first?.bisher == "auto")
        #expect(aus.neu == "aus")
        #expect(fehler(await bau([.init(parameter: "heizkreis.betriebsart", eintrag: "2", wert: "turbo")], anlage)).first?.contains("auto, ein oder aus") == true)
        // Obergeschoss hängt schon an Heizkreis 2; an Heizkreis 1 wäre er doppelt versorgt.
        let doppelt = fehler(await bau([.init(parameter: "circuits[].peers", eintrag: "1", wert: "Erdgeschoss, Obergeschoss")], anlage))
        #expect(doppelt.contains { $0.contains("Obergeschoss wird schon von Obergeschoss versorgt") })
        #expect(fehler(await bau([.init(parameter: "circuits[].peers", eintrag: "2", wert: "Dachboden")], anlage)).first?.contains("Unbekannte Verteiler") == true)
        let kkp = try vorschlag(await bau([.init(parameter: "kesselkreispumpe.betriebsart", wert: "ein")], anlage))
        #expect(kkp.ziele.first?.geraet == "heiz_9a1b2c")
    }

    @Test func aktionen() async throws {
        let anlage = try Testanlage()
        let staende = await anlage.staende()
        let bild = await anlage.bild()
        func aktion(_ a: Vorschlag.Aktion, _ g: String, _ e: String?, _ aenderungen: [Aenderung] = []) -> Vorschlagsbau.Ergebnis {
            Vorschlagsbau.aktion(a, geraet: g, eintrag: e, titel: "", begruendung: "Nie vermessen.", wirkung: "Endlage bekannt.",
                                 staende: staende, bild: bild, aenderungen: aenderungen)
        }
        let m = try vorschlag(aktion(.messfahrt, "Erdgeschoss", "8"))
        #expect(m.ziele.first?.eintrag == 8)
        #expect(m.neu.contains("Wohnzimmer"))
        #expect(fehler(aktion(.messfahrt, "Erdgeschoss", "11")).first?.contains("gehört zu keinem Raum") == true)
        #expect(fehler(aktion(.messfahrt, "Kessel", "1")).first?.contains("braucht einen Verteiler") == true)
        #expect(fehler(aktion(.neustart, "Kessel", nil)).first?.contains("keine verzeichnet") == true)
        let zeitzone = Aenderung(id: "a", zeit: .now, geraet: "Kessel", parameter: "Zeitzone", bisher: "", neu: "",
                                 ausloeser: "In der App geändert", geraetekennung: "heiz_9a1b2c", parameterkennung: "heizung/timezone")
        #expect(try vorschlag(aktion(.neustart, "Kessel", nil, [zeitzone])).aktion == .neustart)
        let relais = try vorschlag(aktion(.relaispruefung, "Pufferspeicher", "2"))
        #expect(relais.ziele.first?.eintrag == 2)
    }
}

/// Übernahme, Rücknahme und Wirkungskontrolle gegen eine Anlage, die wie die Firmware schreibt
@Suite("Vorschlagsablauf")
struct VorschlagsablaufTests {
    func speicherVoll(_ anlage: Testanlage) async throws -> Vorschlag {
        let e = Vorschlagsbau.aenderung(titel: "Speicher voll", geraet: nil, angaben: [.init(parameter: "buffer.voll_c", wert: "68")],
                                        begruendung: "b", wirkung: "w", staende: await anlage.staende(), bild: await anlage.bild())
        guard case .vorschlag(let v) = e else { throw CancellationError() }
        return v
    }

    @Test func uebernehmenUndZuruecknehmen() async throws {
        let anlage = try Testanlage()
        let v = try await speicherVoll(anlage)
        let jetzt = Date.now
        let uebernommen = await Vorschlagsablauf.uebernehmen(v, mit: anlage, jetzt: jetzt)
        #expect(uebernommen.status == .uebernommen(jetzt))
        #expect(await anlage.gesichert.sorted() == ["heiz_3f21ac", "heiz_9a1b2c"], "vor dem Schreiben gesichert")
        #expect(uebernommen.sicherungen["heiz_3f21ac"] == "heiz_3f21ac_sicherung")
        #expect(await anlage.stand("heiz_3f21ac")?.konfiguration?["buffer"]?["voll_c"] == 68)
        #expect(await anlage.stand("heiz_9a1b2c")?.konfiguration?["buffer"]?["voll_c"] == 68)
        #expect(await anlage.protokoll.count == 2)
        #expect(await anlage.protokoll.first?.ausloeser == "Vorschlag der KI, bestätigt")
        #expect(uebernommen.kontrolle?.faellig == jetzt.addingTimeInterval(7 * 86_400))

        let zurueck = await Vorschlagsablauf.zuruecknehmen(uebernommen, mit: anlage)
        #expect(zurueck.status == .zurueckgenommen)
        #expect(await anlage.stand("heiz_3f21ac")?.konfiguration?["buffer"]?["voll_c"] == 62)
        #expect(await anlage.protokoll.last?.neu == "62,0\u{00A0}°C")
    }

    /// Hat jemand den Wert inzwischen am Gerät geändert, bleibt alles, wie es ist.
    @Test func inzwischenGeaendert() async throws {
        let anlage = try Testanlage()
        let v = try await speicherVoll(anlage)
        await anlage.aendern("heiz_9a1b2c") { $0.konfiguration?["buffer"]?["voll_c"] = 64 }
        let ergebnis = await Vorschlagsablauf.uebernehmen(v, mit: anlage)
        guard case .gescheitert(let meldung) = ergebnis.status else {
            Issue.record("erwartet: gescheitert")
            return
        }
        #expect(meldung.contains("inzwischen auf 64,0"))
        #expect(await anlage.geschrieben.isEmpty)
        #expect(await anlage.gesichert.isEmpty)
    }

    /// Scheitert das zweite Gerät, nimmt die App das erste zurück.
    @Test func zweitesGeraetScheitert() async throws {
        let anlage = try Testanlage()
        let v = try await speicherVoll(anlage)
        let zweites = v.ziele.last!.geraet
        let erstes = v.ziele.first!.geraet
        await anlage.scheitern(zweites)
        let ergebnis = await Vorschlagsablauf.uebernehmen(v, mit: anlage)
        guard case .gescheitert(let meldung) = ergebnis.status else {
            Issue.record("erwartet: gescheitert")
            return
        }
        #expect(meldung.contains("auf den bisherigen Wert zurückgesetzt"))
        #expect(await anlage.stand(erstes)?.konfiguration?["buffer"]?["voll_c"] == 62)
        #expect(await anlage.geschrieben.map(\.neu) == [true, false])
        #expect(await anlage.protokoll.isEmpty)
    }

    /// Eine spätere Änderung von Hand überschreibt die Rücknahme nicht.
    @Test func ruecknahmeNachHandaenderung() async throws {
        let anlage = try Testanlage()
        let v = await Vorschlagsablauf.uebernehmen(try await speicherVoll(anlage), mit: anlage)
        await anlage.aendern("heiz_3f21ac") { $0.konfiguration?["buffer"]?["voll_c"] = 70 }
        let zurueck = await Vorschlagsablauf.zuruecknehmen(v, mit: anlage)
        #expect(zurueck.status == v.status, "bleibt übernommen, Rückgängig bleibt möglich")
        #expect(zurueck.ergebnis?.contains("70,0") == true)
        #expect(await anlage.stand("heiz_3f21ac")?.konfiguration?["buffer"]?["voll_c"] == 70)
    }

    @Test func pumpeUeberDenBefehlsweg() async throws {
        let anlage = try Testanlage()
        let e = Vorschlagsbau.aenderung(titel: "", geraet: nil, angaben: [.init(parameter: "heizkreis.betriebsart", eintrag: "2", wert: "aus")],
                                        begruendung: "b", wirkung: "w", staende: await anlage.staende(), bild: await anlage.bild())
        guard case .vorschlag(let v) = e else { throw CancellationError() }
        let ergebnis = await Vorschlagsablauf.uebernehmen(v, mit: anlage)
        #expect(ergebnis.kontrolle != nil, "übernommen")
        #expect(await anlage.stand("heiz_3f21ac")?.heizgeraet?.heizkreise?.first { $0.id == 2 }?.betriebsart == "aus")
    }

    @Test func aktionAusfuehren() async throws {
        let anlage = try Testanlage()
        let e = Vorschlagsbau.aktion(.relaispruefung, geraet: "Pufferspeicher", eintrag: "1", titel: "", begruendung: "b", wirkung: "w",
                                     staende: await anlage.staende(), bild: await anlage.bild(), aenderungen: [])
        guard case .vorschlag(let v) = e else { throw CancellationError() }
        let ergebnis = await Vorschlagsablauf.uebernehmen(v, mit: anlage)
        #expect(ergebnis.ergebnis == "Relais antwortet, Kanal 1 aus")
        #expect(await anlage.ausgefuehrt == [.relaispruefung])
        #expect(await anlage.gesichert.isEmpty, "Aktionen ändern keine Einstellung")
    }

    @Test func wirkungskontrolle() async throws {
        let anlage = try Testanlage()
        let beginn = Date.now.addingTimeInterval(-8 * 86_400)
        var v = await Vorschlagsablauf.uebernehmen(try await speicherVoll(anlage), mit: anlage, jetzt: beginn)
        #expect(v.kontrolle?.ausgewertet == nil)
        let frueh = await Vorschlagsablauf.wirkungPruefen(v, mit: anlage, jetzt: beginn.addingTimeInterval(86_400))
        #expect(frueh.kontrolle?.ausgewertet == nil, "vor Ablauf nichts")
        v = await Vorschlagsablauf.wirkungPruefen(v, mit: anlage)
        #expect(v.kontrolle?.ausgewertet != nil)
        #expect(v.kontrolle?.nachher != nil)
    }
}

@Suite("Ablage")
@MainActor
struct AblageTests {
    @Test func speichernUndLaden() throws {
        let ordner = FileManager.default.temporaryDirectory.appending(path: "ablage-\(UUID().uuidString)")
        defer { try? FileManager.default.removeItem(at: ordner) }
        let a = Assistenzablage(ordner: ordner)
        var v = Vorschlag.speicherVoll
        v.status = .gescheitert("Test")
        a.vorschlagSichern(v)
        a.protokollieren([Aenderung(id: "1", zeit: .now, geraet: "Kessel", parameter: "Voll bei", bisher: "62", neu: "68", ausloeser: "t")])
        var g = Gespraech.neu()
        g.beitraege = [Beitrag(id: "b", rolle: .nutzer, bausteine: [.text("Wie läuft es?")], zeit: .now)]
        a.gespraechSichern(g, transkript: nil)

        let b = Assistenzablage(ordner: ordner)
        #expect(b.vorschlag("voll_68")?.status == .gescheitert("Test"))
        #expect(b.aenderungen.first?.neu == "68")
        #expect(b.gespraeche.first?.id == g.id)
        #expect(b.gespraechLaden(g.id)?.gespraech.beitraege.first?.bausteine == [.text("Wie läuft es?")])
    }
}
