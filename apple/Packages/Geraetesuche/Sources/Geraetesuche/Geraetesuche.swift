import Foundation
import Geraeteschnittstelle
import Network
import Observation

/// Ein per Bonjour gefundenes Gerät.
public struct GefundenesGeraet: Sendable, Hashable, Identifiable {
    /// Kennung aus dem TXT-Eintrag (`fbh_…`, `heiz_…`); fehlt sie, der Instanzname
    public var id: String
    /// Instanzname; die Firmware setzt dafür den Ort, solange einer eingetragen ist.
    public var name: String
    public var ort: String?
    public var art: Geraeteart?
    /// Rolle aus dem TXT-Eintrag; neben den beiden Gerätearten auch `station` für den Leitstand
    public var rolle: String? = nil
    /// HTTP-Adresse, sobald der Eintrag aufgelöst ist
    public var adresse: URL?
    public var endpunkt: NWEndpoint

    /// Der Leitstand ist kein Regelgerät und hat deshalb keine `art`.
    public var istLeitstand: Bool {
        Leitstand.istLeitstand(rolle: rolle, kennung: id)
    }

    /// Die Attrappen melden sich nur auf diesem Rechner an und zeigen auf 127.0.0.1.
    public var istAttrappe: Bool {
        adresse?.host() == "127.0.0.1"
    }
}

/// Sucht die Geräte im Heimnetz über Bonjour (`_fbhctrl._tcp`).
///
/// Die Firmware meldet sich unter ihrem Ort an und trägt Kennung, Ort und Rolle als TXT ein
/// (`components/peers/peers.c`). Ältere Firmware lässt die Rolle leer; dann entscheidet die
/// Kennung.
@MainActor
@Observable
public final class Geraetesuche {
    public enum Zustand: Sendable, Equatable {
        case ruht
        case sucht
        /// Unter iOS und macOS fragt das System beim ersten Suchen nach dem Zugriff auf das lokale
        /// Netzwerk; wurde er verweigert, findet die Suche nichts.
        case keinZugriff
        case fehler(String)
    }

    public static let dienst = "_fbhctrl._tcp"

    public private(set) var geraete: [GefundenesGeraet] = []
    public private(set) var zustand: Zustand = .ruht

    private var browser: NWBrowser?
    private var aufloesungen: [String: Task<Void, Never>] = [:]
    private let schlange = DispatchQueue(label: "de.simplytech.heizung.geraetesuche")

    public init() {}

    public func starten() {
        guard browser == nil else { return }
        let parameter = NWParameters()
        parameter.includePeerToPeer = false
        let browser = NWBrowser(for: .bonjourWithTXTRecord(type: Self.dienst, domain: nil), using: parameter)
        browser.stateUpdateHandler = { [weak self] neu in
            Task { @MainActor in self?.zustandGeaendert(neu) }
        }
        browser.browseResultsChangedHandler = { [weak self] ergebnisse, _ in
            Task { @MainActor in self?.ergebnisseGeaendert(ergebnisse) }
        }
        self.browser = browser
        zustand = .sucht
        browser.start(queue: schlange)
    }

    public func beenden() {
        browser?.cancel()
        browser = nil
        aufloesungen.values.forEach { $0.cancel() }
        aufloesungen.removeAll()
        zustand = .ruht
    }

    /// Sucht höchstens `dauer` lang und liefert die Geräte, deren Adresse sich auflösen ließ, also
    /// eine Verbindung annahmen. Endet früher, sobald alle `gesucht`en Kennungen aufgelöst sind.
    ///
    /// Jeder Aufruf beginnt mit einer neuen Suche. Eine laufende hielte die Adresse fest, die sie
    /// zu einem Eintrag einmal aufgelöst hat, auch wenn das Gerät inzwischen eine andere hat.
    public static func kurz(dauer: Duration = .seconds(6), gesucht: Set<String> = []) async -> [GefundenesGeraet] {
        let suche = Geraetesuche()
        suche.starten()
        defer { suche.beenden() }
        let ende = ContinuousClock.now + dauer
        while ContinuousClock.now < ende, !Task.isCancelled, suche.zustand != .keinZugriff {
            let aufgeloest = Set(suche.geraete.filter { $0.adresse != nil }.map(\.id))
            if !gesucht.isEmpty, gesucht.isSubset(of: aufgeloest) { break }
            try? await Task.sleep(for: .milliseconds(200))
        }
        return suche.geraete.filter { $0.adresse != nil }
    }

    private func zustandGeaendert(_ neu: NWBrowser.State) {
        switch neu {
        case .ready:
            zustand = .sucht
        case .waiting(let fehler), .failed(let fehler):
            if case .dns(let code) = fehler, code == DNSServiceErrorType(kDNSServiceErr_PolicyDenied) {
                zustand = .keinZugriff
            } else {
                zustand = .fehler(fehler.localizedDescription)
            }
        case .cancelled:
            zustand = .ruht
        default:
            break
        }
    }

