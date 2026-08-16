/*
 * Pumpensteuerung eines Heizkreises.
 *
 * Reines Rechenmodul ohne ESP-IDF-Bezug, wie valve und roomctrl: die Zeit
 * kommt als Parameter herein, es wird nichts geschaltet und nichts
 * protokolliert. Damit laesst sich das Verhalten in test/host gegen
 * aufgezeichnete Verlaeufe pruefen, statt es an der Anlage auszuprobieren.
 *
 * Grundgedanke: Die Pumpe laeuft, solange ein Abnehmer da ist und der
 * Speicher warm genug ist. Fehlt der Bedarf, laeuft sie nach und geht dann
 * aus. Mindestlaufzeit und Mindestpause verhindern kurzes Takten, ein
 * Schutzlauf verhindert das Festsitzen im Sommer, und der Frostschutz hat
 * Vorrang vor allem.
 *
 * Bewusst so festgelegt: Faellt die Verbindung zu einem Verteiler aus, gilt
 * Bedarf als vorhanden. Eine laufende Pumpe gegen geschlossene Ventile ist
 * verschwenderisch, aber unschaedlich; eine stehende Pumpe bei Waermebedarf
 * ist es nicht.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PUMP_MODE_AUTO = 0,
    PUMP_MODE_ON,   /* dauerhaft ein, Handbetrieb */
    PUMP_MODE_OFF,  /* dauerhaft aus, Handbetrieb */
} pump_mode_t;

/* Warum die Pumpe gerade laeuft oder steht. Die Oberflaeche zeigt das an,
 * damit nachvollziehbar bleibt, wer geschaltet hat. */
typedef enum {
    PUMP_REASON_NONE = 0,
    PUMP_REASON_DEMAND,      /* Abnehmer vorhanden */
    PUMP_REASON_OVERRUN,     /* Nachlauf nach dem letzten Bedarf */
    PUMP_REASON_MIN_RUN,     /* Mindestlaufzeit noch nicht erreicht */
    PUMP_REASON_MIN_PAUSE,   /* Mindestpause noch nicht abgelaufen */
    PUMP_REASON_FROST,       /* Frostschutz */
    PUMP_REASON_SEIZE,       /* Schutzlauf gegen Festsitzen */
    PUMP_REASON_NO_DEMAND,   /* kein Abnehmer */
    PUMP_REASON_BUFFER_COLD, /* Speicher zu kalt */
    PUMP_REASON_MANUAL,      /* Handbetrieb */
} pump_reason_t;

typedef struct {
    uint32_t overrun_s;   /* Nachlauf nach dem letzten Bedarf */
    uint32_t min_run_s;   /* Mindestlaufzeit */
    uint32_t min_pause_s; /* Mindestpause */
    float min_buffer_c;   /* darunter bringt Umwaelzen nichts */
    float frost_c;        /* darunter laeuft die Pumpe in jedem Fall */
    uint32_t seize_days;  /* Schutzlauf nach so vielen Tagen Stillstand */
    uint32_t seize_run_s; /* Dauer des Schutzlaufs */
} pump_cfg_t;

typedef struct {
    bool demand;         /* Abnehmer vorhanden oder Verteiler nicht erreichbar */
    bool buffer_valid;   /* Speichertemperatur liegt vor */
    float buffer_c;
    bool room_valid;     /* kaelteste Raumtemperatur liegt vor */
    float min_room_c;
    bool flow_valid;     /* Vorlauftemperatur des Kreises liegt vor */
    float flow_c;
} pump_input_t;

typedef struct {
    pump_mode_t mode;
    bool on;
    pump_reason_t reason;

    /* Zeitpunkte in Millisekunden seit dem Start, 0 = noch nie. */
    uint32_t since_ms;       /* letzter Zustandswechsel */
    uint32_t last_demand_ms; /* letzter Zeitpunkt mit Bedarf */
    uint32_t last_run_ms;    /* letzter Zeitpunkt mit laufender Pumpe */
    uint32_t seize_until_ms; /* laufender Schutzlauf endet */
    bool started;            /* erste Auswertung hat stattgefunden */
    /* Mindestlaufzeit und Mindestpause gelten erst, nachdem wirklich einmal
     * geschaltet wurde. Sonst spraeche die Mindestpause schon gegen den
     * allerersten Anlauf nach dem Einschalten, und ein Wechsel der
     * Betriebsart muesste sie abwarten. */
    bool switched;
} pump_state_t;

/* Vorgabewerte: 5 min Nachlauf, je 3 min Mindestlaufzeit und -pause,
 * 40 °C Mindesttemperatur des Speichers, 6 °C Frostgrenze, Schutzlauf alle
 * sieben Tage fuer drei Minuten. */
void pump_defaults(pump_cfg_t *cfg);

