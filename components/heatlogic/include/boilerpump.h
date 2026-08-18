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
 * Verglichen wird der Kesselvorlauf mit der Speichertemperatur, ersatzweise
 * mit dem eigenen Ruecklauf. Waehrend die Pumpe laeuft, sind beide fast
 * dasselbe -- der Ruecklauf kommt aus dem Speicher. Steht sie, gehen sie
 * auseinander, und nur der Speicher taugt dann noch als Bezug.
 *
 * Beim Anlaufen des Brenners steht die Pumpe zunaechst: Der kalte Kessel
 * wuerde sonst den warmen Speicher abkuehlen. Sie springt an, sobald der
 * Vorlauf den Ruecklauf ueberholt -- das ist zugleich die Ruecklaufanhebung,
 * die dem Kessel die Taupunktunterschreitung erspart.
 *
 * Ob der Brenner laeuft, geht deshalb bewusst nicht ein. Die Spreizung sagt
 * dasselbe, nur eine Stufe spaeter und dafuer richtig: Ein Brenner, der eben
 * erst gezuendet hat, hat noch keine Waerme abzugeben. Nach oben sichert die
 * Notgrenze ab.
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
    BP_REASON_NO_TRANSFER,  /* Ruecklauf waermer als Vorlauf */
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
     * Speichertemperatur, wenn sie vorliegt. Sie ist der bessere Bezug: Bei
     * stehender Pumpe fliesst nichts, Vor- und Ruecklauf nehmen beide die
     * Temperatur des Kesselkoerpers an, und die Spreizung sagt dann nichts
     * mehr. Ein Kessel, der noch Waerme haelt, bliebe so unbemerkt stehen.
     * Faellt der Wert aus -- er kommt vom Nachbargeraet --, gilt wieder der
     * Ruecklauf.
     */
    bool buffer_valid;
    float buffer_c;
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
} bp_state_t;

/* Vorgabe: ein Kelvin ein, ein halbes aus, zwei Minuten Haltezeit, je drei
 * Minuten Mindestlaufzeit und -pause, Notgrenze 85 Grad. */
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