    private func ergebnisseGeaendert(_ ergebnisse: Set<NWBrowser.Result>) {
        var neu: [GefundenesGeraet] = []
        for ergebnis in ergebnisse {
            guard case .service(let name, _, _, _) = ergebnis.endpoint else { continue }
            var txt: [String: String] = [:]
            if case .bonjour(let eintrag) = ergebnis.metadata {
                txt = eintrag.dictionary
            }
            var geraet = Self.auswerten(name: name, txt: txt, endpunkt: ergebnis.endpoint)
            geraet.adresse = geraete.first { $0.endpunkt == ergebnis.endpoint }?.adresse
            neu.append(geraet)
            if geraet.adresse == nil, aufloesungen[geraet.id] == nil {
                aufloesen(geraet)
            }
        }
        geraete = neu.sorted { ($0.art?.rawValue ?? "", $0.name) < ($1.art?.rawValue ?? "", $1.name) }
    }

    private func aufloesen(_ geraet: GefundenesGeraet) {
        aufloesungen[geraet.id] = Task { [weak self] in
            let adresse = await Self.adresse(von: geraet.endpunkt)
            guard let self else { return }
            self.aufloesungen[geraet.id] = nil
            if let adresse, let i = self.geraete.firstIndex(where: { $0.endpunkt == geraet.endpunkt }) {
                self.geraete[i].adresse = adresse
            }
        }
    }

    // MARK: Auswertung

    /// Macht aus Instanzname und TXT-Eintrag ein Gerät.
    public nonisolated static func auswerten(name: String, txt: [String: String], endpunkt: NWEndpoint) -> GefundenesGeraet {
        let kennung = txt["id"].flatMap { $0.isEmpty ? nil : $0 }
        let ort = txt["site"].flatMap { $0.isEmpty ? nil : $0 }
        let rolle = txt["role"].flatMap { $0.isEmpty ? nil : $0 }
        let art = rolle.flatMap(Geraeteart.init(rawValue:)) ?? kennung.flatMap(Geraeteart.init(kennung:))
        return GefundenesGeraet(id: kennung ?? name, name: name, ort: ort, art: art, rolle: rolle, adresse: nil, endpunkt: endpunkt)
    }

    /// Löst einen Bonjour-Eintrag zu einer HTTP-Adresse auf, indem eine Verbindung aufgebaut und
    /// die Gegenstelle abgelesen wird. Erzwungen wird IPv4: Die Geräte kündigen IPv4 an, und
    /// link-lokale IPv6-Adressen taugen schlecht für eine URL.
    public nonisolated static func adresse(von endpunkt: NWEndpoint, zeitgrenze: Duration = .seconds(4)) async -> URL? {
        let parameter = NWParameters.tcp
        if let ip = parameter.defaultProtocolStack.internetProtocol as? NWProtocolIP.Options {
            ip.version = .v4
        }
        let verbindung = NWConnection(to: endpunkt, using: parameter)
        let einmal = Einmal()
        return await withCheckedContinuation { fortsetzung in
            verbindung.stateUpdateHandler = { zustand in
                switch zustand {
                case .ready:
                    let url = verbindung.currentPath?.remoteEndpoint.flatMap(Self.url(aus:))
                    if einmal.zuerst() { fortsetzung.resume(returning: url) }
                    verbindung.cancel()
                case .failed, .cancelled:
                    if einmal.zuerst() { fortsetzung.resume(returning: nil) }
                default:
                    break
                }
            }
            verbindung.start(queue: .global(qos: .userInitiated))
            Task {
                try? await Task.sleep(for: zeitgrenze)
                verbindung.cancel()
            }
        }
    }

    /// Die Beschreibung einer aufgelösten Adresse trägt die Schnittstelle (`192.168.1.213%en0`).
    /// Für IPv4 wird sie weggelassen, für IPv6 als Zone kodiert, wie es eine URL verlangt.
    nonisolated static func url(aus endpunkt: NWEndpoint) -> URL? {
        guard case .hostPort(let host, let port) = endpunkt else { return nil }
        let text: String = switch host {
        case .ipv4(let a): a.rawValue.map(String.init).joined(separator: ".")
        case .ipv6(let a): "[\("\(a)".replacingOccurrences(of: "%", with: "%25"))]"
        case .name(let n, _): n
        @unknown default: ""
        }
        return text.isEmpty ? nil : URL(string: "http://\(text):\(port.rawValue)")
    }
}

/// Stellt sicher, dass eine Fortsetzung genau einmal fortgesetzt wird.
private final class Einmal: @unchecked Sendable {
    private let sperre = NSLock()
    private var erledigt = false

    func zuerst() -> Bool {
        sperre.withLock {
            defer { erledigt = true }
            return !erledigt
        }
    }
}
