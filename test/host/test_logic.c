/*
 * Prueft Regelgesetz, Ventil-Zustandsmaschine und Pumpensteuerung auf dem
 * Rechner.
 *
 * Beide Module sind bewusst frei von IDF-Abhaengigkeiten, deshalb genuegt hier
 * ein gewoehnlicher C-Uebersetzer. Aufruf: make -C test/host
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "heatlogic.h"
#include "hw_map.h"
#include "roomctrl.h"
#include "atc_decode.h"
#include "schedule.h"
#include "valve.h"

static int s_failed;
static int s_checks;

#define CHECK(cond, fmt, ...)                                            \
    do {                                                                 \
        s_checks++;                                                      \
        if (!(cond)) {                                                   \
            s_failed++;                                                  \
            printf("  FEHLER %s:%d  " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
        }                                                                \
    } while (0)

#define CLOSE(a, b, tol) (fabsf((a) - (b)) <= (tol))

/* ------------------------------------------------------------------ */

static void test_control_law(void)
{
    printf("Regelgesetz\n");

    /* Proportionalband 1 K, Rasterung 0,1 - die Vorgabe. */
    const float band = 1.0f, step = 0.1f;

    /* Zwei Kelvin zu warm: Ventil ganz zu. */
    CHECK(CLOSE(roomctrl_target_position(20.0f, 22.0f, band, step), 0.0f, 0.001f),
          "zu warm muss 0 ergeben");

    /* Ein Kelvin zu warm liegt genau am unteren Bandende. */
    CHECK(CLOSE(roomctrl_target_position(20.0f, 21.0f, band, step), 0.0f, 0.001f),
          "unteres Bandende muss 0 ergeben");

    /* Sollwert erreicht: halbe Stellung. */
    CHECK(CLOSE(roomctrl_target_position(20.0f, 20.0f, band, step), 0.5f, 0.001f),
          "am Sollwert muss 0,5 herauskommen");

    /* Vier Zehntel zu kalt. */
    CHECK(CLOSE(roomctrl_target_position(20.0f, 19.6f, band, step), 0.7f, 0.001f),
          "0,4 K zu kalt muss 0,7 ergeben");

    /* Ein Kelvin und mehr zu kalt: ganz auf. */
    CHECK(CLOSE(roomctrl_target_position(20.0f, 19.0f, band, step), 1.0f, 0.001f),
          "unteres Bandende muss 1 ergeben");
    CHECK(CLOSE(roomctrl_target_position(20.0f, 15.0f, band, step), 1.0f, 0.001f),
          "weit zu kalt muss 1 ergeben");

    /* Breiteres Band flacht die Kennlinie ab. */
    CHECK(CLOSE(roomctrl_target_position(20.0f, 19.0f, 2.0f, step), 0.8f, 0.001f),
          "Band 2 K: 1 K zu kalt muss 0,8 ergeben");

    /* Uebereinstimmung mit der geschlossenen Form der Kennlinie. */
    for (float ist = 15.0f; ist <= 25.0f; ist += 0.1f) {
        float expected = roundf(((20.0f - ist) + 1.0f) / 2.0f / 0.1f) * 0.1f;
        if (expected > 1.0f) {
            expected = 1.0f;
        }
        if (expected < 0.0f) {
            expected = 0.0f;
        }
        float got = roomctrl_target_position(20.0f, ist, band, step);
        CHECK(CLOSE(got, expected, 0.001f), "Abweichung bei ist=%.1f: %.3f statt %.3f", ist, got,
              expected);
    }

    /* Mindestaenderung. */
    CHECK(!roomctrl_needs_move(0.30f, 0.305f, 0.01f), "kleine Abweichung darf nicht fahren");
    CHECK(roomctrl_needs_move(0.30f, 0.40f, 0.01f), "grosse Abweichung muss fahren");
}

/* ------------------------------------------------------------------ */

/* Laesst die Zeit in Schritten von 50 ms laufen, hoechstens bis limit_ms. */
static uint32_t run_until_idle(valve_t *v, uint32_t start_ms, uint32_t limit_ms)
{
    uint32_t t = start_ms;
    while (valve_is_moving(v) && (t - start_ms) < limit_ms) {
        t += 50;
        valve_tick(v, t);
    }
    return t - start_ms;
}

static void test_valve_travel(void)
{
    printf("Ventilfahrt\n");

    valve_cfg_t cfg = {.open_ms = 10000, .close_ms = 12000, .max_ms = 15000, .blank_ms = 2000};
    valve_t v;
    valve_init(&v, &cfg);
    valve_restore(&v, 0.0f);

    CHECK(valve_drive(&v) == HW_DRIVE_OFF, "im Stillstand darf nichts anliegen");

    /* Halb oeffnen. */
    CHECK(valve_goto(&v, 0.5f, 0.01f, 0), "Fahrbefehl muss angenommen werden");
    CHECK(v.op == VALVE_OPENING, "muss oeffnen");
    CHECK(valve_drive(&v) == HW_DRIVE_OPEN, "Ausgang muss auf oeffnen stehen");

    uint32_t took = run_until_idle(&v, 0, 30000);
    CHECK(CLOSE(v.position, 0.5f, 0.02f), "Position nach halber Fahrt: %.3f", v.position);
    CHECK(CLOSE((float)took, 5000.0f, 100.0f), "Dauer der halben Fahrt: %u ms", took);
    CHECK(v.last_stop_reason == VALVE_STOP_TARGET, "Grund muss die Zielstellung sein");
    CHECK(valve_drive(&v) == HW_DRIVE_OFF, "nach der Fahrt muss abgeschaltet sein");

    /* Kein neuer Fahrbefehl bei zu kleiner Abweichung. */
    CHECK(!valve_goto(&v, 0.505f, 0.01f, 6000), "kleine Abweichung darf nicht fahren");

    /* Schliessen dauert laenger als oeffnen. */
    valve_goto(&v, 0.0f, 0.01f, 7000);
    CHECK(v.op == VALVE_CLOSING, "muss schliessen");
    took = run_until_idle(&v, 7000, 30000);
    CHECK(CLOSE((float)took, 6000.0f, 150.0f), "halbe Schliessfahrt: %u ms", took);
    CHECK(CLOSE(v.position, 0.0f, 0.02f), "muss zu sein: %.3f", v.position);
}

static void test_valve_endstop(void)
{
    printf("Endlage\n");

    valve_cfg_t cfg = {.open_ms = 10000, .close_ms = 10000, .max_ms = 15000, .blank_ms = 2000};
    valve_t v;
    valve_init(&v, &cfg);
    valve_restore(&v, 0.0f);

    valve_goto(&v, 1.0f, 0.01f, 0);

    /* Innerhalb der Sperrzeit ist die Meldung der Anlaufstrom, keine
     * Endlage. */
    valve_tick(&v, 500);
    CHECK(!valve_endstop(&v, 500), "Meldung in der Sperrzeit muss verworfen werden");
    CHECK(valve_is_moving(&v), "der Antrieb muss weiterlaufen");

    /* Danach wird sie ausgewertet. */
    for (uint32_t t = 550; t <= 4000; t += 50) {
        valve_tick(&v, t);
    }
    CHECK(valve_endstop(&v, 4000), "Meldung nach der Sperrzeit muss zaehlen");
    CHECK(!valve_is_moving(&v), "nach der Endlage muss der Antrieb stehen");
    CHECK(CLOSE(v.position, 1.0f, 0.0001f), "Position muss auf 1 stehen: %.3f", v.position);
    CHECK(v.last_stop_reason == VALVE_STOP_ENDSTOP, "Grund muss die Endlage sein");
}

static void test_valve_timeout(void)
{
    printf("Maximallaufzeit\n");

    /* Fahrzeit deutlich laenger als die Maximallaufzeit: der Antrieb muss
     * abschalten, auch wenn keine Endlage gemeldet wird. */
    valve_cfg_t cfg = {.open_ms = 60000, .close_ms = 60000, .max_ms = 5000, .blank_ms = 1000};
    valve_t v;
    valve_init(&v, &cfg);
    valve_restore(&v, 0.0f);

    valve_goto(&v, 1.0f, 0.01f, 0);
    uint32_t took = run_until_idle(&v, 0, 30000);

    CHECK(CLOSE((float)took, 5000.0f, 100.0f), "muss nach der Maximallaufzeit stoppen: %u ms",
          took);
    CHECK(v.last_stop_reason == VALVE_STOP_TIMEOUT, "Grund muss die Maximallaufzeit sein");
    CHECK(CLOSE(v.position, 1.0f, 0.0001f),
          "bei Fahrt auf Anschlag gilt die Endstellung als erreicht: %.3f", v.position);
}

