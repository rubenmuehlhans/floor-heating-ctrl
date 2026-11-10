#include "katalog.h"

namespace katalog {

using anlage::Art;

const std::vector<Parameter> &alle()
{
    static const std::vector<Parameter> P = {
        // Verteiler, je Raum
        {"raum.target_c", Art::verteiler, Ort::raum, "target_c", "Solltemperatur", "°C", 5, 35, 0.5, false, false, nullptr,
         "Raumtemperatur, auf die der Verteiler regelt; am Sollwert stehen die Ventile halb offen."},
        {"raum.mode", Art::verteiler, Ort::raum, "mode", "Betriebsart", "", 0, 0, 0, false, false, "heat=Regelung aktiv|off=ausgeschaltet",
         "„Ausgeschaltet“ fährt die Ventile des Raums zu; er meldet keinen Wärmebedarf."},
        {"raum.p_band_k", Art::verteiler, Ort::raum, "p_band_k", "Regelbereich", "K", 0.2, 10, 0.1, false, false, nullptr,
         "Abweichung vom Sollwert, bei der die Ventile ganz öffnen bzw. schließen."},
        {"raum.interval_s", Art::verteiler, Ort::raum, "interval_s", "Regelung prüft alle", "s", 5, 3600, 5, false, false, nullptr,
         "Abstand zwischen zwei Regelschritten des Raums."},
        {"raum.step", Art::verteiler, Ort::raum, "step", "Stellschritt der Ventile", "", 0.01, 0.5, 0.01, false, false, nullptr,
         "Die Ventilstellung wird auf Schritte dieser Größe gerundet; 0,1 entspricht etwa 4 s Motorlauf."},
        {"raum.min_delta", Art::verteiler, Ort::raum, "min_delta", "Kleinste Fahrt", "", 0, 0.5, 0.01, false, false, nullptr,
         "Kleinere Abweichungen zwischen Ziel und Stellung lösen keine Fahrt aus."},
        {"sensor_timeout_s", Art::verteiler, Ort::geraet, "sensor_timeout_s", "Thermometer gilt als ausgefallen nach", "s", 60, 43200, 60,
         false, false, nullptr, "Ist der letzte Messwert älter, setzt die Regelung des Raums aus."},
        {"seize_weekday", Art::verteiler, Ort::geraet, "seize_weekday", "Schutzfahrt, Wochentag", "", 0, 6, 1, false, false, nullptr,
         "0 = Sonntag … 6 = Samstag."},
        {"seize_hour", Art::verteiler, Ort::geraet, "seize_hour", "Schutzfahrt, Stunde", "Uhr", 0, 23, 1, false, false, nullptr,
         "Uhrzeit des Termins, Ortszeit."},
        // Heizungsgerät
        {"demand_poll_s", Art::heizung, Ort::geraet, "demand_poll_s", "Bedarf abfragen alle", "s", 1, 300, 1, false, false, nullptr,
         "Wie oft das Gerät die versorgten Verteiler nach Wärmebedarf fragt."},
        {"demand_timeout_s", Art::heizung, Ort::geraet, "demand_timeout_s", "Verteiler gilt als verstummt nach", "s", 10, 3600, 10, false,
         false, nullptr, "Danach gilt sein Kreis als mit Bedarf: Die Pumpe läuft im Zweifel."},
        {"burner.delta_on_k", Art::heizung, Ort::geraet, "burner.delta_on_k", "Start ab Abgas über Ruhewert", "K", 1, 100, 0.5, false,
         false, nullptr, "Der Brenner gilt als gestartet, wenn das Abgas so weit über seinem Ruhewert liegt."},
        {"burner.delta_off_k", Art::heizung, Ort::geraet, "burner.delta_off_k", "Ende ab Abgas über Ruhewert", "K", 0, 99, 0.5, false,
         false, nullptr, "Der Brenner gilt als aus, wenn das Abgas nur noch so weit über dem Ruhewert liegt."},
        {"burner.swing_k", Art::heizung, Ort::geraet, "burner.swing_k", "Abfall und Anstieg des Abgases", "K", 1, 60, 0.5, false, false,
         nullptr, "Abfall unter den Höchstwert des Laufs, ab dem der Brenner als aus gilt."},
        {"burner.on_hold_s", Art::heizung, Ort::geraet, "burner.on_hold_s", "Brennerstart bestätigt nach", "s", 0, 3600, 10, false, false,
         nullptr, "So lange muss die Startbedingung ununterbrochen anstehen."},
        {"burner.off_hold_s", Art::heizung, Ort::geraet, "burner.off_hold_s", "Brennerende bestätigt nach", "s", 0, 3600, 10, false,
         false, nullptr, "So lange muss das Abgas ununterbrochen fallen."},
        {"burner.duese_l_h", Art::heizung, Ort::geraet, "burner.duese_l_h", "Düsendurchsatz", "l/h", 0, 20, 0.05, false, false, nullptr,
         "Ölmenge je Stunde Brennerlaufzeit; daraus schätzt das Gerät den Verbrauch. Wirkt nicht auf die Regelung."},
        {"buffer.leer_c", Art::heizung, Ort::geraet, "buffer.leer_c", "Speicher leer bei", "°C", 15, 90, 0.5, false, true, nullptr,
         "Speichertemperatur, die als 0 % angezeigt wird. Schaltet nichts."},
        {"buffer.voll_c", Art::heizung, Ort::geraet, "buffer.voll_c", "Speicher voll bei", "°C", 20, 95, 0.5, false, true, nullptr,
         "Speichertemperatur, die als 100 % angezeigt wird; die Temperatur am Ende einer Ladung."},
        {"buffer.leer_lernen", Art::heizung, Ort::geraet, "buffer.leer_lernen", "Leerpunkt selbst nachmessen", "", 0, 0, 0, false, true,
         "true=an|false=aus", "Startet der Brenner nach Verbrauch, nimmt das Gerät die Speichertemperatur als Messpunkt."},
        {"buffer.lern_drop_k", Art::heizung, Ort::geraet, "buffer.lern_drop_k", "Nachmessen nur nach Abfall um", "K", 0.5, 30, 0.5, false,
         true, nullptr, "Ein Brennerstart zählt nur als Messpunkt, wenn der Speicher so weit gefallen ist."},
        {"buffer.warn_c", Art::heizung, Ort::geraet, "buffer.warn_c", "Warnung „Warmwasser knapp“ unter", "°C", 20, 80, 1, false, true,
         nullptr, "Nur eine Meldung."},
        {"buffer.volumen_l", Art::heizung, Ort::geraet, "buffer.volumen_l", "Speicherinhalt", "l", 0, 20000, 50, false, true, nullptr,
         "Rechnet Zapfungen und Ladungen in Kilowattstunden um; steuert nichts."},
        {"buffer.zapf_drop_k", Art::heizung, Ort::geraet, "buffer.zapf_drop_k", "Zapfung erkannt ab Abfall um", "K", 0.2, 40, 0.1, false,
         true, nullptr, "Abfall bei stehendem Brenner im Zeitfenster, der als Zapfung zählt."},
        {"buffer.zapf_win_s", Art::heizung, Ort::geraet, "buffer.zapf_win_s", "Zeitfenster der Zapfung", "s", 60, 7200, 60, false, true,
         nullptr, "In diesem Zeitraum muss der Abfall liegen."},
        {"buffer.spread_full_k", Art::heizung, Ort::geraet, "buffer.spread_full_k", "Ladung fertig unter Spreizung", "K", 1, 40, 0.5,
         false, true, nullptr, "Nur Anzeige und Protokoll."},
        {"buffer.spread_hold_s", Art::heizung, Ort::geraet, "buffer.spread_hold_s", "Ladung fertig bestätigt nach", "s", 0, 3600, 10,
         false, true, nullptr, "So lange muss die Spreizung ununterbrochen unterschritten sein."},
        {"buffer.kessel_hot_c", Art::heizung, Ort::geraet, "buffer.kessel_hot_c", "Ladung fertig erst ab Vorlauf", "°C", 20, 95, 1, false,
         true, nullptr, "Die Ladung gilt nur als fertig, wenn der Kesselvorlauf mindestens so warm ist."},
        {"boiler_pump.overrun_s", Art::heizung, Ort::geraet, "boiler_pump.overrun_s", "Kesselkreispumpe: Nachlauf nach Brennerende",
         "min", 0, 120, 1, true, false, nullptr, "So lange läuft die Pumpe nach dem Brennerende weiter."},
        {"boiler_pump.on_k", Art::heizung, Ort::geraet, "boiler_pump.on_k", "Kesselkreispumpe: Ein ab Kessel wärmer als Speicher um", "K",
         0.1, 20, 0.1, false, false, nullptr, "Gilt nur bei stehendem Brenner und nach dem Nachlauf."},
        {"boiler_pump.off_k", Art::heizung, Ort::geraet, "boiler_pump.off_k", "Kesselkreispumpe: Aus unter Spreizung", "K", -20, 19.9,
         0.1, false, false, nullptr, "Läuft die Pumpe, schaltet sie ab, sobald der Kessel keine Wärme mehr abgibt."},
        {"boiler_pump.hold_s", Art::heizung, Ort::geraet, "boiler_pump.hold_s", "Kesselkreispumpe: Schaltverzögerung", "s", 0, 3600, 10,
         false, false, nullptr, "So lange muss eine Schwelle über- oder unterschritten sein."},
        {"boiler_pump.min_run_s", Art::heizung, Ort::geraet, "boiler_pump.min_run_s", "Kesselkreispumpe: Mindestlaufzeit", "s", 0, 3600,
         10, false, false, nullptr, ""},
        {"boiler_pump.min_pause_s", Art::heizung, Ort::geraet, "boiler_pump.min_pause_s", "Kesselkreispumpe: Mindestpause", "s", 0, 3600,
         10, false, false, nullptr, ""},
        {"heizkreis.overrun_s", Art::heizung, Ort::heizkreis, "overrun_s", "Nachlauf nach Bedarfsende", "s", 0, 3600, 10, false, false,
         nullptr, "So lange läuft die Pumpe weiter, nachdem kein Verteiler mehr Wärme anfordert."},
        {"heizkreis.min_run_s", Art::heizung, Ort::heizkreis, "min_run_s", "Mindestlaufzeit", "s", 0, 3600, 10, false, false, nullptr, ""},
        {"heizkreis.min_pause_s", Art::heizung, Ort::heizkreis, "min_pause_s", "Mindestpause", "s", 0, 3600, 10, false, false, nullptr, ""},
        {"heizkreis.min_buffer_c", Art::heizung, Ort::heizkreis, "min_buffer_c", "Pumpe aus unter Speicher", "°C", 0, 90, 1, false, false,
         nullptr, "Ist der Speicher kälter, bleibt die Pumpe aus, auch bei Wärmebedarf."},
    };
    return P;
}

const Parameter *finden(const std::string &id)
{
    for (const auto &p : alle())
        if (id == p.id) return &p;
    return nullptr;
}

}  // namespace katalog
