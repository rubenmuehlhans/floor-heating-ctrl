/*
 * Kesselkreispumpe zwischen Kessel und Pufferspeicher.
 *
 * Reines Rechenmodul wie der Rest von heatlogic: die Zeit kommt als Parameter
 * herein, es wird nichts geschaltet und nichts protokolliert.
 *
 * Der Kessel gibt nur dann Waerme ab, wenn sein Vorlauf waermer ist als das,
 * was aus dem Speicher zurueckkommt. Kehrt sich das um -- der Brenner ist aus,
 * der Kessel kuehlt aus --, foerdert dieselbe Pumpe Waerme aus dem Speicher in
 * den Kessel, und von dort geht sie durch den Schornstein und an den
 * Heizungsraum verloren. Sie gehoert dann abgeschaltet.
 *
 * Zwei Dinge sind dabei wichtiger als das Sparen:
 *
 *   - Ohne gueltige Messwerte laeuft die Pumpe. Eine laufende Pumpe ohne Not
 *     ist verschwenderisch, ein heisser Kessel ohne Abfuhr ist es nicht.
 *   - Ueberschreitet der Kesselvorlauf die Notgrenze, laeuft sie ebenfalls,
 *     ganz gleich was die Spreizung sagt. Ein Fuehler, der klemmt, darf die
 *     Waermeabfuhr nicht verhindern.
 *
 * Verglichen wird der Kesselvorlauf bei laufender Pumpe mit dem eigenen
 * Ruecklauf -- er kommt aus dem Speicher, und die Differenz ist die Waerme,
 * die der Kessel tatsaechlich abgibt. Der Speicherfuehler taugt dafuer nicht:
 * Er sitzt an anderer Stelle und lag an der Anlage drei Kelvin unter dem
 * Wasser, das zurueckkam. Steht die Pumpe, fliesst nichts, Vor- und Ruecklauf
 * nehmen die Kesseltemperatur an, und nur der Speicher bleibt als Bezug --
 * zuzueglich des Abstands, um den der Ruecklauf zuletzt darueber lag.
 *
 * Beim Anlaufen des Brenners steht die Pumpe zunaechst: Der kalte Kessel
 * wuerde sonst den warmen Speicher abkuehlen. Sie springt an, sobald der
 * Vorlauf den Speicher um on_k ueberholt -- das ist zugleich die
 * Ruecklaufanhebung, die dem Kessel die Taupunktunterschreitung erspart.
 *
 * Solange der Brenner laeuft, laeuft sie in jedem Fall. Die Spreizung allein
 * sagt nicht, ob der Kessel Waerme abgibt: Am 23. September lag der
 * Kesselvorlauf mitten in einem Brennerlauf nur 0,9 K ueber dem
 * Speicherfuehler, die Regel las daraus "nichts abzugeben", und der Kessel
 * schaltete den Brenner nach gut zwoelf Minuten selbst ab -- sonst brennt er
 * eine Dreiviertelstunde. Auf die Notgrenze ist dabei kein Verlass: Steht die
 * Pumpe, sehen die Fuehler am Rohr den Kesselkoerper nicht.
 *
 * Die Brennererkennung meldet einen Start erst eine bis mehrere Minuten nach
 * dem Zuenden. Die Ruecklaufanhebung bleibt damit erhalten; ist die Pumpe bis
 * dahin nicht angesprungen, schaltet die Meldung sie ein. Aus geht sie erst,
 * wenn die Erkennung den Brenner als aus meldet, und dann wie sonst nach
 * Spreizung und Haltezeit.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Warum die Kesselkreispumpe laeuft oder steht. */
typedef enum {
    BP_REASON_NONE = 0,
    BP_REASON_TRANSFER,     /* der Kessel gibt Waerme ab */
    BP_REASON_BURNER,       /* der Brenner laeuft */
    BP_REASON_NO_TRANSFER,  /* Kessel kaum waermer als der Speicher (ersatzweise der Ruecklauf) */
    BP_REASON_EMERGENCY,    /* Notgrenze des Kesselvorlaufs ueberschritten */
    BP_REASON_NO_READING,   /* ohne Messwerte laeuft sie */
    BP_REASON_HOLD,         /* Bedingung hat gewechselt, Haltezeit laeuft noch */
    BP_REASON_MIN_RUN,
    BP_REASON_MIN_PAUSE,
    BP_REASON_MANUAL,
    BP_REASON_DISABLED,
} bp_reason_t;

typedef enum {
    BP_MODE_AUTO = 0,
    BP_MODE_ON,
    BP_MODE_OFF,
} bp_mode_t;

typedef struct {
    /* Ab dieser Spreizung gilt der Kessel als abgebend. */
    float on_k;
    /* Darunter gilt er als aufnehmend. Kleiner als on_k, sonst pendelt es. */
    float off_k;
    uint32_t hold_s;       /* so lange muss die Bedingung anliegen */
    uint32_t min_run_s;
    uint32_t min_pause_s;
    float emergency_c;     /* darueber laeuft sie in jedem Fall */
    bool enabled;
} bp_cfg_t;

typedef struct {
    bool valid;            /* beide Kesselfuehler liefern */
    float vl_c, rl_c;
    /*
     * Speichertemperatur, wenn sie vorliegt. Bezug bei stehender Pumpe: Dann
     * fliesst nichts, Vor- und Ruecklauf nehmen beide die Temperatur des
     * Kesselkoerpers an, und die Spreizung sagt nichts mehr. Ein Kessel, der
     * noch Waerme haelt, bliebe so unbemerkt stehen. Faellt der Wert aus --
     * er kommt vom Nachbargeraet --, gilt wieder der Ruecklauf.
     */
    bool buffer_valid;
    float buffer_c;
    /* Die Brennererkennung meldet den Brenner als laufend. Unbekannt zaehlt
     * als aus; dann entscheidet die Spreizung wie ohne diese Angabe. */
    bool burner_running;
} bp_input_t;

typedef struct {
    bp_mode_t mode;
    bool on;
    bp_reason_t reason;

    uint32_t since_ms;      /* letzter Zustandswechsel */
    bool started;
    bool switched;          /* es wurde wirklich schon einmal geschaltet */
    uint32_t cond_since_ms; /* seit wann die aktuelle Bedingung anliegt */
    bool cond_transfer;     /* welche Bedingung das ist */
    /* Um so viel lag der Ruecklauf bei laufender Pumpe zuletzt ueber dem
     * Speicherfuehler, 0 bis 10 K. Bei stehender Pumpe kommt er auf den
     * Speicher. */
    float buffer_bias_k;
} bp_state_t;

/* Vorgabe: drei Kelvin ein, zwei aus, zwei Minuten Haltezeit, je drei Minuten
 * Mindestlaufzeit und -pause, Notgrenze 85 Grad. */
void bp_defaults(bp_cfg_t *cfg);
void bp_init(bp_state_t *st, bp_mode_t mode);
void bp_set_mode(bp_state_t *st, bp_mode_t mode, uint32_t now_ms);
void bp_tick(bp_state_t *st, const bp_cfg_t *cfg, const bp_input_t *in, uint32_t now_ms);

const char *bp_reason_text(bp_reason_t r);

/* Kurzschluessel des Grundes. Die Oberflaeche setzt den Klartext daraus
 * selbst -- die Zeichenketten hier gehen auch ins Protokoll und bleiben
 * deshalb ohne Umlaute. */
const char *bp_reason_key(bp_reason_t r);

#ifdef __cplusplus
}
#endif