static void test_valve_reference_run(void)
{
    printf("Referenzfahrt bei unbekannter Stellung\n");

    valve_cfg_t cfg = {.open_ms = 10000, .close_ms = 10000, .max_ms = 12000, .blank_ms = 1000};
    valve_t v;
    valve_init(&v, &cfg);
    CHECK(!v.position_known, "nach dem Start ist die Stellung unbekannt");

    /* Ziel 0,5: zuerst muss gegen die untere Endlage referenziert werden. */
    CHECK(valve_goto(&v, 0.5f, 0.01f, 0), "Referenzfahrt muss starten");
    CHECK(v.op == VALVE_CLOSING, "Referenzfahrt geht zu");

    /* Endlage nach 8 s melden. */
    for (uint32_t t = 50; t <= 8000; t += 50) {
        valve_tick(&v, t);
    }
    CHECK(valve_endstop(&v, 8000), "Endlage muss ausgewertet werden");
    CHECK(v.position_known, "danach ist die Stellung bekannt");
    CHECK(v.op == VALVE_OPENING, "anschliessend muss das eigentliche Ziel angefahren werden");

    run_until_idle(&v, 8000, 30000);
    CHECK(CLOSE(v.position, 0.5f, 0.02f), "Endstellung nach der Referenzfahrt: %.3f", v.position);
}

static void test_valve_force(void)
{
    printf("Notfahrt\n");

    valve_cfg_t cfg = {.open_ms = 10000, .close_ms = 10000, .max_ms = 12000, .blank_ms = 1000};
    valve_t v;

    /* Die Steuerung haelt das Ventil fuer ganz offen. Ein gewoehnlicher
     * Fahrbefehl auf "auf" taete deshalb gar nichts - genau der Fall, fuer den
     * es die Notfahrt gibt. */
    valve_init(&v, &cfg);
    valve_restore(&v, 1.0f);
    CHECK(!valve_goto(&v, 1.0f, 0.0f, 0), "gewoehnlicher Fahrbefehl faehrt hier nicht");

    valve_force(&v, true, 0);
    CHECK(v.op == VALVE_OPENING, "Notfahrt muss trotzdem oeffnen");
    CHECK(valve_drive(&v) == HW_DRIVE_OPEN, "Ausgang muss auf oeffnen stehen");

    /* Sie darf nicht an der vermeintlich erreichten Stellung abbrechen. */
    for (uint32_t t = 50; t <= 3000; t += 50) {
        valve_tick(&v, t);
    }
    CHECK(valve_is_moving(&v), "Notfahrt darf nicht an der Zielstellung enden");

    CHECK(valve_endstop(&v, 3000), "Endlage muss die Notfahrt beenden");
    CHECK(!valve_is_moving(&v), "danach muss der Antrieb stehen");
    CHECK(CLOSE(v.position, 1.0f, 0.0001f), "Stellung nach der Notfahrt: %.3f", v.position);
    CHECK(v.position_known, "die Stellung gilt danach als bekannt");

    /* Bei unbekannter Stellung darf keine Referenzfahrt dazwischenkommen: ein
     * "auf" muss auf machen, nicht erst zu. */
    valve_init(&v, &cfg);
    CHECK(!v.position_known, "Ausgangslage: Stellung unbekannt");
    valve_force(&v, true, 0);
    CHECK(v.op == VALVE_OPENING, "Notfahrt auf darf nicht erst schliessen");

    /* Ohne Endlagenmeldung beendet die Maximallaufzeit die Fahrt, und das
     * Ventil steht dann mechanisch am Anschlag. */
    uint32_t took = run_until_idle(&v, 0, 30000);
    CHECK(CLOSE((float)took, 12000.0f, 100.0f), "Notfahrt endet nach der Maximallaufzeit: %u ms",
          took);
    CHECK(v.last_stop_reason == VALVE_STOP_TIMEOUT, "Grund muss die Maximallaufzeit sein");
    CHECK(CLOSE(v.position, 1.0f, 0.0001f), "Stellung am Anschlag: %.3f", v.position);

    /* Und in die Gegenrichtung ebenso. */
    valve_init(&v, &cfg);
    valve_restore(&v, 0.0f);
    valve_force(&v, false, 0);
    CHECK(v.op == VALVE_CLOSING, "Notfahrt zu muss schliessen, auch wenn schon zu");
    run_until_idle(&v, 0, 30000);
    CHECK(CLOSE(v.position, 0.0f, 0.0001f), "Stellung nach Notfahrt zu: %.3f", v.position);

    /* Ein Halt beendet die Notfahrt. */
    valve_init(&v, &cfg);
    valve_restore(&v, 0.5f);
    valve_force(&v, true, 0);
    valve_tick(&v, 1000);
    valve_stop(&v, 1000);
    CHECK(!valve_is_moving(&v), "Halt muss die Notfahrt beenden");
    CHECK(!v.forcing, "die Notfahrt darf danach nicht weiterlaufen");
}

/* ------------------------------------------------------------------ */

static void test_hw_map(void)
{
    printf("Hardwarezuordnung\n");

    /* Jeder Kanal belegt zwei aufeinanderfolgende Bits, keines doppelt. */
    unsigned used = 0;
    for (uint8_t n = 1; n <= HW_CHANNEL_COUNT; n++) {
        const hw_channel_t *c = hw_channel(n);
        CHECK(c != NULL, "CH%u muss beschrieben sein", n);
        CHECK(!(used & (1U << c->sr_bit_ia)), "Bit %u ist doppelt vergeben", c->sr_bit_ia);
        CHECK(!(used & (1U << c->sr_bit_ib)), "Bit %u ist doppelt vergeben", c->sr_bit_ib);
        used |= (1U << c->sr_bit_ia) | (1U << c->sr_bit_ib);
    }

    /* Die Gruppentabelle muss zur Kanaltabelle passen: eine Endlagenmeldung
     * traefe sonst den falschen Kanal. */
    for (uint8_t g = 0; g < HW_BEMF_GROUP_COUNT; g++) {
        const hw_bemf_group_t *grp = hw_bemf_group(g);
        CHECK(grp != NULL, "Gruppe %u muss beschrieben sein", g);
        for (int i = 0; i < 2; i++) {
            uint8_t ch = grp->channels[i];
            if (ch == 0) {
                continue;
            }
            CHECK(hw_group_of_channel(ch) == g, "CH%u gehoert laut Kanaltabelle zu Gruppe %u",
                  ch, hw_group_of_channel(ch));
        }
    }

    /* Jeder Kanal muss in genau einer Gruppe auftauchen. */
    for (uint8_t n = 1; n <= HW_CHANNEL_COUNT; n++) {
        int found = 0;
        for (uint8_t g = 0; g < HW_BEMF_GROUP_COUNT; g++) {
            const hw_bemf_group_t *grp = hw_bemf_group(g);
            for (int i = 0; i < 2; i++) {
                if (grp->channels[i] == n) {
                    found++;
                }
            }
        }
        CHECK(found == 1, "CH%u steht in %d Gruppen", n, found);
    }
}


/* ------------------------------------------------------------------ */
/* Pumpensteuerung                                                     */
/* ------------------------------------------------------------------ */

/* Laesst die Zustandsmaschine eine Zeitspanne im Sekundentakt laufen. */
static uint32_t pumpe_laufen(pump_state_t *st, const pump_cfg_t *cfg, pump_input_t in,
                             uint32_t von_ms, uint32_t dauer_s)
{
    uint32_t t = von_ms;
    for (uint32_t i = 0; i < dauer_s; i++) {
        t += 1000;
        pump_tick(st, cfg, &in, t);
    }
    return t;
}

static pump_input_t bedarf(bool da)
{
    pump_input_t in = {0};
    in.demand = da;
    in.buffer_valid = true;
    in.buffer_c = 55.0f;   /* warm genug */
    return in;
}

