import Foundation
import FoundationModels

/// Die Werkzeuge je Modell. Claude bekommt das Wissen in den Anweisungen und längere Tabellen;
/// die Apple-Modelle haben weniger Kontext und schlagen im Wissen nach.
public enum Werkzeugsatz {
    public static func claude(_ zugriff: any Anlagenzugriff) -> [any Tool] {
        [
            AnlagenstatusWerkzeug(zugriff: zugriff),
            BefundeWerkzeug(zugriff: zugriff),
            VerlaufWerkzeug(zugriff: zugriff),
            EreignisseWerkzeug(zugriff: zugriff),
            FeinverlaufWerkzeug(zugriff: zugriff),
            ProtokolleWerkzeug(zugriff: zugriff),
            KennzahlenWerkzeug(zugriff: zugriff),
            EinstellungenWerkzeug(zugriff: zugriff),
            AenderungVorschlagenWerkzeug(zugriff: zugriff),
            AktionVorschlagenWerkzeug(zugriff: zugriff),
        ]
    }

    public static func apple(_ zugriff: any Anlagenzugriff, wissen: Wissen) -> [any Tool] {
        [
            AnlagenstatusWerkzeug(zugriff: zugriff),
            BefundeWerkzeug(zugriff: zugriff),
            VerlaufWerkzeug(zugriff: zugriff, zeilen: 16, hoechstensReihen: 4),
            EreignisseWerkzeug(zugriff: zugriff, hoechstens: 20),
            FeinverlaufWerkzeug(zugriff: zugriff, zeilen: 24),
            ProtokolleWerkzeug(zugriff: zugriff),
            KennzahlenWerkzeug(zugriff: zugriff),
            EinstellungenWerkzeug(zugriff: zugriff),
            AenderungVorschlagenWerkzeug(zugriff: zugriff),
            AktionVorschlagenWerkzeug(zugriff: zugriff),
            WissenWerkzeug(wissen: wissen, laenge: 900),
        ]
    }
}
