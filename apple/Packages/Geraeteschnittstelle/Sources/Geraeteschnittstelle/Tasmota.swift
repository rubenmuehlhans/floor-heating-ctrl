import Foundation

/// Zugang zu einem Tasmota-Relais, nur zur Fehlersuche. Geschaltet wird es vom Heizungsgerät,
/// nicht von der App.
///
/// Die Ausfallregel aus dem Handbuch („Absicherung am Relais“) schaltet die Pumpe ein, wenn das
/// Lebenszeichen `Var1` des Heizungsgeräts eine Viertelstunde ausbleibt. Das Lebenszeichen
/// senden die Heizungsgeräte nur an die Relais der Heizkreise.
public struct Tasmota: Sendable {
    public let verbindung: Geraeteverbindung
    let benutzer: String?

    public init(adresse: URL, benutzer: String? = nil, sitzung: URLSession = .geraete) {
        verbindung = Geraeteverbindung(basis: adresse, sitzung: sitzung)
        self.benutzer = benutzer.flatMap { $0.isEmpty ? nil : $0 }
    }

    /// Adresse aus der Konfiguration, mit oder ohne `http://`
    public static func adresse(_ host: String) -> URL? {
        let h = host.trimmingCharacters(in: .whitespaces)
        guard !h.isEmpty else { return nil }
        return URL(string: h.contains("://") ? h : "http://\(h)")
    }

    public func befehl(_ cmnd: String) async throws -> JSONWert {
        var teile = URLComponents()
        teile.path = "/cm"
        teile.queryItems = (benutzer.map { [URLQueryItem(name: "user", value: $0)] } ?? []) + [URLQueryItem(name: "cmnd", value: cmnd)]
        return try await verbindung.holen(teile.string ?? "/cm", als: JSONWert.self)
    }

    public static func regeltext(relais: Int) -> String {
        "ON Var1#State DO RuleTimer1 900 ENDON ON Rules#Timer=1 DO Power\(relais) 1 ENDON"
    }

    public struct Pruefung: Sendable, Equatable {
        /// Schaltzustand des Kanals
        public var ein: Bool?
        /// `PowerOnState`: 1 heißt, nach einem Stromausfall schaltet das Relais ein.
        public var einschaltzustand: Int?
        public var regelAktiv: Bool
        /// Die Regel enthält Lebenszeichen, Zeitgeber und das Einschalten dieses Kanals.
        public var regelPasst: Bool
        public var regel: String?

        public var inOrdnung: Bool { einschaltzustand == 1 && regelAktiv && regelPasst }
    }

    public func pruefen(relais: Int) async throws -> Pruefung {
        let schaltung = try await befehl("Power\(relais)")
        let ein = (schaltung["POWER\(relais)"] ?? schaltung["POWER"])?.alsText.map { $0 == "ON" }
        let zustand = try? await befehl("PowerOnState")
        let regel = try? await befehl("Rule1")
        let r = regel?["Rule1"]
        let text = r?["Rules"]?.alsText
        let kompakt = (text ?? "").uppercased().replacingOccurrences(of: " ", with: "")
        let passt = kompakt.contains("VAR1#STATE") && kompakt.contains("RULETIMER1") && kompakt.contains("POWER\(relais)1")
        return Pruefung(
            ein: ein,
            einschaltzustand: zustand?["PowerOnState"]?.alsGanzzahl ?? zustand?["PowerOnState"]?.alsText.flatMap(Int.init),
            regelAktiv: r?["State"]?.alsText == "ON",
            regelPasst: passt,
            regel: text)
    }

    /// Richtet die Ausfallregel ein, wie das Handbuch sie beschreibt.
    public func ausfallregelEinrichten(relais: Int) async throws {
        _ = try await befehl("PowerOnState 1")
        _ = try await befehl("Rule1 \(Self.regeltext(relais: relais))")
        _ = try await befehl("Rule1 1")
    }
}