static void test_pump_basic(void)
{
    printf("Pumpe: Bedarf und Nachlauf\n");

    pump_cfg_t cfg;
    pump_defaults(&cfg);
    pump_state_t st;
    pump_init(&st, PUMP_MODE_AUTO);

    uint32_t t = 1000;
    pump_tick(&st, &cfg, &(pump_input_t){0}, t);
    CHECK(!st.on, "ohne Bedarf steht die Pumpe");

    /* Bedarf: laeuft sofort an. */
    t = pumpe_laufen(&st, &cfg, bedarf(true), t, 2);
    CHECK(st.on, "mit Bedarf laeuft die Pumpe");
    CHECK(st.reason == PUMP_REASON_DEMAND, "Grund ist der Abnehmer, nicht %s",
          pump_reason_text(st.reason));

    /* Mindestlaufzeit ueberschreiten, damit sie danach abschalten darf. */
    t = pumpe_laufen(&st, &cfg, bedarf(true), t, cfg.min_run_s + 5);

    /* Bedarf faellt weg: Nachlauf. */
    t = pumpe_laufen(&st, &cfg, bedarf(false), t, cfg.overrun_s - 10);
    CHECK(st.on, "im Nachlauf laeuft die Pumpe weiter");
    CHECK(st.reason == PUMP_REASON_OVERRUN, "Grund ist der Nachlauf, nicht %s",
          pump_reason_text(st.reason));

    /* Nachlauf abgelaufen: aus. */
    t = pumpe_laufen(&st, &cfg, bedarf(false), t, 20);
    CHECK(!st.on, "nach dem Nachlauf steht die Pumpe");
    CHECK(st.reason == PUMP_REASON_NO_DEMAND, "Grund ist der fehlende Abnehmer, nicht %s",
          pump_reason_text(st.reason));
}

static void test_pump_min_times(void)
{
    printf("Pumpe: Mindestlaufzeit und Mindestpause\n");

    pump_cfg_t cfg;
    pump_defaults(&cfg);
    cfg.overrun_s = 0; /* Nachlauf hier ausblenden */
    pump_state_t st;
    pump_init(&st, PUMP_MODE_AUTO);

    uint32_t t = pumpe_laufen(&st, &cfg, bedarf(true), 1000, 5);
    CHECK(st.on, "Pumpe laeuft");

    /* Bedarf sofort wieder weg: die Mindestlaufzeit haelt sie an. */
    t = pumpe_laufen(&st, &cfg, bedarf(false), t, 30);
    CHECK(st.on, "Mindestlaufzeit haelt die Pumpe");
    CHECK(st.reason == PUMP_REASON_MIN_RUN, "Grund ist die Mindestlaufzeit, nicht %s",
          pump_reason_text(st.reason));

    t = pumpe_laufen(&st, &cfg, bedarf(false), t, cfg.min_run_s);
    CHECK(!st.on, "nach der Mindestlaufzeit schaltet sie ab");

    /* Bedarf kommt sofort zurueck: die Mindestpause haelt sie aus. */
    t = pumpe_laufen(&st, &cfg, bedarf(true), t, 30);
    CHECK(!st.on, "Mindestpause haelt die Pumpe aus");
    CHECK(st.reason == PUMP_REASON_MIN_PAUSE, "Grund ist die Mindestpause, nicht %s",
          pump_reason_text(st.reason));

    t = pumpe_laufen(&st, &cfg, bedarf(true), t, cfg.min_pause_s);
    CHECK(st.on, "nach der Mindestpause laeuft sie wieder");
}

static void test_pump_buffer_cold(void)
{
    printf("Pumpe: kalter Speicher\n");

    pump_cfg_t cfg;
    pump_defaults(&cfg);
    pump_state_t st;
    pump_init(&st, PUMP_MODE_AUTO);

    pump_input_t in = bedarf(true);
    in.buffer_c = cfg.min_buffer_c - 5.0f;

    uint32_t t = pumpe_laufen(&st, &cfg, in, 1000, 10);
    CHECK(!st.on, "unter der Mindesttemperatur bleibt die Pumpe aus");
    CHECK(st.reason == PUMP_REASON_BUFFER_COLD, "Grund ist der kalte Speicher, nicht %s",
          pump_reason_text(st.reason));

    /* Ohne gueltigen Messwert wird nicht gesperrt: eine fehlende Angabe darf
     * die Heizung nicht stilllegen. */
    pump_init(&st, PUMP_MODE_AUTO);
    in.buffer_valid = false;
    t = pumpe_laufen(&st, &cfg, in, t, 10);
    CHECK(st.on, "ohne Speicherwert wird nicht gesperrt");
}

static void test_pump_frost(void)
{
    printf("Pumpe: Frostschutz\n");

    pump_cfg_t cfg;
    pump_defaults(&cfg);
    pump_state_t st;
    pump_init(&st, PUMP_MODE_OFF); /* von Hand ausgeschaltet */

    pump_input_t in = bedarf(false);
    in.buffer_c = 20.0f; /* auch der Speicher ist kalt */
    in.room_valid = true;
    in.min_room_c = cfg.frost_c - 1.0f;

    pumpe_laufen(&st, &cfg, in, 1000, 5);
    CHECK(st.on, "Frostschutz geht auch dem Handbetrieb vor");
    CHECK(st.reason == PUMP_REASON_FROST, "Grund ist der Frostschutz, nicht %s",
          pump_reason_text(st.reason));
}

static void test_pump_manual(void)
{
    printf("Pumpe: Handbetrieb\n");

    pump_cfg_t cfg;
    pump_defaults(&cfg);
    pump_state_t st;
    pump_init(&st, PUMP_MODE_ON);

    uint32_t t = pumpe_laufen(&st, &cfg, bedarf(false), 1000, 5);
    CHECK(st.on, "auf Ein laeuft die Pumpe ohne Bedarf");

    pump_set_mode(&st, PUMP_MODE_OFF, t);
    t = pumpe_laufen(&st, &cfg, bedarf(true), t, 5);
    CHECK(!st.on, "auf Aus steht sie trotz Bedarf");

    /* Der Wechsel der Betriebsart soll nicht auf die Mindestpause warten. */
    pump_set_mode(&st, PUMP_MODE_AUTO, t);
    t = pumpe_laufen(&st, &cfg, bedarf(true), t, 5);
    CHECK(st.on, "zurueck auf Automatik laeuft sie sofort an");
}

static void test_pump_seize(void)
{
    printf("Pumpe: Schutzlauf\n");

    pump_cfg_t cfg;
    pump_defaults(&cfg);
    cfg.seize_run_s = 60;
    pump_state_t st;
    pump_init(&st, PUMP_MODE_AUTO);

    pump_input_t in = bedarf(false);
    uint32_t t = 1000;
    pump_tick(&st, &cfg, &in, t);
    CHECK(!st.on, "zu Beginn steht die Pumpe");

    /* Ohne faelligen Termin passiert auch nach Tagen nichts. */
    t = pumpe_laufen(&st, &cfg, in, t, 3 * 86400UL);
    CHECK(!st.on, "ohne Termin laeuft kein Schutzlauf");

    /* Der Termin faellt. */
    pump_input_t faellig = in;
    faellig.seize_due = true;
    t += 1000;
    pump_tick(&st, &cfg, &faellig, t);
    CHECK(st.on, "zum Termin laeuft der Schutzlauf an");
    CHECK(st.reason == PUMP_REASON_SEIZE, "Grund ist der Schutzlauf, nicht %s",
          pump_reason_text(st.reason));

    /* Er endet von selbst. */
    t = pumpe_laufen(&st, &cfg, in, t, cfg.seize_run_s + cfg.min_run_s + 5);
    CHECK(!st.on, "der Schutzlauf endet von selbst");

    /*
     * Eine Pumpe, die am selben Tag ohnehin gelaufen ist, sitzt nicht fest --
     * der Termin geht dann an ihr vorbei.
     */
    pump_init(&st, PUMP_MODE_AUTO);
    t = pumpe_laufen(&st, &cfg, bedarf(true), t, 600);
    CHECK(st.on, "sie laeuft wegen Bedarf");
    t = pumpe_laufen(&st, &cfg, in, t, cfg.overrun_s + cfg.min_run_s + 30);
    CHECK(!st.on, "und geht danach aus");
    t += 1000;
    pump_tick(&st, &cfg, &faellig, t);
    CHECK(!st.on, "kurz nach einem Lauf braucht sie keinen Schutzlauf");

    /* Einen Tag spaeter schon. */
    t = pumpe_laufen(&st, &cfg, in, t, 86400UL + 60);
    t += 1000;
    pump_tick(&st, &cfg, &faellig, t);
    CHECK(st.on, "einen Tag spaeter laeuft er an");
}

