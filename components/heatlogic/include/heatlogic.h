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

#ifdef __cplusplus
}
#endif
