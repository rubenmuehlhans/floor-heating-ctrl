/*
 * 24-Stunden-Verlauf fuer die Anzeige, siehe st_verlauf.h.
 *
 * Ein Ring aus 288 Plaetzen; jede Zelle weiss, zu welchem Platz sie gehoert.
 * Wird eine Zelle fuer einen neuen Platz genommen, verliert sie alle alten
 * Werte. Werte stehen als Hundertstel in int16, INT16_MIN heisst leer.
 */
#include "st_verlauf.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "protokoll.h"
#include "st_log.h"
#include "st_web.h"

static const char *TAG = "verlauf";

#define REIHEN   (SV_FEST + SV_RAEUME)
#define LEER     INT16_MIN
#define ZEIT_AB  1700000000u

static int16_t (*s_w)[SV_PLAETZE];
static uint32_t *s_zelle;
/* alles im PSRAM, erst in sv_start angelegt: ohne PSRAM kostet der Verlauf
 * keinen Arbeitsspeicher */
static sv_raum_t *s_raum;
static int s_raum_n;
static uint32_t s_revision;
static bool s_wiederhergestellt;

/* laufender Platz: Summen und Anzahl je Reihe */
static uint32_t s_platz;
static float *s_summe;
static uint16_t *s_anzahl;

bool sv_start(void)
{
    if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) == 0) {
        return false;
    }
    s_w = heap_caps_malloc(sizeof(int16_t) * REIHEN * SV_PLAETZE, MALLOC_CAP_SPIRAM);
    s_zelle = heap_caps_calloc(SV_PLAETZE, sizeof(uint32_t), MALLOC_CAP_SPIRAM);
    s_raum = heap_caps_calloc(SV_RAEUME, sizeof(sv_raum_t), MALLOC_CAP_SPIRAM);
    s_summe = heap_caps_calloc(REIHEN, sizeof(float), MALLOC_CAP_SPIRAM);
    s_anzahl = heap_caps_calloc(REIHEN, sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (s_w == NULL || s_zelle == NULL || s_raum == NULL || s_summe == NULL || s_anzahl == NULL) {
        s_w = NULL;
        return false;
    }
    for (int r = 0; r < REIHEN; r++) {
        for (int i = 0; i < SV_PLAETZE; i++) {
            s_w[r][i] = LEER;
        }
    }
    return true;
}

bool sv_aktiv(void)
{
    return s_w != NULL;
}

uint32_t sv_platz(void)
{
    time_t t = time(NULL);
    return t > ZEIT_AB ? (uint32_t)(t / 300) : 0;
}

uint32_t sv_revision(void)
{
    return s_revision;
}

int sv_raeume(const sv_raum_t **liste)
{
    *liste = s_raum;
    return s_raum_n;
}

static int zelle(uint32_t platz)
{
    int i = (int)(platz % SV_PLAETZE);
    if (s_zelle[i] != platz) {
        s_zelle[i] = platz;
        for (int r = 0; r < REIHEN; r++) {
            s_w[r][i] = LEER;
        }
    }
    return i;
}

static void setzen(int reihe, uint32_t platz, float v)
{
    if (reihe < 0 || reihe >= REIHEN || isnan(v) || v < -300.0f || v > 300.0f) {
        return;
    }
    s_w[reihe][zelle(platz)] = (int16_t)lroundf(v * 100.0f);
}

bool sv_wert(int reihe, uint32_t platz, float *out)
{
    if (s_w == NULL || reihe < 0 || reihe >= REIHEN) {
        return false;
    }
    int i = (int)(platz % SV_PLAETZE);
    if (s_zelle[i] != platz || s_w[reihe][i] == LEER) {
        return false;
    }
    *out = s_w[reihe][i] / 100.0f;
    return true;
}