/* ------------------------------------------------------------------ */
/* Bedarfsauswertung                                                   */
/* ------------------------------------------------------------------ */

static void test_demand(void)
{
    printf("Bedarf der Verteiler\n");

    demand_result_t out;

    /* Zwei Verteiler, einer meldet Bedarf. */
    demand_source_t a[2] = {
        {.seen = true, .demand = false, .age_s = 3, .room_valid = true, .min_room_c = 21.0f},
        {.seen = true, .demand = true, .age_s = 4, .room_valid = true, .min_room_c = 19.5f},
    };
    demand_evaluate(a, 2, 180, &out);
    CHECK(out.demand, "ein meldender Verteiler genuegt");
    CHECK(!out.stale, "beide sind frisch");
    CHECK(CLOSE(out.min_room_c, 19.5f, 0.01f), "kaelteste Raumtemperatur ist 19,5, nicht %.1f",
          out.min_room_c);

    /* Einer ist verstummt: im Zweifel heizen. */
    demand_source_t b[2] = {
        {.seen = true, .demand = false, .age_s = 3},
        {.seen = true, .demand = false, .age_s = 400},
    };
    demand_evaluate(b, 2, 180, &out);
    CHECK(out.demand, "ein verstummter Verteiler gilt als bedarfsmeldend");
    CHECK(out.stale, "die Stoerung wird ausgewiesen");

    /* Einer hat nie geantwortet: er wird nicht gewertet, sonst liefe die
     * Pumpe wegen eines abgeschalteten Geraets durchgehend. */
    demand_source_t c[2] = {
        {.seen = true, .demand = false, .age_s = 3},
        {.seen = false, .demand = false, .age_s = 99999},
    };
    demand_evaluate(c, 2, 180, &out);
    CHECK(!out.demand, "ein nie erreichtes Geraet erzeugt keinen Bedarf");
    CHECK(!out.stale, "und auch keine Stoerungsmeldung");
    CHECK(out.any_seen, "der erreichbare zaehlt");

    /* Gar keiner erreichbar. */
    demand_source_t d[1] = {{.seen = false}};
    demand_evaluate(d, 1, 180, &out);
    CHECK(!out.any_seen, "keine Quelle hat je geantwortet");
    CHECK(!out.demand, "und es entsteht kein Bedarf");
}


/* ------------------------------------------------------------------ */
/* Brennererkennung                                                    */
/* ------------------------------------------------------------------ */

/* Laesst die Erkennung eine Zeitspanne mit festem Abgaswert laufen. */
static uint32_t brenner_laufen(burner_state_t *st, const burner_cfg_t *cfg, float abgas,
                               uint32_t von_ms, uint32_t dauer_s)
{
    uint32_t t = von_ms;
    for (uint32_t i = 0; i < dauer_s; i++) {
        t += 1000;
        burner_tick(st, cfg, true, abgas, t);
    }
    return t;
}

static void test_burner_detect(void)
{
    printf("Brenner: Erkennung\n");

    burner_cfg_t cfg;
    burner_defaults(&cfg);
    burner_state_t st;
    burner_init(&st);

    /* Kaltes Rohr bei 22 Grad -- das ist die Bezugslinie. */
    uint32_t t = brenner_laufen(&st, &cfg, 22.0f, 1000, 120);
    CHECK(st.known, "der Abgaswert liegt vor");
    CHECK(!st.running, "am kalten Rohr laeuft kein Brenner");
    CHECK(CLOSE(st.baseline_c, 22.0f, 0.1f), "Bezugslinie ist 22, nicht %.1f", st.baseline_c);

    /* Rohr wird warm: erst nach der Haltezeit gilt der Brenner als laufend. */
    t = brenner_laufen(&st, &cfg, 22.0f + cfg.delta_on_k + 5.0f, t, cfg.on_hold_s - 10);
    CHECK(!st.running, "vor Ablauf der Haltezeit gilt er noch nicht als laufend");
    t = brenner_laufen(&st, &cfg, 22.0f + cfg.delta_on_k + 5.0f, t, 20);
    CHECK(st.running, "nach der Haltezeit laeuft er");
    CHECK(st.starts_today == 1, "ein Start gezaehlt, nicht %u", (unsigned)st.starts_today);

    /* Laufzeit zaehlt mit. */
    uint32_t vorher = st.runtime_today_s;
    t = brenner_laufen(&st, &cfg, 22.0f + cfg.delta_on_k + 5.0f, t, 60);
    CHECK(st.runtime_today_s - vorher >= 59 && st.runtime_today_s - vorher <= 61,
          "60 Sekunden Laufzeit gezaehlt, nicht %u", (unsigned)(st.runtime_today_s - vorher));

    /* Rohr kuehlt ab: erst nach der laengeren Haltezeit gilt er als aus. */
    t = brenner_laufen(&st, &cfg, 22.0f + 2.0f, t, cfg.off_hold_s - 30);
    CHECK(st.running, "kurzes Abkuehlen beendet den Lauf noch nicht");
    t = brenner_laufen(&st, &cfg, 22.0f + 2.0f, t, 60);
    CHECK(!st.running, "nach der Haltezeit ist er aus");
    CHECK(st.starts_today == 1, "kein zweiter Start gezaehlt");
}

static void test_burner_hysteresis(void)
{
    printf("Brenner: Hysterese\n");

    burner_cfg_t cfg;
    burner_defaults(&cfg);
    burner_state_t st;
    burner_init(&st);

    uint32_t t = brenner_laufen(&st, &cfg, 20.0f, 1000, 60);

    /* Ein Wert zwischen den beiden Schwellen darf keinen Wechsel ausloesen --
     * weder von aus nach ein noch umgekehrt. */
    float mitte = 20.0f + (cfg.delta_on_k + cfg.delta_off_k) / 2.0f;
    t = brenner_laufen(&st, &cfg, mitte, t, 600);
    CHECK(!st.running, "zwischen den Schwellen bleibt er aus");

    /* Ueber die obere Schwelle, dann zurueck in die Mitte. */
    t = brenner_laufen(&st, &cfg, 20.0f + cfg.delta_on_k + 3.0f, t, cfg.on_hold_s + 10);
    CHECK(st.running, "ueber der oberen Schwelle laeuft er");
    t = brenner_laufen(&st, &cfg, mitte, t, 900);
    CHECK(st.running, "in der Mitte laeuft er weiter");
}

static void test_burner_baseline(void)
{
    printf("Brenner: Bezugslinie\n");

    burner_cfg_t cfg;
    burner_defaults(&cfg);
    burner_state_t st;
    burner_init(&st);

    /* Im Winter ist der Heizungsraum kalt. */
    uint32_t t = brenner_laufen(&st, &cfg, 12.0f, 1000, 120);
    CHECK(CLOSE(st.baseline_c, 12.0f, 0.1f), "Bezugslinie folgt nach unten");

    /* Steigt der Heizungsraum ueber die Jahreszeit von 12 auf 20 Grad, sind
     * das 8 K ueber der kalten Linie -- weniger als die Einschaltschwelle von
     * 12 K. Genau dafuer ist der Abstand da: eine langsame Erwaermung des
     * Raums darf keinen Brennerlauf vortaeuschen. */
    t = brenner_laufen(&st, &cfg, 20.0f, t, 1800);
    CHECK(!st.running, "8 K ueber der Linie sind noch kein Lauf");

    /* Ein wirklicher Lauf hebt das Rohr deutlich weiter an. */
    t = brenner_laufen(&st, &cfg, 12.0f + cfg.delta_on_k + 8.0f, t, cfg.on_hold_s + 10);
    CHECK(st.running, "ueber der Einschaltschwelle gilt er als laufend");

    /* Faellt der Wert wieder, endet der Lauf und die Linie bleibt unten. */
    t = brenner_laufen(&st, &cfg, 12.0f, t, cfg.off_hold_s + 10);
    CHECK(!st.running, "zurueck am kalten Rohr ist er aus");
    CHECK(CLOSE(st.baseline_c, 12.0f, 0.1f), "die Bezugslinie bleibt beim Minimum");
}

