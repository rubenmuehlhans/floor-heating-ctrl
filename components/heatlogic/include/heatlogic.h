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
    /*
     * Der woechentliche Termin faellt gerade. Gemessen wird er an der Uhr
     * (components/schedule), nicht an der Laufzeit: ein Geraet mit taeglichem
     * Neustart erreicht nie sieben Tage Laufzeit, und nach jedem Stromausfall
     * finge eine Standzeitzaehlung von vorn an.
     */
    bool seize_due;
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
 * 40 °C Mindesttemperatur des Speichers, 6 °C Frostgrenze, Schutzlauf drei
 * Minuten lang. Wann er faellt, entscheidet der Termin von aussen. */
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
 *
 * Das Anlaufen wird an einer gleitenden Bezugslinie erkannt: dem Minimum des
 * Abgasfuehlers ueber die letzten 24 Stunden, also der Temperatur des kalten
 * Rohrs. Sie wandert mit der Jahreszeit mit und traegt fuer diese Richtung
 * zuverlaessig.
 *
 * Fuer das Abschalten taugt sie nicht. Ein warmer Kessel haelt das Rohr
 * dauerhaft sieben bis acht Kelvin ueber dem Nachtminimum -- der Brenner galt
 * damit stundenlang als laufend, obwohl er es nicht war. Gemessen wird deshalb
 * am Abfall: Das Abgas steigt, solange die Flamme brennt, und faellt sofort,
 * wenn sie aus ist. Faellt es um swing_k unter den Hoechstwert dieser Fahrt,
 * ist der Brenner aus; steigt es danach um swing_k ueber den Tiefstwert, laeuft
 * er wieder.
 *
 * Ein Vergleich mit dem Kesselvorlauf waere naheliegend -- das Abgas ist nur
 * waermer als das Wasser, solange es brennt -- und trug beim kalten Start auch.
 * Bei laufender Pumpe steht der Kessel aber schon auf Speichertemperatur,
 * waehrend das Rohr erst anfaengt, warm zu werden; dann liegt das Abgas
 * waehrend des Brennens weit unter dem Vorlauf. Der Vergleich beschreibt also
 * nur den kalten Start und wurde wieder verworfen.
 *
 * Die Haltezeiten unterscheiden einen Brennerlauf von einer Stoerung im
 * Messwert. Sie sind unsymmetrisch: das Anlaufen soll rasch erkannt werden,
 * das Abschalten erst, wenn das Rohr wirklich abkuehlt.
 */
typedef struct {
    float delta_on_k;    /* so weit ueber der Bezugslinie gilt er als laufend */
    float delta_off_k;   /* so weit darueber gilt er wieder als aus */
    /* So weit unter dem Hoechstwert der Fahrt gilt er als aus -- und so weit
     * ueber dem Tiefstwert danach wieder als angelaufen. */
    float swing_k;
    uint32_t on_hold_s;  /* so lange muss die Bedingung anhalten */
    uint32_t off_hold_s;
    float duese_l_h;     /* Duesendurchsatz, 0 = keine Verbrauchsschaetzung */
} burner_cfg_t;

typedef struct {
    bool abgas_valid;
    float abgas_c;
} burner_input_t;

