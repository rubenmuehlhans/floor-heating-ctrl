/*
 * Prueft Regelgesetz und Ventil-Zustandsmaschine auf dem Rechner.
 *
 * Beide Module sind bewusst frei von IDF-Abhaengigkeiten, deshalb genuegt hier
 * ein gewoehnlicher C-Uebersetzer. Aufruf: make -C test/host
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hw_map.h"
#include "roomctrl.h"
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

    /* Proportionalband 1 K, Rasterung 0,1 - die Vorgabe aus dem
     * ESPHome-Aufbau. */
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

    /* Uebereinstimmung mit der urspruenglichen Formel aus der YAML. */
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

    /* Die Gruppentabelle muss zur Kanaltabelle passen - hier laege sonst der
     * Fehler, der in der YAML CH9 bis CH11 der falschen Messgruppe zugewiesen
     * hat. */
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

int main(void)
{
    test_control_law();
    test_valve_travel();
    test_valve_endstop();
    test_valve_timeout();
    test_valve_reference_run();
    test_valve_force();
    test_hw_map();

    printf("\n%d Pruefungen, %d Fehler\n", s_checks, s_failed);
    return s_failed == 0 ? 0 : 1;
}