static void test_burner_consumption(void)
{
    printf("Brenner: Verbrauch und Tageswechsel\n");

    burner_cfg_t cfg;
    burner_defaults(&cfg);
    cfg.duese_l_h = 2.4f;
    burner_state_t st;
    burner_init(&st);
    st.runtime_today_s = 3600;

    CHECK(CLOSE(burner_litres_today(&st, &cfg), 2.4f, 0.01f),
          "eine Stunde Laufzeit ergibt 2,4 Liter, nicht %.2f", burner_litres_today(&st, &cfg));

    cfg.duese_l_h = 0.0f;
    CHECK(burner_litres_today(&st, &cfg) == 0.0f, "ohne Duesenangabe keine Schaetzung");

    st.starts_today = 7;
    burner_new_day(&st);
    CHECK(st.runtime_today_s == 0 && st.starts_today == 0, "der Tageswechsel setzt zurueck");
}

static void test_burner_no_probe(void)
{
    printf("Brenner: ohne Abgasfuehler\n");

    burner_cfg_t cfg;
    burner_defaults(&cfg);
    burner_state_t st;
    burner_init(&st);

    for (uint32_t i = 0; i < 100; i++) {
        burner_tick(&st, &cfg, false, 0.0f, 1000 + i * 1000);
    }
    CHECK(!st.known, "ohne Fuehler ist der Zustand unbekannt");
    CHECK(!st.running, "und es wird kein Lauf gemeldet");
}


/* ------------------------------------------------------------------ */
/* Ladezustand des Pufferspeichers                                     */
/* ------------------------------------------------------------------ */

static uint32_t laden_laufen(charge_state_t *st, const charge_cfg_t *cfg, charge_input_t in,
                             uint32_t von_ms, uint32_t dauer_s)
{
    uint32_t t = von_ms;
    for (uint32_t i = 0; i < dauer_s; i++) {
        t += 1000;
        charge_tick(st, cfg, &in, t);
    }
    return t;
}

/* Kessel heizt: Vorlauf heiss, Ruecklauf noch kalt. */
static charge_input_t ladung(bool brenner, float vl, float rl, float puffer)
{
    charge_input_t in = {0};
    in.burner_known = true;
    in.burner_running = brenner;
    in.kessel_valid = true;
    in.kessel_vl_c = vl;
    in.kessel_rl_c = rl;
    in.puffer_valid = true;
    in.puffer_c = puffer;
    return in;
}

static void test_charge_cycle(void)
{
    printf("Ladung: Verlauf einer Kesselladung\n");

    charge_cfg_t cfg;
    charge_defaults(&cfg);
    charge_state_t st;
    charge_init(&st);

    /* Ruhe: Brenner aus, Speicher halbvoll. */
    uint32_t t = laden_laufen(&st, &cfg, ladung(false, 45.0f, 44.0f, 48.0f), 1000, 30);
    CHECK(st.phase == CHARGE_IDLE, "ohne Brenner keine Ladung, nicht %s",
          charge_phase_text(st.phase));
    CHECK(!st.limited, "mit Kesselwerten ist die Auswertung vollstaendig");

    /* Brenner an, Ruecklauf kalt: es wird geladen. */
    t = laden_laufen(&st, &cfg, ladung(true, 72.0f, 45.0f, 50.0f), t, 60);
    CHECK(st.phase == CHARGE_LOADING, "bei weiter Spreizung wird geladen, nicht %s",
          charge_phase_text(st.phase));
    CHECK(CLOSE(st.spread_k, 27.0f, 0.1f), "Spreizung 27 K, nicht %.1f", st.spread_k);

    /* Ruecklauf zieht nach: erst nach der Haltezeit gilt der Speicher als voll. */
    t = laden_laufen(&st, &cfg, ladung(true, 74.0f, 69.0f, 60.0f), t, cfg.spread_hold_s - 30);
    CHECK(st.phase == CHARGE_LOADING, "vor Ablauf der Haltezeit noch nicht voll");
    t = laden_laufen(&st, &cfg, ladung(true, 74.0f, 69.0f, 61.0f), t, 60);
    CHECK(st.phase == CHARGE_FULL, "nach der Haltezeit ist er geladen, nicht %s",
          charge_phase_text(st.phase));

    /* Brenner aus, Speicher bleibt warm: geladen haelt. */
    t = laden_laufen(&st, &cfg, ladung(false, 70.0f, 66.0f, 61.0f), t, 600);
    CHECK(st.phase == CHARGE_FULL, "nach dem Abschalten bleibt er geladen");

    /* Der Speicher gibt ab: unter 85 Prozent faellt die Anzeige zurueck. */
    t = laden_laufen(&st, &cfg, ladung(false, 50.0f, 48.0f, 50.0f), t, 60);
    CHECK(st.phase == CHARGE_IDLE, "beim Entladen faellt er zurueck, nicht %s",
          charge_phase_text(st.phase));
}

static void test_charge_burner_decides(void)
{
    printf("Ladung: Kessel schaltet selbst ab\n");

    charge_cfg_t cfg;
    charge_defaults(&cfg);
    charge_state_t st;
    charge_init(&st);

    /* Waehrend der Ladung bleibt die Spreizung weit -- der Kessel schaltet
     * trotzdem ab, weil seine eigene Regelung die Ladung fuer beendet haelt. */
    uint32_t t = laden_laufen(&st, &cfg, ladung(true, 75.0f, 55.0f, 58.0f), 1000, 120);
    CHECK(st.phase == CHARGE_LOADING, "es wird geladen");

    t = laden_laufen(&st, &cfg, ladung(false, 74.0f, 60.0f, 60.0f), t, 30);
    CHECK(st.phase == CHARGE_FULL, "Abschalten bei heissem Vorlauf gilt als fertig, nicht %s",
          charge_phase_text(st.phase));
}

static void test_charge_level(void)
{
    printf("Ladung: Fuellstand und Warnung\n");

    charge_cfg_t cfg;
    charge_defaults(&cfg);   /* leer 35, voll 62, Warnung 40 */
    charge_state_t st;
    charge_init(&st);

    laden_laufen(&st, &cfg, ladung(false, 40.0f, 39.0f, 35.0f), 1000, 5);
    CHECK(CLOSE(st.level, 0.0f, 0.01f), "35 Grad sind leer, nicht %.2f", st.level);
    CHECK(st.warn_dhw, "unter 40 Grad wird das Warmwasser knapp");

    laden_laufen(&st, &cfg, ladung(false, 40.0f, 39.0f, 62.0f), 6000, 5);
    CHECK(CLOSE(st.level, 1.0f, 0.01f), "62 Grad sind voll, nicht %.2f", st.level);
    CHECK(!st.warn_dhw, "ueber der Warnschwelle keine Meldung");

    laden_laufen(&st, &cfg, ladung(false, 40.0f, 39.0f, 48.5f), 12000, 5);
    CHECK(CLOSE(st.level, 0.5f, 0.02f), "48,5 Grad sind die Haelfte, nicht %.2f", st.level);

    /* Ausserhalb der Spanne wird begrenzt, nicht extrapoliert. */
    laden_laufen(&st, &cfg, ladung(false, 40.0f, 39.0f, 80.0f), 18000, 5);
    CHECK(CLOSE(st.level, 1.0f, 0.01f), "ueber voll bleibt es bei 100 Prozent");
}