/* Reihe eines Raums, bei Bedarf neu */
static int raum_reihe(const char *geraet, uint8_t raum, const char *name)
{
    for (int i = 0; i < s_raum_n; i++) {
        if (s_raum[i].raum == raum && strcmp(s_raum[i].geraet, geraet) == 0) {
            if (name && name[0]) {
                snprintf(s_raum[i].name, sizeof(s_raum[i].name), "%s", name);
            }
            return SV_FEST + i;
        }
    }
    if (s_raum_n >= SV_RAEUME) {
        return -1;
    }
    sv_raum_t *r = &s_raum[s_raum_n];
    snprintf(r->geraet, sizeof(r->geraet), "%s", geraet);
    r->raum = raum;
    snprintf(r->name, sizeof(r->name), "%s", name && name[0] ? name : "Raum");
    return SV_FEST + s_raum_n++;
}

static bool fuehler(const st_plant_t *p, const char *rolle, float *out)
{
    for (int i = 0; i < p->heat_count; i++) {
        for (int k = 0; k < p->heat[i].probe_count; k++) {
            if (strcmp(p->heat[i].probes[k].role, rolle) == 0 && !isnan(p->heat[i].probes[k].temp_c)) {
                *out = p->heat[i].probes[k].temp_c;
                return true;
            }
        }
    }
    return false;
}

static void aufnehmen(int reihe, float v)
{
    if (reihe >= 0 && reihe < REIHEN && !isnan(v)) {
        s_summe[reihe] += v;
        s_anzahl[reihe]++;
    }
}

static void abschliessen(void)
{
    if (s_platz == 0) {
        return;
    }
    for (int r = 0; r < REIHEN; r++) {
        if (s_anzahl[r]) {
            setzen(r, s_platz, s_summe[r] / s_anzahl[r]);
        }
        s_summe[r] = 0;
        s_anzahl[r] = 0;
    }
    s_revision++;
}

void sv_abtasten(const st_plant_t *p)
{
    uint32_t platz = sv_platz();
    if (s_w == NULL || platz == 0) {
        return;
    }
    if (platz != s_platz) {
        abschliessen();
        s_platz = platz;
    }
    float v;
    if (fuehler(p, "puffer", &v)) {
        aufnehmen(SV_SPEICHER, v);
    }
    if (fuehler(p, "kessel_vl", &v)) {
        aufnehmen(SV_KESSEL, v);
    }
    if (p->outdoor.valid && p->outdoor.age_s < ST_OUTDOOR_STALE_S) {
        aufnehmen(SV_AUSSEN, p->outdoor.temp_c);
    }
    for (int i = 0; i < p->heat_count; i++) {
        const st_heat_t *h = &p->heat[i];
        if (h->burner_own && h->burner_known && h->dev.reachable) {
            aufnehmen(SV_BRENNER, h->burner_running ? 1.0f : 0.0f);
        }
    }
    for (int m = 0; m < p->manifold_count; m++) {
        const st_manifold_t *mf = &p->manifolds[m];
        if (!mf->dev.reachable) {
            continue;
        }
        for (int k = 0; k < mf->room_count; k++) {
            const st_room_t *r = &mf->rooms[k];
            int reihe = raum_reihe(mf->dev.id, r->id, r->name);
            if (r->temp_valid) {
                aufnehmen(reihe, r->temp_c);
            }
        }
    }
}

/* ------------------------------------------------------ Karte lesen */

/* Liest die Fuenfminutendateien eines Geraets fuer einen Tag. `ziel[i]` ist
 * die Reihe fuer `schluessel[i]`, -1 zum Uebergehen. */