void pump_init(pump_state_t *st, pump_mode_t mode);
void pump_set_mode(pump_state_t *st, pump_mode_t mode, uint32_t now_ms);

/* Ein Rechenschritt. now_ms muss monoton wachsen. */
void pump_tick(pump_state_t *st, const pump_cfg_t *cfg, const pump_input_t *in, uint32_t now_ms);

/* Klartext des Grundes, fuer Oberflaeche und Protokoll. */
const char *pump_reason_text(pump_reason_t r);

/*
 * Auswertung der Verteilerantworten.
 *
 * seen  - hat der Verteiler seit dem Start ueberhaupt schon geantwortet?
 * age_s - wie lange ist die letzte Antwort her?
 *
 * Ein Verteiler, der nie geantwortet hat, wird nicht gewertet: sonst liefe
 * die Pumpe wegen eines dauerhaft abgeschalteten Geraets durchgehend. Einer,
 * der frueher geantwortet hat und jetzt schweigt, gilt nach timeout_s als
 * bedarfsmeldend.
 */
typedef struct {
    bool seen;
    bool demand;
    uint32_t age_s;
    bool room_valid;
    float min_room_c;
} demand_source_t;

typedef struct {
    bool demand;
    bool stale;        /* mindestens eine Quelle ist verstummt */
    bool any_seen;     /* mindestens eine Quelle hat je geantwortet */
    bool room_valid;
    float min_room_c;
} demand_result_t;

void demand_evaluate(const demand_source_t *src, uint32_t count, uint32_t timeout_s,
                     demand_result_t *out);

/*
 * Brennererkennung am Abgasfuehler.
 *
 * Der Fuehler sitzt aussen am Rohr und liefert ein gedaempftes, verzoegertes
 * Signal. Ein fester Schwellwert waere von der Raumtemperatur des
 * Heizungsraums abhaengig -- im Sommer stuende er zu tief, im Winter zu hoch.
 * Gemessen wird deshalb gegen eine gleitende Bezugslinie: das Minimum des
 * Abgasfuehlers ueber die letzten 24 Stunden. Das ist die Temperatur des
 * kalten Rohrs und wandert mit der Jahreszeit mit.
 *
 * Die Haltezeiten unterscheiden einen Brennerlauf von einer Stoerung im
 * Messwert. Sie sind unsymmetrisch: das Anlaufen soll rasch erkannt werden,
 * das Abschalten erst, wenn das Rohr wirklich abkuehlt.
 */
typedef struct {
    float delta_on_k;    /* ueber der Bezugslinie gilt der Brenner als laufend */
    float delta_off_k;   /* darunter als aus */
    uint32_t on_hold_s;  /* so lange muss die Bedingung anhalten */
    uint32_t off_hold_s;
    float duese_l_h;     /* Duesendurchsatz, 0 = keine Verbrauchsschaetzung */
} burner_cfg_t;

typedef struct {
    bool known;          /* ein Abgaswert liegt vor */
    bool running;
    float baseline_c;    /* gleitende Bezugslinie */
    float abgas_c;
    uint32_t since_ms;   /* letzter Zustandswechsel */

    /* Tageswerte. Sie werden von der Anwendung zum Tageswechsel gesichert und
     * zurueckgesetzt; dieses Modul zaehlt nur. */
    uint32_t runtime_today_s;
    uint32_t starts_today;

    /* Innere Groessen. */
    uint32_t cond_since_ms; /* seit wann die Bedingung anliegt */
    bool cond;              /* welche Bedingung gerade anliegt */
    bool started;
    uint32_t last_ms;
    float min_seen_c;       /* kleinster Wert im laufenden Fenster */
    uint32_t window_ms;     /* Beginn des Fensters */
    uint32_t runtime_rest_ms; /* angebrochene Sekunde der Laufzeit */
} burner_state_t;

/* Vorgabe: 12 K ein, 6 K aus, 60 s und 300 s Haltezeit, 2,2 l/h. */
void burner_defaults(burner_cfg_t *cfg);

void burner_init(burner_state_t *st);

/*
 * Ein Rechenschritt. abgas_valid ist false, wenn kein Abgasfuehler zugeordnet
 * ist oder sein Wert fehlt -- dann bleibt der Zustand stehen und known ist
 * falsch.
 */
void burner_tick(burner_state_t *st, const burner_cfg_t *cfg, bool abgas_valid, float abgas_c,
                 uint32_t now_ms);

/* Verbrauchsschaetzung des Tages in Litern. */
float burner_litres_today(const burner_state_t *st, const burner_cfg_t *cfg);

/* Tageswechsel: Laufzeit und Starts zuruecksetzen. */
void burner_new_day(burner_state_t *st);

#ifdef __cplusplus
}
#endif