static void test_charge_limited(void)
{
    printf("Ladung: ohne Kesselwerte\n");

    charge_cfg_t cfg;
    charge_defaults(&cfg);
    charge_state_t st;
    charge_init(&st);

    charge_input_t in = {0};
    in.puffer_valid = true;
    in.puffer_c = 55.0f;
    /* kessel_valid und burner_known bleiben falsch */

    uint32_t t = laden_laufen(&st, &cfg, in, 1000, 60);
    CHECK(st.limited, "ohne Kesselwerte ist die Auswertung eingeschraenkt");
    CHECK(st.level_valid, "der Fuellstand steht trotzdem");
    CHECK(CLOSE(st.level, (55.0f - 35.0f) / 27.0f, 0.02f), "Fuellstand aus dem Pufferfuehler");
    CHECK(st.phase == CHARGE_UNKNOWN, "die Phase bleibt unbekannt, nicht %s",
          charge_phase_text(st.phase));

    /* War der Speicher zuvor als geladen erkannt, haelt das -- bis er
     * merklich abkuehlt. Sonst spraenge die Anzeige, sobald das Geraet am
     * Kessel kurz nicht antwortet. */
    charge_init(&st);
    laden_laufen(&st, &cfg, ladung(true, 74.0f, 69.0f, 61.0f), 1000, cfg.spread_hold_s + 30);
    CHECK(st.phase == CHARGE_FULL, "erst geladen");
    in.puffer_c = 60.0f;
    t = laden_laufen(&st, &cfg, in, 400000, 30);
    CHECK(st.phase == CHARGE_FULL, "bei warmem Speicher haelt die Anzeige");
    in.puffer_c = 45.0f;
    laden_laufen(&st, &cfg, in, t, 30);
    CHECK(st.phase == CHARGE_IDLE, "kuehlt er ab, faellt sie zurueck");
}

/* ------------------------------------------------------------------ */
/* Ausloesen der Aufzeichnung                                          */
/* ------------------------------------------------------------------ */

#define TAIL_S 600

static rec_trig_cfg_t rec_cfg(void)
{
    rec_trig_cfg_t cfg;
    rec_trigger_defaults(&cfg);
    return cfg;
}

static rec_input_t brenner_ein(bool laeuft)
{
    rec_input_t in = {0};
    in.burner_known = true;
    in.burner_running = laeuft;
    return in;
}

static rec_input_t speicher(float c)
{
    rec_input_t in = {0};
    in.buffer_valid = true;
    in.buffer_c = c;
    return in;
}

/* Laesst die Zeit laufen und liefert den Zeitpunkt danach. */
static uint32_t rec_laufen(rec_trigger_t *st, rec_input_t in, uint32_t t, uint32_t sekunden)
{
    rec_trig_cfg_t cfg = rec_cfg();
    for (uint32_t i = 0; i < sekunden; i++) {
        t += 1000;
        rec_trigger_tick(st, &in, &cfg, t);
    }
    return t;
}

/* Laesst den Speicher gleichmaessig steigen. */
static uint32_t speicher_steigen(rec_trigger_t *st, float von_c, float pro_stunde_k,
                                 uint32_t t, uint32_t sekunden, bool *ausgeloest)
{
    rec_trig_cfg_t cfg = rec_cfg();
    for (uint32_t i = 0; i < sekunden; i++) {
        t += 1000;
        rec_input_t in = speicher(von_c + pro_stunde_k * (float)(i + 1) / 3600.0f);
        if (rec_trigger_tick(st, &in, &cfg, t) && ausgeloest != NULL) {
            *ausgeloest = true;
        }
    }
    return t;
}

static void test_rec_trigger_auto(void)
{
    printf("Aufzeichnung: selbsttaetig ueber den Brenner\n");

    rec_trig_cfg_t cfg = rec_cfg();
    rec_trigger_t st;
    rec_trigger_init(&st);
    CHECK(st.phase == REC_TRIG_OFF, "ohne Zutun laeuft nichts");

    uint32_t t = 10000;
    rec_input_t aus = brenner_ein(false), ein = brenner_ein(true);
    rec_trigger_arm(&st, &aus, t);
    CHECK(st.phase == REC_TRIG_ARMED, "scharf geschaltet");
    CHECK(st.source == REC_SRC_BURNER, "ausgeloest wird ueber den Brenner");
    CHECK(!st.wait_off, "bei stehendem Brenner wird nicht gewartet");

    t = rec_laufen(&st, aus, t, 3600);
    CHECK(st.phase == REC_TRIG_ARMED, "ohne Brennerstart bleibt sie scharf");

    /* Der Brenner laeuft an. */
    t += 1000;
    CHECK(rec_trigger_tick(&st, &ein, &cfg, t), "der Brennerstart loest aus");
    CHECK(st.phase == REC_TRIG_RUN, "sie zeichnet auf");
    CHECK(st.automatic, "und zwar selbsttaetig");

    t = rec_laufen(&st, ein, t, 1800);
    CHECK(st.phase == REC_TRIG_RUN, "waehrend des Brennerlaufs laeuft sie weiter");
    CHECK(!st.tail, "ein Nachlauf ist das noch nicht");

    /* Brenner aus: der Nachlauf beginnt. */
    t = rec_laufen(&st, aus, t, 60);
    CHECK(st.tail, "nach dem Brenner laeuft der Nachlauf");
    CHECK(st.phase == REC_TRIG_RUN, "aufgezeichnet wird dabei weiter");
    uint32_t rest = rec_trigger_tail_left_s(&st, TAIL_S, t);
    CHECK(rest > TAIL_S - 65 && rest < TAIL_S - 55,
          "nach einer Minute ist rund eine Minute Nachlauf vorbei, nicht %u", (unsigned)rest);

    t = rec_laufen(&st, aus, t, TAIL_S);
    CHECK(st.phase == REC_TRIG_DONE, "danach ist sie fertig");
    CHECK(!st.tail, "der Nachlauf ist vorbei");
    CHECK(rec_trigger_tail_left_s(&st, TAIL_S, t) == 0, "und es bleibt keine Restzeit");
}

static void test_rec_trigger_takten(void)
{
    printf("Aufzeichnung: taktender Brenner\n");

    rec_trigger_t st;
    rec_trigger_init(&st);
    uint32_t t = 10000;
    rec_input_t aus = brenner_ein(false), ein = brenner_ein(true);

    rec_trigger_arm(&st, &aus, t);
    t = rec_laufen(&st, ein, t, 600);
    CHECK(st.phase == REC_TRIG_RUN, "sie laeuft");

    /*
     * Ein Kessel, der zwischendurch abschaltet und gleich wieder anlaeuft,
     * ist immer noch dieselbe Ladung. Waere jede Pause ein Ende, zerfiele die
     * Kurve in Bruchstuecke.
     */
    for (int i = 0; i < 4; i++) {
        t = rec_laufen(&st, aus, t, 120);
        CHECK(st.phase == REC_TRIG_RUN, "eine Pause von zwei Minuten beendet sie nicht");
        t = rec_laufen(&st, ein, t, 300);
        CHECK(!st.tail, "laeuft der Brenner wieder, ist der Nachlauf zurueckgenommen");
    }

    t = rec_laufen(&st, aus, t, TAIL_S + 5);
    CHECK(st.phase == REC_TRIG_DONE, "erst die lange Pause beendet sie");
}

static void test_rec_trigger_laufender_brenner(void)
{
    printf("Aufzeichnung: Brenner laeuft beim Scharfschalten\n");

    rec_trig_cfg_t cfg = rec_cfg();
    rec_trigger_t st;
    rec_trigger_init(&st);
    uint32_t t = 10000;
    rec_input_t aus = brenner_ein(false), ein = brenner_ein(true);

    /* Mitten in einen Lauf hinein zu beginnen ergaebe eine halbe Kurve. */
    rec_trigger_arm(&st, &ein, t);
    CHECK(st.wait_off, "gewartet wird auf den naechsten Start");

    t = rec_laufen(&st, ein, t, 1800);
    CHECK(st.phase == REC_TRIG_ARMED, "der laufende Brenner loest nicht aus");

    t = rec_laufen(&st, aus, t, 60);
    CHECK(st.phase == REC_TRIG_ARMED, "nach dessen Ende bleibt sie scharf");
    CHECK(!st.wait_off, "aber sie wartet nicht mehr");

    t += 1000;
    CHECK(rec_trigger_tick(&st, &ein, &cfg, t), "der naechste Start loest aus");
    CHECK(st.phase == REC_TRIG_RUN, "jetzt zeichnet sie auf");
}

