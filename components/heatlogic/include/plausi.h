/*
 * Plausibilitaet der Messstellen.
 *
 * Reines Rechenmodul wie der Rest von heatlogic: die Zeit kommt als Parameter
 * herein, es wird nichts geschaltet und nichts protokolliert.
 *
 * Gesucht werden Messwerte, die so nicht zusammenpassen koennen. Bei laufender
 * Pumpe und warmem Speicher muss der Vorlauf eines Kreises waermer sein als
 * sein Ruecklauf -- dort kommt das warme Wasser an. Ist er es dauerhaft nicht,
 * sitzen die Fuehler an den falschen Rohren oder ihre Rollen sind vertauscht
 * zugeordnet.
 *
 * Verglichen wird also mit der Anlage selbst, nicht mit Katalogwerten. Eine
 * Historie braucht es dafuer nicht, nur die laufenden Werte.
 *
 * Bewusst traege: Jede Bedingung muss ueber viele Minuten anliegen, bevor sie
 * gemeldet wird, und sie verschwindet ebenso langsam wieder. Eine Meldung, die
 * bei jedem Anlaufen der Pumpe kommt und geht, liest niemand mehr.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PLAUSI_NONE = 0,
    /* Vorlauf dauerhaft kaelter als Ruecklauf, obwohl die Pumpe laeuft. */
    PLAUSI_FLOW_SWAPPED,
    /* Der Speicher ist waermer als der Kesselvorlauf waehrend einer Ladung. */
    PLAUSI_BUFFER_ABOVE_BOILER,
    /* Ein Fuehler verwirft mehr Messungen, als er annimmt. */
    PLAUSI_PROBE_ERRORS,
} plausi_code_t;

typedef struct {
    /* So lange muss die Bedingung anliegen, bevor gemeldet wird. */
    uint32_t hold_s;
    /* Mindestunterschied, damit Messrauschen nicht ausloest. */
    float margin_k;
    /* Darunter wird nicht geurteilt: ohne Waerme im Speicher sagt die
     * Spreizung eines Kreises nichts aus. */
    float min_buffer_c;
    /* Anteil verworfener Messungen, ab dem ein Fuehler auffaellt. */
    float max_error_ratio;
} plausi_cfg_t;

/* Ein Befund. Er haelt an, solange die Bedingung anliegt. */
typedef struct {
    bool active;
    uint32_t since_ms;   /* seit wann die Bedingung ununterbrochen anliegt */
    uint32_t held_s;     /* wie lange sie bisher angelegen hat */
} plausi_finding_t;

/* Vorgabe: 30 min Haltezeit, 1 K Rand, 35 °C Mindesttemperatur des Speichers,
 * 5 Prozent verworfene Messungen. */
void plausi_defaults(plausi_cfg_t *cfg);
void plausi_init(plausi_finding_t *f);

/*
 * Ein Zeitschritt fuer die Vertauschungspruefung eines Heizkreises.
 *
 * Geurteilt wird nur, solange die Pumpe laeuft und der Speicher warm genug
 * ist -- bei stehender Pumpe stehen beide Rohre einfach da und nehmen an, was
 * ihre Umgebung vorgibt. Im Sommer ist der Ruecklauf aus dem Estrich dann
 * regelmaessig waermer als der Vorlauf am Mischer, ohne dass etwas vertauscht
 * waere.
 */
void plausi_flow_tick(plausi_finding_t *f, const plausi_cfg_t *cfg, bool pump_on,
                      bool buffer_valid, float buffer_c, bool vl_valid, float vl_c,
                      bool rl_valid, float rl_c, uint32_t now_ms);

/*
 * Speicher waermer als der Kesselvorlauf, waehrend geladen wird. Physikalisch
 * unmoeglich: Der Speicher wird ueber den Vorlauf gefuellt.
 */
void plausi_buffer_tick(plausi_finding_t *f, const plausi_cfg_t *cfg, bool loading,
                        bool buffer_valid, float buffer_c, bool vl_valid, float vl_c,
                        uint32_t now_ms);

/* Anteil verworfener Messungen eines Fuehlers. */
bool plausi_probe_bad(const plausi_cfg_t *cfg, uint32_t reads, uint32_t errors);

/* Klartext eines Befundes, fuer Oberflaeche und Protokoll. */
const char *plausi_text(plausi_code_t c);

#ifdef __cplusplus
}
#endif