static void datei_lesen(uint32_t tag, const char *kennung, const char *const *schluessel, const int *ziel, int n,
                        uint32_t erster, char *zeile, size_t laenge)
{
    char name[48], pfad[128];
    int16_t spalte[24];
    float werte[24];
    if (n > 24) {
        n = 24;
    }
    for (uint8_t teil = 1; teil < 20; teil++) {
        pk_dateiname(kennung, teil, true, name, sizeof(name));
        if (!st_log_datei(tag, name, pfad, sizeof(pfad))) {
            return;
        }
        FILE *f = fopen(pfad, "r");
        if (f == NULL) {
            return;
        }
        if (fgets(zeile, (int)laenge, f) != NULL && pk_spalten_waehlen(zeile, schluessel, n, spalte)) {
            uint32_t zeit;
            while (fgets(zeile, (int)laenge, f) != NULL) {
                if (!pk_zeile_lesen(zeile, spalte, n, &zeit, werte) || zeit / 300 < erster) {
                    continue;
                }
                for (int i = 0; i < n; i++) {
                    if (ziel[i] >= 0 && !isnan(werte[i])) {
                        setzen(ziel[i], zeit / 300, werte[i]);
                    }
                }
            }
        }
        fclose(f);
    }
}

void sv_wiederherstellen(const st_plant_t *p)
{
    if (s_w == NULL || s_wiederhergestellt || sv_platz() == 0 || p->heat_count + p->manifold_count == 0) {
        return;
    }
    st_log_status_t ls;
    st_log_status(&ls);
    if (!ls.karte) {
        return;
    }
    s_wiederhergestellt = true;
    char *zeile = heap_caps_malloc(ST_LOG_ZEILE_MAX, MALLOC_CAP_SPIRAM);
    if (zeile == NULL) {
        return;
    }
    uint32_t jetzt = sv_platz();
    uint32_t erster = jetzt - SV_PLAETZE + 1;
    uint32_t tage[2] = {(uint32_t)(erster * 300), (uint32_t)(jetzt * 300)};
    int n_tage = tage[0] / 86400 == tage[1] / 86400 ? 1 : 2;

    for (int t = 0; t < n_tage; t++) {
        /* Heizungsgeraete: jedes schreibt nur seine eigenen Fuehler */
        static const char *const heiz[] = {"fuehler.puffer", "fuehler.kessel_vl", "brenner"};
        static const int heiz_ziel[] = {SV_SPEICHER, SV_KESSEL, SV_BRENNER};
        for (int i = 0; i < p->heat_count; i++) {
            datei_lesen(tage[t], p->heat[i].dev.id, heiz, heiz_ziel, 3, erster, zeile, ST_LOG_ZEILE_MAX);
        }
        /* Verteiler: Raumtemperaturen */
        for (int m = 0; m < p->manifold_count; m++) {
            const st_manifold_t *mf = &p->manifolds[m];
            if (mf->room_count == 0) {
                continue;
            }
            char schl[ST_MAX_ROOMS][20];
            const char *zeiger[ST_MAX_ROOMS];
            int ziel[ST_MAX_ROOMS];
            for (int k = 0; k < mf->room_count; k++) {
                snprintf(schl[k], sizeof(schl[k]), "raum.%u.ist", mf->rooms[k].id);
                zeiger[k] = schl[k];
                ziel[k] = raum_reihe(mf->dev.id, mf->rooms[k].id, mf->rooms[k].name);
            }
            datei_lesen(tage[t], mf->dev.id, zeiger, ziel, mf->room_count, erster, zeile, ST_LOG_ZEILE_MAX);
        }
        /* Aussen: die eigene Zeile des Leitstands fuehrt jedes Funkthermometer */
        if (p->outdoor.assigned && p->outdoor.mac[0]) {
            char id[24], schl[40];
            st_device_id(id, sizeof(id));
            snprintf(schl, sizeof(schl), "funk.%s.temp", p->outdoor.mac);
            for (char *c = schl; *c; c++) {
                if (*c >= 'a' && *c <= 'f') {
                    *c = (char)(*c - 'a' + 'A');
                }
            }
            const char *zeiger[] = {schl};
            const int ziel[] = {SV_AUSSEN};
            datei_lesen(tage[t], id, zeiger, ziel, 1, erster, zeile, ST_LOG_ZEILE_MAX);
        }
    }
    free(zeile);
    s_revision++;
    ESP_LOGI(TAG, "24 Stunden von der Karte gelesen");
}