static void test_rec_trigger_speicher(void)
{
    printf("Aufzeichnung: ueber den Anstieg des Speichers\n");

    rec_trigger_t st;
    rec_trigger_init(&st);
    uint32_t t = 10000;

    rec_input_t kalt = speicher(40.0f);
    CHECK(rec_trigger_source(&kalt) == REC_SRC_BUFFER,
          "ohne Brennerzeichen dient der Speicher als Ersatz");

    rec_trigger_arm(&st, &kalt, t);
    CHECK(st.source == REC_SRC_BUFFER, "und wird beim Scharfschalten vermerkt");

    /*
     * Ein stehender Speicher loest nicht aus, auch nicht nach Stunden. Das
     * langsame Abkuehlen einer ruhenden Anlage ebenso wenig.
     */
    bool los = false;
    t = speicher_steigen(&st, 40.0f, -1.0f, t, 7200, &los);
    CHECK(!los && st.phase == REC_TRIG_ARMED, "ein abkuehlender Speicher loest nicht aus");

    /* Eine Ladung hebt ihn um Dutzende Grad je Stunde. */
    t = speicher_steigen(&st, 33.0f, 25.0f, t, 1800, &los);
    CHECK(los, "der Anstieg einer Ladung loest aus");
    CHECK(st.phase == REC_TRIG_RUN, "sie zeichnet auf");
    CHECK(st.source == REC_SRC_BUFFER, "ueber den Speicher");

    /* Steigt er nicht mehr, ist die Ladung durch -- aber erst nach der
     * Ruhezeit, nicht schon bei der ersten Delle. */
    rec_input_t oben = speicher(58.0f);
    t = rec_laufen(&st, oben, t, 600);
    CHECK(st.phase == REC_TRIG_RUN, "zehn Minuten Stillstand beenden sie noch nicht");
    t = rec_laufen(&st, oben, t, 400);
    CHECK(st.phase == REC_TRIG_DONE, "nach einer Viertelstunde ohne Anstieg ist sie fertig");
}

static void test_rec_trigger_ohne_zeichen(void)
{
    printf("Aufzeichnung: ohne jedes Zeichen\n");

    rec_input_t leer = {0};
    CHECK(rec_trigger_source(&leer) == REC_SRC_NONE,
          "ohne Abgas- und ohne Pufferfuehler gibt es kein Zeichen");

    rec_input_t nur_brenner = brenner_ein(false);
    CHECK(rec_trigger_source(&nur_brenner) == REC_SRC_BURNER,
          "der Brenner hat Vorrang, auch wenn er gerade steht");

    rec_input_t beides = brenner_ein(false);
    beides.buffer_valid = true;
    beides.buffer_c = 50.0f;
    CHECK(rec_trigger_source(&beides) == REC_SRC_BURNER,
          "liegt beides vor, zaehlt der Brenner -- er ist das unmittelbare Zeichen");

    /*
     * Meldet sich das Geraet am Kessel spaeter, wechselt eine scharf
     * geschaltete Aufzeichnung auf den Brenner.
     */
    rec_trig_cfg_t cfg = rec_cfg();
    rec_trigger_t st;
    rec_trigger_init(&st);
    uint32_t t = 10000;
    rec_input_t nur_speicher = speicher(45.0f);
    rec_trigger_arm(&st, &nur_speicher, t);
    CHECK(st.source == REC_SRC_BUFFER, "erst ueber den Speicher");

    t += 1000;
    rec_trigger_tick(&st, &beides, &cfg, t);
    CHECK(st.source == REC_SRC_BURNER, "dann ueber den Brenner");
}

static void test_rec_trigger_hand(void)
{
    printf("Aufzeichnung: von Hand\n");

    rec_trigger_t st;
    rec_trigger_init(&st);
    uint32_t t = 10000;
    rec_input_t aus = brenner_ein(false);

    rec_trigger_manual(&st);
    CHECK(st.phase == REC_TRIG_RUN, "sie beginnt sofort");
    CHECK(!st.automatic, "und gilt nicht als selbsttaetig");

    /* Von Hand begonnen heisst auch von Hand beendet: der Brennerzustand
     * entscheidet hier nicht. */
    t = rec_laufen(&st, aus, t, 2 * TAIL_S);
    CHECK(st.phase == REC_TRIG_RUN, "der stehende Brenner beendet sie nicht");
    CHECK(!st.tail, "einen Nachlauf gibt es dabei nicht");

    rec_trigger_stop(&st);
    CHECK(st.phase == REC_TRIG_DONE, "von Hand beendet");

    /* Abbrechen im scharfen Zustand laesst nichts zurueck. */
    rec_trigger_init(&st);
    rec_trigger_arm(&st, &aus, t);
    rec_trigger_stop(&st);
    CHECK(st.phase == REC_TRIG_OFF, "eine abgebrochene Schaltung hinterlaesst nichts");
    CHECK(!st.automatic, "und gilt nicht mehr als selbsttaetig");

    /* Ein voller Puffer beendet sie ebenfalls. */
    rec_trigger_init(&st);
    rec_trigger_manual(&st);
    rec_trigger_full(&st);
    CHECK(st.phase == REC_TRIG_DONE, "ein voller Puffer beendet sie");
}

/* ------------------------------------------------------------------ */
/* Woechentlicher Termin                                               */
/* ------------------------------------------------------------------ */

#define SA 6   /* Samstag */
#define SO 0

static void test_sched_weekly(void)
{
    printf("Terminplan: woechentlich\n");

    sched_weekly_t st;
    sched_weekly_init(&st);

    /* Termin: Samstag 11 Uhr. Der Freitag geht leer aus. */
    CHECK(!sched_weekly_due(&st, SA, 11, 5, 23, 100, 2026), "am Freitag faellt nichts an");
    CHECK(!sched_weekly_due(&st, SA, 11, SA, 10, 101, 2026), "vor der Stunde noch nicht");
    CHECK(sched_weekly_due(&st, SA, 11, SA, 11, 101, 2026), "zur Stunde loest er aus");
    CHECK(!sched_weekly_due(&st, SA, 11, SA, 11, 101, 2026), "und dann am selben Tag nicht mehr");
    CHECK(!sched_weekly_due(&st, SA, 11, SA, 23, 101, 2026), "auch spaeter am Tag nicht");
    CHECK(!sched_weekly_due(&st, SA, 11, SO, 12, 102, 2026), "am Sonntag ebenso wenig");

    /* Eine Woche weiter. */
    CHECK(sched_weekly_due(&st, SA, 11, SA, 11, 108, 2026), "am naechsten Samstag wieder");

    /*
     * Verpasst wird nichts: Ein Geraet, das zur Terminstunde aus war und
     * mittags angeht, holt den Termin am selben Tag nach.
     */
    sched_weekly_init(&st);
    CHECK(sched_weekly_due(&st, SA, 11, SA, 17, 115, 2026), "nachmittags wird nachgeholt");

    /* Ohne gestellte Uhr faellt nichts an -- sonst laege der Termin nach
     * jedem Neustart im Jahr 1970. */
    sched_weekly_init(&st);
    CHECK(!sched_weekly_due(&st, SA, 11, SA, 11, 101, -1), "ohne Uhr faellt nichts an");

    /* Abgeschaltet. */
    sched_weekly_init(&st);
    CHECK(!sched_weekly_due(&st, -1, 11, SA, 11, 101, 2026), "abgeschaltet loest nicht aus");

    /*
     * Jahreswechsel: Der 1. Januar hat wieder den Tag 0. Ohne das Jahr im
     * Vergleich fiele er mit dem 1. Januar des Vorjahres zusammen.
     */
    sched_weekly_init(&st);
    CHECK(sched_weekly_due(&st, SA, 11, SA, 11, 0, 2026), "erster Samstag des Jahres");
    CHECK(!sched_weekly_due(&st, SA, 11, SA, 11, 0, 2026), "am selben Tag nur einmal");
    CHECK(sched_weekly_due(&st, SA, 11, SA, 11, 0, 2027), "im naechsten Jahr wieder");
}