typedef struct {
    bool known;          /* ein Abgaswert liegt vor */
    bool running;
    float baseline_c;    /* gleitende Bezugslinie */
    float abgas_c;
    float extreme_c;     /* Hoechst- bzw. Tiefstwert seit dem letzten Wechsel */
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

/* Vorgabe: 12 K ueber der Bezugslinie ein, 6 K aus, 6 K Ausschlag gegen den
 * Extremwert, 60 s und 300 s Haltezeit, 2,2 l/h. */
void burner_defaults(burner_cfg_t *cfg);

void burner_init(burner_state_t *st);

/*
 * Ein Rechenschritt. Ohne gueltigen Abgaswert bleibt der Zustand stehen und
 * known ist falsch.
 */
void burner_tick(burner_state_t *st, const burner_cfg_t *cfg, const burner_input_t *in,
                 uint32_t now_ms);

/* Verbrauchsschaetzung des Tages in Litern. */
float burner_litres_today(const burner_state_t *st, const burner_cfg_t *cfg);

/* Tageswechsel: Laufzeit und Starts zuruecksetzen. */
void burner_new_day(burner_state_t *st);

/*
 * Ladezustand des Pufferspeichers.
 *
 * Mit einem einzigen, ungeschickt sitzenden Fuehler ist keine belastbare
 * Aussage moeglich, und der Mischer verhindert, dass die Heizkreisfuehler
 * ergaenzend herangezogen werden koennen. Ausgewertet wird deshalb zweierlei:
 *
 *   - Der Kessel. Waehrend der Ladung ist der Ruecklauf kalt; naehert er sich
 *     dem Vorlauf, nimmt der Speicher keine Waerme mehr auf. Ebenso, wenn der
 *     Brenner bei hohem Vorlauf von selbst abschaltet -- dann hat die eigene
 *     Regelung des Kessels entschieden.
 *   - Der Pufferfuehler. Er liefert eine lineare Schaetzung zwischen leer und
 *     voll. Sie wird ausdruecklich als Schaetzung ausgewiesen.
 *
 * Fehlen die Kesselwerte, weil das Geraet am Kessel nicht erreichbar ist,
 * bleibt nur die Schaetzung. Das meldet limited.
 */
typedef struct {
    float spread_full_k;    /* darunter gilt der Speicher als voll */
    uint32_t spread_hold_s; /* so lange muss die Bedingung anhalten */
    float voll_c;           /* Pufferwert, der 100 Prozent entspricht */
    float leer_c;           /* Pufferwert, der 0 Prozent entspricht */
    float warn_c;           /* darunter wird das Warmwasser knapp */
    float kessel_hot_c;     /* darueber gilt der Kesselvorlauf als heiss */
} charge_cfg_t;

typedef enum {
    CHARGE_UNKNOWN = 0,
    CHARGE_IDLE,    /* Brenner aus, keine Ladung im Gange */
    CHARGE_LOADING, /* Brenner laeuft, der Speicher nimmt auf */
    CHARGE_FULL,    /* fertig geladen */
} charge_phase_t;

typedef struct {
    bool burner_known;
    bool burner_running;
    bool kessel_valid;
    float kessel_vl_c;
    float kessel_rl_c;
    bool puffer_valid;
    float puffer_c;
} charge_input_t;

typedef struct {
    charge_phase_t phase;
    bool limited;      /* ohne Kesselwerte nur die Schaetzung */
    bool level_valid;
    float level;       /* 0 bis 1 */
    bool warn_dhw;     /* Warmwasserreserve knapp */
    bool spread_valid;
    float spread_k;
    uint32_t since_ms; /* letzter Wechsel der Phase */

    /* Innere Groessen. */
    bool started;
    bool cond;
    uint32_t cond_since_ms;
    bool last_burner_running;
    bool had_burner;
} charge_state_t;

/* Vorgabe: 8 K Spreizung, 5 min Haltezeit, 62/35 Grad, Warnung unter 40,
 * Kesselvorlauf ab 60 Grad als heiss. */
void charge_defaults(charge_cfg_t *cfg);
void charge_init(charge_state_t *st);
void charge_tick(charge_state_t *st, const charge_cfg_t *cfg, const charge_input_t *in,
                 uint32_t now_ms);
const char *charge_phase_text(charge_phase_t p);

/* ------------------------------------------------------------------ */
/* Ausloesen der Aufzeichnung                                          */
/* ------------------------------------------------------------------ */

/*
 * Wann eine Ladung anfaengt, sieht man von aussen erst an der steigenden
 * Abgastemperatur. Von Hand ist der Anfang der Kurve deshalb kaum zu treffen.
 * Scharf geschaltet beginnt die Aufzeichnung von selbst und endet auch von
 * selbst.
 *
 * Ausgeloest wird ueber den Brennerzustand, wenn er bekannt ist -- er kommt
 * vom eigenen Abgasfuehler oder vom Geraet am Kessel. Ist er es nicht, dient
 * die steigende Speichertemperatur als Ersatz: das Geraet am Pufferspeicher
 * sieht den Brenner nicht, wohl aber, dass geladen wird. Der Ersatz spricht
 * einige Minuten spaeter an, weil die Waerme erst im Speicher ankommen muss.
 *
 * Hier steht nur, wann begonnen und wann beendet wird. Das Belegen des
 * Speichers und das Schreiben der Zeilen bleiben in der Anwendung.
 */
typedef enum {
    REC_TRIG_OFF = 0,
    REC_TRIG_ARMED,   /* wartet auf den Beginn einer Ladung */
    REC_TRIG_RUN,
    REC_TRIG_DONE,
} rec_trig_phase_t;

/* Woran der Beginn erkannt wird. */
typedef enum {
    REC_SRC_NONE = 0,   /* kein Zeichen vorhanden, scharf schalten waere sinnlos */
    REC_SRC_BURNER,
    REC_SRC_BUFFER,
} rec_source_t;

typedef struct {
    bool burner_known;
    bool burner_running;
    bool buffer_valid;
    float buffer_c;
} rec_input_t;

typedef struct {
    float rise_k;         /* so viel Anstieg gilt als beginnende Ladung */
    uint32_t rise_win_s;  /* im Fenster dieser Laenge */
    uint32_t quiet_s;     /* so lange kein neuer Hoechstwert: die Ladung ist durch */
    uint32_t tail_s;      /* Nachlauf nach dem Brennerlauf */
} rec_trig_cfg_t;

typedef struct {
    rec_trig_phase_t phase;
    rec_source_t source;  /* was diese Aufzeichnung ausgeloest hat */
    bool automatic;       /* selbsttaetig ausgeloest, endet auch von selbst */
    bool wait_off;        /* scharf, aber der Brenner laeuft noch aus dem vorigen Lauf */
    bool tail;            /* Brenner ist aus, der Nachlauf laeuft noch */
    uint32_t tail_since_ms;

    /*
     * Bezugswerte fuer die Speichertemperatur. Der untere folgt einem
     * Ruecklauf sofort und wandert sonst mit dem Fenster weiter, der obere
     * haelt den Hoechstwert -- dieselbe Bauart wie die Bezugslinie der
     * Brennererkennung, und ohne Ringpuffer.
     */
    bool ref_have;
    float ref_c;
    uint32_t ref_ms;
    float peak_c;
    uint32_t peak_ms;
} rec_trigger_t;

/* Vorgabe: 1,5 K Anstieg in 20 Minuten, 15 Minuten Ruhe als Ende, 10 Minuten
 * Nachlauf. Eine Ladung hebt den Speicher um ein Vielfaches davon; ein
 * Messrauschen oder die Erwaermung des Aufstellraums bleiben darunter. */
void rec_trigger_defaults(rec_trig_cfg_t *cfg);
void rec_trigger_init(rec_trigger_t *st);

/* Woran sich der Beginn mit den vorliegenden Messwerten erkennen liesse. */
rec_source_t rec_trigger_source(const rec_input_t *in);

/*
 * Schaltet scharf. Laeuft der Brenner in diesem Augenblick schon, wird nicht
 * mitten hinein begonnen, sondern der naechste Start abgewartet: eine halbe
 * Kurve taugt zur Auswertung nicht. Ueber die Speichertemperatur laesst sich
 * das nicht unterscheiden -- dort zaehlt der naechste Anstieg, gemessen ab
 * dem Wert beim Scharfschalten.
 */
void rec_trigger_arm(rec_trigger_t *st, const rec_input_t *in, uint32_t now_ms);

/* Beginnt sofort. Eine so begonnene Aufzeichnung endet nicht von selbst. */
void rec_trigger_manual(rec_trigger_t *st);

/* Beendet eine laufende und verwirft eine scharf geschaltete Aufzeichnung. */
void rec_trigger_stop(rec_trigger_t *st);

/* Der Puffer ist voll: die Aufzeichnung ist damit fertig. */
void rec_trigger_full(rec_trigger_t *st);

/*
 * Ein Zeitschritt. Liefert true in dem Augenblick, in dem die Aufzeichnung
 * beginnt; die Anwendung setzt dann ihren Zeitstempel und faengt bei Zeile
 * null an.
 */
bool rec_trigger_tick(rec_trigger_t *st, const rec_input_t *in, const rec_trig_cfg_t *cfg,
                      uint32_t now_ms);

/* Verbleibender Nachlauf in Sekunden, 0 wenn keiner laeuft. */
uint32_t rec_trigger_tail_left_s(const rec_trigger_t *st, uint32_t tail_s, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
