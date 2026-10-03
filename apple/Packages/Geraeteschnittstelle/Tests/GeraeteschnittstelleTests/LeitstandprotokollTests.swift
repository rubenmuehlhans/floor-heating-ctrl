import Foundation
import Testing
@testable import Geraeteschnittstelle

/// Protokoll des Leitstands: Dateien wachsen nur, die App holt sie ab der bekannten Stelle.
struct LeitstandprotokollTests {
    private let datei = Data("zeit,a\n2026-09-23T00:00:00Z,1\n2026-09-23T00:05:00Z,2\n".utf8)

    private func leitstand(_ host: String, _ tabelle: @escaping @Sendable (Netzattrappe.Eingang) -> Netzattrappe.Antwort) -> Leitstand {
        Leitstand(adresse: URL(string: "http://\(host)")!, sitzung: Netzattrappe.sitzung(host: host, tabelle))
    }

    @Test func rangeLiefertNurDenRest() async throws {
        let datei = datei
        let l = leitstand("range.test") { e in
            guard let range = e.kopf["Range"] else { return .init(rumpf: datei) }
            let ab = Int(range.dropFirst("bytes=".count).dropLast())!
            return ab >= datei.count
                ? .init(status: 416, rumpf: Data())
                : .init(status: 206, rumpf: datei.subdata(in: ab..<datei.count))
        }
        #expect(try await l.protokollDatei(tag: "2026-09-23", name: "x.5min.csv") == datei)
        let rest = try await l.protokollDatei(tag: "2026-09-23", name: "x.5min.csv", ab: 7)
        #expect(rest == datei.subdata(in: 7..<datei.count))
        // Am Ende der Datei antwortet der Leitstand mit 416: nichts Neues, kein Fehler.
        #expect(try await l.protokollDatei(tag: "2026-09-23", name: "x.5min.csv", ab: datei.count).isEmpty)
        let e = Netzattrappe.eingaenge(host: "range.test")
        #expect(e.map { $0.kopf["Range"] } == [nil, "bytes=7-", "bytes=\(datei.count)-"])
        #expect(e[0].pfad == "/log/2026-09-23/x.5min.csv")
    }

    /// Ein Gerät, das Range nicht kennt, schickt die ganze Datei; die App schneidet selbst.
    @Test func ohneRangeWirdGeschnitten() async throws {
        let datei = datei
        let l = leitstand("ganz.test") { _ in .init(rumpf: datei) }
        #expect(try await l.protokollDatei(tag: "2026-09-23", name: "x.5min.csv", ab: 7) == datei.subdata(in: 7..<datei.count))
        #expect(try await l.protokollDatei(tag: "2026-09-23", name: "x.5min.csv", ab: datei.count + 5).isEmpty)
    }

    @Test func tageUndDateien() async throws {
        let l = leitstand("tage.test") { e in
            switch e.pfad {
            case "/api/log/days": return .init(rumpf: Data(#"{"tage":["2026-09-23","2026-09-22"]}"#.utf8))
            case "/api/log/days?tag=2026-09-23":
                return .init(rumpf: Data(#"{"tag":"2026-09-23","dateien":[{"name":"heiz_1.5min.csv","byte":120},{"name":"heiz_1.2.5min.csv","byte":40},{"name":"heiz_1.csv","byte":900}]}"#.utf8))
            default: return .init(status: 404, rumpf: Data())
            }
        }
        #expect(try await l.protokollTage() == ["2026-09-22", "2026-09-23"])
        let d = try await l.protokollDateien(tag: "2026-09-23")
        #expect(d.map(\.mittelFuer) == ["heiz_1", "heiz_1", nil])
    }

    @Test func reiheVomLeitstand() async throws {
        let l = leitstand("feinverlauf.test") { e in
            .init(rumpf: Data(#"{"geraet":"heiz_1","raster_s":30,"spalten":["geraet.heap","funk.A4:C1:38:77:88:99.temp"],"zeilen":[["2026-09-23T05:50:00Z",41234,null],["2026-09-23T05:50:30Z",40100.5,21.4]]}"#.utf8))
        }
        let von = Date(timeIntervalSince1970: 1_790_142_600)
        let r = try await l.reihe(geraet: "heiz_1", schluessel: ["geraet.heap", "funk.A4:C1:38:77:88:99.temp"],
                                  von: von, bis: von.addingTimeInterval(3600), raster: 30)
        #expect(r.raster == 30 && r.spalten.count == 2 && r.zeilen.count == 2)
        #expect(r.zeilen[0].werte == [41234, nil])
        #expect(r.zeilen[1].werte == [40100.5, 21.4])
        let pfad = Netzattrappe.eingaenge(host: "feinverlauf.test")[0].pfad
        #expect(pfad.contains("schluessel=geraet.heap,funk.A4:C1:38:77:88:99.temp"))
        #expect(pfad.contains("von=1790142600&bis=1790146200&raster=30"))
    }

    @Test func homekitImZustand() async throws {
        let z = try JSONDecoder().decode(Leitstandzustand.self, from: Data(#"{"homekit":{"active":true,"controllers":2,"accessories":9}}"#.utf8))
        #expect(z.homekit?.aktiv == true && z.homekit?.steuerungen == 2 && z.homekit?.zubehoer == 9)
        let basic = try JSONDecoder().decode(Leitstandzustand.self, from: Data(#"{"homekit":{"active":false,"reason":"HomeKit braucht PSRAM","controllers":0,"accessories":0}}"#.utf8))
        #expect(basic.homekit?.grund == "HomeKit braucht PSRAM")
        let alt = try JSONDecoder().decode(Leitstandzustand.self, from: Data("{}".utf8))
        #expect(alt.homekit == nil, "ältere Firmware ohne HomeKit")

        let l = leitstand("homekit.test") { _ in .init(status: 202) }
        try await l.kopplungenLoeschen()
        let e = Netzattrappe.eingaenge(host: "homekit.test")
        #expect(e.first?.pfad == "/api/homekit/reset")
        #expect(try JSONWert.lesen(e.first!.rumpf) == ["bestaetigung": "KOPPLUNGEN LOESCHEN"])
    }
}