static void test_sched_days_left(void)
{
    printf("Terminplan: Tage bis zum naechsten Termin\n");

    sched_weekly_t st;
    sched_weekly_init(&st);

    CHECK(sched_weekly_days_left(&st, SA, 11, SO, 8, 100, 2026) == 6,
          "vom Sonntag sind es sechs Tage, nicht %d",
          sched_weekly_days_left(&st, SA, 11, SO, 8, 100, 2026));
    CHECK(sched_weekly_days_left(&st, SA, 11, SA, 8, 106, 2026) == 0, "am Terminmorgen null");

    sched_weekly_due(&st, SA, 11, SA, 11, 106, 2026);
    CHECK(sched_weekly_days_left(&st, SA, 11, SA, 12, 106, 2026) == 7,
          "nach dem Termin sind es sieben");
    CHECK(sched_weekly_days_left(&st, -1, 11, SA, 8, 106, 2026) == -1, "abgeschaltet: -1");
    CHECK(sched_weekly_days_left(&st, SA, 11, SA, 8, 106, -1) == -1, "ohne Uhr: -1");
}

/* ------------------------------------------------------------------ */
/* Dekodierung der Funkpakete                                          */
/* ------------------------------------------------------------------ */

/* Wandelt eine Hexzeichenkette in Bytes -- so lassen sich die Beispiele der
 * Hersteller woertlich uebernehmen. */
static void hex2bin(const char *hex, uint8_t *out, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned int b = 0;
        sscanf(hex + 2 * i, "%2x", &b);
        out[i] = (uint8_t)b;
    }
}

static void test_decode_ruuvi(void)
{
    printf("Funkpakete: RuuviTag, Datensatz 5\n");

    /*
     * Die drei Beispieldatensaetze aus der Ruuvi-Spezifikation. Sie stehen
     * hier woertlich, damit ein vertauschtes Byte auffaellt, bevor ein
     * falscher Messwert in die Aufzeichnung wandert.
     */
    uint8_t d[24];
    atc_device_t dev;

    hex2bin("0512FC5394C37C0004FFFC040CAC364200CDCBB8334C884F", d, sizeof(d));
    memset(&dev, 0, sizeof(dev));
    CHECK(atc_decode_ruuvi(d, &dev), "gueltiger Datensatz wird angenommen");
    CHECK(CLOSE(dev.temp_c, 24.30f, 0.001f), "Temperatur 24,30 C, nicht %.3f", dev.temp_c);
    CHECK(CLOSE(dev.humidity, 53.49f, 0.01f), "Feuchte 53,49 %%, nicht %.2f", dev.humidity);
    CHECK(CLOSE(dev.pressure_hpa, 1000.44f, 0.01f), "Druck 1000,44 hPa, nicht %.2f",
          dev.pressure_hpa);
    CHECK(dev.battery_mv == 2977, "Spannung 2977 mV, nicht %u", dev.battery_mv);
    CHECK(dev.battery == 0, "eine Ladung in Prozent sendet der RuuviTag nicht");
    CHECK(dev.format == ATC_FMT_RUUVI, "Format ist Ruuvi");
    CHECK(dev.mac[0] == 0xCB && dev.mac[5] == 0x4F, "MAC CB:..:4F, nicht %02X:..:%02X",
          dev.mac[0], dev.mac[5]);

    /* Die Grenzwerte kommen unverfaelscht durch; ausgesiebt werden sie erst
     * von der Plausibilitaetsschranke im Empfaenger. */
    hex2bin("057FFFFFFEFFFE7FFF7FFF7FFFFFDEFEFFFECBB8334C884F", d, sizeof(d));
    memset(&dev, 0, sizeof(dev));
    CHECK(atc_decode_ruuvi(d, &dev), "Hoechstwerte werden angenommen");
    CHECK(CLOSE(dev.temp_c, 163.835f, 0.001f), "Hoechsttemperatur, nicht %.3f", dev.temp_c);

    /* "Kein Messwert" sendet Ruuvi als feste Muster. */
    hex2bin("058000FFFFFFFF800080008000FFFFFFFFFFFFFFFFFFFFFF", d, sizeof(d));
    memset(&dev, 0, sizeof(dev));
    CHECK(!atc_decode_ruuvi(d, &dev), "ohne Temperatur wird das Paket verworfen");

    /* Feuchte und Druck fehlen einzeln: das Paket bleibt gueltig. */
    hex2bin("0512FCFFFFFFFF0004FFFC040CAC364200CDCBB8334C884F", d, sizeof(d));
    memset(&dev, 0, sizeof(dev));
    CHECK(atc_decode_ruuvi(d, &dev), "ohne Feuchte und Druck bleibt der Wert brauchbar");
    CHECK(dev.humidity == 0.0f && dev.pressure_hpa == 0.0f,
          "fehlende Groessen werden zu null, nicht %.2f / %.2f", dev.humidity,
          dev.pressure_hpa);

    /* Aeltere Datensaetze werden nicht ausgewertet. */
    hex2bin("0312FC5394C37C0004FFFC040CAC364200CDCBB8334C884F", d, sizeof(d));
    memset(&dev, 0, sizeof(dev));
    CHECK(!atc_decode_ruuvi(d, &dev), "Datensatz 3 wird nicht ausgewertet");
}

static void test_decode_xiaomi(void)
{
    printf("Funkpakete: Xiaomi-Thermometer\n");

    atc_device_t dev;
    uint8_t d[16];

    /* atc1441: MAC vorwaerts, Zehntelgrad, alles Big Endian. */
    hex2bin("A4C138112233008E2C5A0B62", d, 12);
    memset(&dev, 0, sizeof(dev));
    CHECK(atc_decode_atc1441(d, &dev), "atc1441 wird angenommen");
    CHECK(CLOSE(dev.temp_c, 14.2f, 0.001f), "14,2 C, nicht %.2f", dev.temp_c);
    CHECK(CLOSE(dev.humidity, 44.0f, 0.001f), "44 %%, nicht %.2f", dev.humidity);
    CHECK(dev.battery == 90 && dev.battery_mv == 2914, "Ladung 90 %%, 2914 mV, nicht %u / %u",
          dev.battery, dev.battery_mv);
    CHECK(dev.mac[0] == 0xA4 && dev.mac[5] == 0x33, "MAC in Sendereihenfolge");
    CHECK(dev.format == ATC_FMT_ATC1441, "Format ist atc1441");

    /* pvvx: MAC rueckwaerts, Hundertstel, alles Little Endian. */
    hex2bin("33221138C1A48E053A11620B5A", d, 13);
    memset(&dev, 0, sizeof(dev));
    CHECK(atc_decode_pvvx(d, &dev), "pvvx wird angenommen");
    CHECK(CLOSE(dev.temp_c, 14.22f, 0.001f), "14,22 C, nicht %.2f", dev.temp_c);
    CHECK(CLOSE(dev.humidity, 44.10f, 0.001f), "44,10 %%, nicht %.2f", dev.humidity);
    CHECK(dev.battery == 90 && dev.battery_mv == 2914, "Ladung 90 %%, 2914 mV, nicht %u / %u",
          dev.battery, dev.battery_mv);
    /* Dieselbe MAC wie oben, nur rueckwaerts gesendet. */
    CHECK(dev.mac[0] == 0xA4 && dev.mac[5] == 0x33, "MAC wird umgedreht");
    CHECK(dev.format == ATC_FMT_PVVX, "Format ist pvvx");
}

int main(void)
{
    test_control_law();
    test_valve_travel();
    test_valve_endstop();
    test_valve_timeout();
    test_valve_reference_run();
    test_valve_force();
    test_hw_map();
    test_pump_basic();
    test_pump_min_times();
    test_pump_buffer_cold();
    test_pump_frost();
    test_pump_manual();
    test_pump_seize();
    test_demand();
    test_burner_detect();
    test_burner_hysteresis();
    test_burner_baseline();
    test_burner_consumption();
    test_burner_no_probe();
    test_charge_cycle();
    test_charge_burner_decides();
    test_charge_level();
    test_charge_limited();
    test_rec_trigger_auto();
    test_rec_trigger_takten();
    test_rec_trigger_laufender_brenner();
    test_rec_trigger_speicher();
    test_rec_trigger_ohne_zeichen();
    test_rec_trigger_hand();
    test_sched_weekly();
    test_sched_days_left();
    test_decode_ruuvi();
    test_decode_xiaomi();

    printf("\n%d Pruefungen, %d Fehler\n", s_checks, s_failed);
    return s_failed == 0 ? 0 : 1;
}
