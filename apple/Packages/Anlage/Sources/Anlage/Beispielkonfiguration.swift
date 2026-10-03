import Foundation
import Geraeteschnittstelle

/// Konfigurationen der Beispielanlage, damit die Formulare auch ohne Geräte etwas zeigen. Sie
/// entstehen aus den Vorgaben des Katalogs und den Werten der Beispielanlage, nicht aus echten
/// Sicherungen; Zugangsdaten enthalten sie nicht.
public enum Beispielkonfiguration {
    /// Vorgaben aller Einstellungen, die oben oder in Gruppen stehen
    static func vorgaben(_ art: Geraeteart) -> JSONWert {
        var k: JSONWert = [:]
        for p in Parameterkatalog.alle where p.geraet == art && p.ort == .geraet && !p.istKennwort {
            Schreibweg.setzen(&k, p.pfad, p.vorgabe, vorlage: k)
        }
        k["wifi"]?["ssid"] = "Heimnetz"
        k["wifi"]?["pass_set"] = true
        k["wifi"]?["ap_pass_set"] = true
        k["mqtt"]?["pass_set"] = false
        return k
    }

    public static func verteiler(_ etage: Etage) -> JSONWert {
        var k = vorgaben(.verteiler)
        k["site"] = .text(etage.name)
        k["wifi"]?["hostname"] = .text("floor-heating-\(etage.name.lowercased())")
        k["mqtt"]?["prefix"] = .text("fbh_\(etage.name.lowercased())")
        k["rooms"] = .liste(etage.raeume.map { r in
            [
                "id": .zahl(Double(r.nummer)), "name": .text(r.name),
                "channels": .liste(r.kanaele.map { .zahl(Double($0)) }),
                "sensor_mac": r.thermometer.map { .text(beispielMac($0)) } ?? .null,
                "mode": r.betriebsart == .aus ? "off" : "heat",
                "target_c": .zahl(r.soll), "p_band_k": .zahl(r.regelung.proportionalband),
                "interval_s": .zahl(Double(r.regelung.pruefintervall)), "min_delta": .zahl(r.regelung.mindestaenderung),
                "step": .zahl(r.regelung.raster),
            ]
        })
        k["channels"] = .liste(etage.kanaele.map { c in
            [
                "id": .zahl(Double(c.nummer)), "open_ms": .zahl((c.fahrzeitAuf * 1000).rounded()),
                "close_ms": .zahl((c.fahrzeitZu * 1000).rounded()), "max_ms": .zahl((c.maximal * 1000).rounded()),
                "blank_ms": .zahl((c.sperrzeit * 1000).rounded()), "bemf_mv": .zahl(Double(c.schwelle)),
                "bemf_hyst_mv": .zahl(Double(c.hysterese)), "calibrated": .bool(c.kalibriert),
                "bemf_group": .zahl(Double(c.gruppe)),
            ]
        })
        k["outdoor_mac"] = etage.traegtAussenfuehler ? "E5:2B:9C:41:07:AA" : .null
        return k
    }

    /// Kessel und Speicher der Beispielanlage; `kessel` wählt das Gerät.
    public static func heizung(_ bild: Anlagenbild, kessel: Bool) -> JSONWert {
        var k = vorgaben(.heizung)
        k["site"] = kessel ? "Kessel" : "Pufferspeicher"
        k["wifi"]?["hostname"] = kessel ? "heizung-kessel" : "heizung-pufferspeicher"
        if let s = bild.speicher {
            if let v = s.voll { k["buffer"]?["voll_c"] = .zahl(v) }
            if let v = s.leer { k["buffer"]?["leer_c"] = .zahl(v) }
            if let v = s.warngrenze { k["buffer"]?["warn_c"] = .zahl(v) }
        }
        if let d = bild.kessel?.duese { k["burner"]?["duese_l_h"] = .zahl(d) }
        let rollen = kessel ? ["kessel_vl", "kessel_rl", "abgas"] : ["puffer", "hk1_vl", "hk1_rl", "hk2_vl", "hk2_rl"]
        k["probes"] = .liste(rollen.enumerated().map { i, rolle in
            ["rom": .text(String(format: "28FF%02X1E80166%03X", i, kessel ? 0xA1 : 0xB2)), "role": .text(rolle),
             "name": .text(Parameterkatalog.rollenname(rolle)), "offset_k": rolle == "puffer" ? 2.6 : 0]
        })
        if kessel, let p = bild.kesselkreispumpe {
            k["boiler_pump"] = [
                "enabled": true, "mode": "auto", "topic": "", "host": .text(p.relais.adresse), "user": "",
                "pass_set": false, "relay": .zahl(Double(p.relais.kanal)), "on_k": .zahl(p.einschaltschwelle),
                "off_k": .zahl(p.ausschaltschwelle), "hold_s": .zahl(Double(p.haltezeit)), "min_run_s": 180,
                "min_pause_s": 180, "emergency_c": .zahl(p.notgrenze),
            ]
        }
        k["circuits"] = kessel ? [] : .liste(bild.heizkreise.map { c in
            [
                "id": .zahl(Double(c.nummer)), "name": .text(c.name), "enabled": true,
                "vl_role": .text("hk\(c.nummer)_vl"), "rl_role": .text("hk\(c.nummer)_rl"),
                "peers": .liste(c.versorgteVerteiler.compactMap { name in
                    bild.etagen.first { $0.name == name }.map { .text($0.id) }
                }),
                "pump": ["topic": "", "host": .text(c.relais.adresse), "user": "", "relay": .zahl(Double(c.relais.kanal)), "pass_set": false],
                "mode": "auto", "overrun_s": .zahl(Double(c.nachlauf)), "min_run_s": .zahl(Double(c.mindestlaufzeit)),
                "min_pause_s": .zahl(Double(c.mindestpause)), "min_buffer_c": .zahl(c.mindestSpeicher),
                "frost_c": .zahl(c.frostgrenze),
            ]
        })
        return k
    }

    /// Verfremdete Adresse aus dem Namen eines Thermometers, etwa `ATC_4F2A19` → `A4:C1:38:4F:2A:19`
    static func beispielMac(_ name: String) -> String {
        let hex = name.filter(\.isHexDigit).suffix(6)
        guard hex.count == 6 else { return "A4:C1:38:00:00:00" }
        let teile = stride(from: 0, to: 6, by: 2).map { i -> String in
            let a = hex.index(hex.startIndex, offsetBy: i)
            return String(hex[a...hex.index(after: a)])
        }
        return "A4:C1:38:" + teile.joined(separator: ":")
    }
}
