/*
 * HomeKit-Bruecke des Leitstands, siehe st_hap.h.
 *
 * Aufbau: Die Bruecke selbst ist das Zubehoer „Leitstand"; je Raum und je
 * Fuehler haengt ein Zubehoer daran. Dessen Kennung (aid) vergibt das SDK aus
 * einem Namen wie „fbh_a1b2c3/2" und haelt sie im NVS fest, damit ein Raum
 * nach einem Neustart derselbe bleibt und Home seine Zuordnung zu Zimmern
 * behaelt.
 *
 * Eine Aufgabe gleicht jede Sekunde mit der Abfrage ab: Sie legt neu
 * erschienene Raeume an, meldet geaenderte Werte an Home und schickt
 * gesammelte Aenderungen aus Home an die Verteiler. Die Rueckrufe des SDK
 * laufen in dessen eigener Aufgabe und merken sich nur, was zu tun ist.
 *
 * Aenderungen an Home gehen nie unter der eigenen Sperre hinaus: Das SDK
 * nimmt dabei seine eigenen Sperren, und seine Rueckrufe warten auf unsere.
 */
#include "st_hap.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

#include <hap.h>
#include <hap_apple_chars.h>
#include <hap_apple_servs.h>

#include "st_config.h"
#include "st_poll.h"
#include "st_web.h"

static const char *TAG = "hap";

#define ZUBEHOER_MAX   (ST_MAX_MANIFOLD * ST_MAX_ROOMS + 3)
#define SAMMELZEIT_MS  900
/* Nach einer Aenderung aus Home gilt deren Wert, bis die Abfrage ihn zeigt,
 * hoechstens so lange; sonst springe der Regler in Home auf den alten Wert
 * zurueck, bis der Verteiler das naechste Mal abgefragt ist. */
#define HALTEZEIT_MS   60000
#define VERSUCHE       3
#define SOLL_MIN       5.0f
#define SOLL_MAX       35.0f

typedef enum { Z_RAUM, Z_AUSSEN, Z_SPEICHER, Z_KESSEL } art_t;

typedef struct {
    bool belegt;
    art_t art;
    char geraet[24];
    uint8_t raum;
    hap_char_t *ist, *soll, *zustand, *ziel, *feuchte;
    bool erreichbar;
    /* zuletzt an Home gemeldet; NAN heisst noch nie */
    float m_ist, m_soll, m_feuchte;
    int m_zustand, m_ziel;
    /* aus Home, noch nicht beim Verteiler */
    bool soll_offen, ziel_offen;
    float soll_neu;
    uint8_t ziel_neu;
    uint8_t versuche;
    int64_t faellig_ms, halten_bis_ms;
} zub_t;

/* Tabellen im PSRAM, erst beim Start angelegt: Auf dem Core Basic ohne
 * PSRAM bleibt HomeKit aus und soll keinen Arbeitsspeicher kosten. */
static zub_t *s_zub;
static SemaphoreHandle_t s_mtx;
static st_hap_stand_t s_stand = {.grund = "Noch nicht gestartet."};
static st_plant_t *s_plant;
/* Das SDK behaelt Zeiger auf die Texte der Zubehoerbeschreibung. */
static char s_name[32], s_serie[24], s_fw[32];

static int64_t jetzt_ms(void)
{
    return esp_timer_get_time() / 1000;
}

/* ---------------------------------------------------------------- Code */

bool st_hap_code_gueltig(const char *z)
{
    if (z == NULL || strlen(z) != 8) {
        return false;
    }
    bool gleich = true;
    for (int i = 0; i < 8; i++) {
        if (z[i] < '0' || z[i] > '9') {
            return false;
        }
        gleich = gleich && z[i] == z[0];
    }
    return !gleich && strcmp(z, "12345678") != 0 && strcmp(z, "87654321") != 0;
}

/* Code und Setup-ID entstehen beim ersten Start auf dem Geraet und bleiben
 * im NVS; es gibt keinen Aufkleber und keinen Code im Repository. */
static void code_laden(char code[11], char sid[5])
{
    nvs_handle_t h;
    if (nvs_open("st_hap", NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    size_t n = 11;
    char ziffern[9] = "";
    if (nvs_get_str(h, "code", code, &n) == ESP_OK && strlen(code) == 10) {
        snprintf(ziffern, sizeof(ziffern), "%.3s%.2s%.3s", code, code + 4, code + 7);
    }
    if (!st_hap_code_gueltig(ziffern)) {
        do {
            for (int i = 0; i < 8; i++) {
                ziffern[i] = (char)('0' + esp_random() % 10);
            }
            ziffern[8] = '\0';
        } while (!st_hap_code_gueltig(ziffern));
        snprintf(code, 11, "%.3s-%.2s-%.3s", ziffern, ziffern + 3, ziffern + 5);
        nvs_set_str(h, "code", code);
    }
    n = 5;
    if (nvs_get_str(h, "sid", sid, &n) != ESP_OK || strlen(sid) != 4) {
        static const char zeichen[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        for (int i = 0; i < 4; i++) {
            sid[i] = zeichen[esp_random() % 36];
        }
        sid[4] = '\0';
        nvs_set_str(h, "sid", sid);
    }
    nvs_commit(h);
    nvs_close(h);
}

/* --------------------------------------------------------- Rueckrufe */

static int identifizieren(hap_acc_t *ha)
{
    (void)ha;
    ESP_LOGI(TAG, "Identifizieren angefordert");
    return HAP_SUCCESS;
}

/* Ein nicht erreichbarer Verteiler zeigt in Home „Keine Antwort" statt
 * veralteter Werte. */
static int lesen(hap_char_t *hc, hap_status_t *status, void *serv_priv, void *read_priv)
{
    (void)hc;
    (void)read_priv;
    zub_t *z = serv_priv;
    bool da = true;
    if (xSemaphoreTake(s_mtx, pdMS_TO_TICKS(200)) == pdTRUE) {
        da = z->erreichbar;
        xSemaphoreGive(s_mtx);
    }
    *status = da ? HAP_STATUS_SUCCESS : HAP_STATUS_COMM_ERR;
    return da ? HAP_SUCCESS : HAP_FAIL;
}

/* Auf 0,5 K und in den erlaubten Bereich. Eigene Funktion: Inline im
 * Rueckruf bringt GCC 13.2 fuer Xtensa bei -O2 zum Absturz. */
static float __attribute__((noinline)) soll_runden(float x)
{
    float t = floorf(x * 2.0f + 0.5f) / 2.0f;
    return fminf(fmaxf(t, SOLL_MIN), SOLL_MAX);
}

static int schreiben(hap_write_data_t w[], int n, void *serv_priv, void *write_priv)
{
    (void)write_priv;
    zub_t *z = serv_priv;
    if (xSemaphoreTake(s_mtx, pdMS_TO_TICKS(500)) != pdTRUE) {
        for (int i = 0; i < n; i++) {
            *(w[i].status) = HAP_STATUS_RES_BUSY;
        }
        return HAP_FAIL;
    }
    int rc = HAP_SUCCESS;
    for (int i = 0; i < n; i++) {
        const char *uuid = hap_char_get_type_uuid(w[i].hc);
        if (!z->erreichbar) {
            *(w[i].status) = HAP_STATUS_COMM_ERR;
            rc = HAP_FAIL;
        } else if (strcmp(uuid, HAP_CHAR_UUID_TARGET_TEMPERATURE) == 0) {
            float t = soll_runden(w[i].val.f);
            z->soll_neu = t;
            z->soll_offen = true;
            z->m_soll = t;
            z->faellig_ms = jetzt_ms() + SAMMELZEIT_MS;
            z->halten_bis_ms = jetzt_ms() + HALTEZEIT_MS;
            z->versuche = 0;
            hap_val_t v = {.f = t};
            hap_char_update_val(w[i].hc, &v);
            *(w[i].status) = HAP_STATUS_SUCCESS;
        } else if (strcmp(uuid, HAP_CHAR_UUID_TARGET_HEATING_COOLING_STATE) == 0) {
            if (w[i].val.u > 1) {
                *(w[i].status) = HAP_STATUS_VAL_INVALID;
                rc = HAP_FAIL;
                continue;
            }
            z->ziel_neu = (uint8_t)w[i].val.u;
            z->ziel_offen = true;
            z->m_ziel = z->ziel_neu;
            z->faellig_ms = jetzt_ms() + SAMMELZEIT_MS;
            z->halten_bis_ms = jetzt_ms() + HALTEZEIT_MS;
            z->versuche = 0;
            hap_char_update_val(w[i].hc, &w[i].val);
            *(w[i].status) = HAP_STATUS_SUCCESS;
        } else if (strcmp(uuid, HAP_CHAR_UUID_TEMPERATURE_DISPLAY_UNITS) == 0) {
            /* Anzeige in Home; der Verteiler kennt nur Celsius */
            hap_char_update_val(w[i].hc, &w[i].val);
            *(w[i].status) = HAP_STATUS_SUCCESS;
        } else {
            *(w[i].status) = HAP_STATUS_RES_ABSENT;
            rc = HAP_FAIL;
        }
    }
    xSemaphoreGive(s_mtx);
    return rc;
}

/* ---------------------------------------------------------- Zubehoer */

static hap_acc_t *zubehoer_neu(const char *name, const char *modell)
{
    hap_acc_cfg_t cfg = {
        .name = (char *)name,
        .manufacturer = "floor-heating-ctrl",
        .model = (char *)modell,
        .serial_num = s_serie,
        .fw_rev = s_fw,
        .hw_rev = NULL,
        .pv = "1.1.0",
        .identify_routine = identifizieren,
        .cid = HAP_CID_BRIDGE,
    };
    return hap_acc_create(&cfg);
}

static zub_t *platz(void)
{
    for (int i = 0; i < ZUBEHOER_MAX; i++) {
        if (!s_zub[i].belegt) {
            memset(&s_zub[i], 0, sizeof(s_zub[i]));
            s_zub[i].m_ist = s_zub[i].m_soll = s_zub[i].m_feuchte = NAN;
            s_zub[i].m_zustand = s_zub[i].m_ziel = -1;
            return &s_zub[i];
        }
    }
    return NULL;
}

static void raum_anlegen(const st_manifold_t *m, const st_room_t *r)
{
    zub_t *z = platz();
    if (z == NULL) {
        return;
    }
    char kennung[32], name[48];
    snprintf(kennung, sizeof(kennung), "%s/%u", m->dev.id, r->id);
    snprintf(name, sizeof(name), "%s", r->name[0] ? r->name : "Raum");

    hap_acc_t *acc = zubehoer_neu(name, "Raumthermostat");
    hap_serv_t *s = hap_serv_thermostat_create(0, r->heat ? 1 : 0, r->temp_valid ? r->temp_c : 20.0f,
                                               r->target_c >= SOLL_MIN ? r->target_c : 20.0f, 0);
    hap_serv_add_char(s, hap_char_name_create(name));
    if (r->hum_valid) {
        hap_serv_add_char(s, hap_char_current_relative_humidity_create(r->humidity));
    }
    z->ist = hap_serv_get_char_by_uuid(s, HAP_CHAR_UUID_CURRENT_TEMPERATURE);
    z->soll = hap_serv_get_char_by_uuid(s, HAP_CHAR_UUID_TARGET_TEMPERATURE);
    z->zustand = hap_serv_get_char_by_uuid(s, HAP_CHAR_UUID_CURRENT_HEATING_COOLING_STATE);
    z->ziel = hap_serv_get_char_by_uuid(s, HAP_CHAR_UUID_TARGET_HEATING_COOLING_STATE);
    z->feuchte = hap_serv_get_char_by_uuid(s, HAP_CHAR_UUID_CURRENT_RELATIVE_HUMIDITY);
    /* Nur aus und heizen; Kuehlen und Automatik gibt es nicht. */
    static const uint8_t ziele[] = {0, 1};
    hap_char_add_valid_vals(z->ziel, ziele, sizeof(ziele));
    static const uint8_t zustaende[] = {0, 1};
    hap_char_add_valid_vals(z->zustand, zustaende, sizeof(zustaende));
    hap_char_float_set_constraints(z->soll, SOLL_MIN, SOLL_MAX, 0.5f);

    z->belegt = true;
    z->art = Z_RAUM;
    snprintf(z->geraet, sizeof(z->geraet), "%s", m->dev.id);
    z->raum = r->id;
    z->erreichbar = m->dev.reachable;
    hap_serv_set_priv(s, z);
    hap_serv_set_write_cb(s, schreiben);
    hap_serv_set_read_cb(s, lesen);
    hap_acc_add_serv(acc, s);
    hap_add_bridged_accessory(acc, hap_get_unique_aid(kennung));
    ESP_LOGI(TAG, "Raum %s (%s) angelegt", name, kennung);
}

static void fuehler_anlegen(art_t art, bool mit_feuchte)
{
    zub_t *z = platz();
    if (z == NULL) {
        return;
    }
    const char *name = art == Z_AUSSEN ? "Außen" : art == Z_SPEICHER ? "Pufferspeicher" : "Kesselvorlauf";
    const char *kennung = art == Z_AUSSEN ? "aussen" : art == Z_SPEICHER ? "puffer" : "kessel_vl";
    hap_acc_t *acc = zubehoer_neu(name, "Temperaturfühler");
    hap_serv_t *s = hap_serv_temperature_sensor_create(0);
    hap_serv_add_char(s, hap_char_name_create((char *)name));
    z->ist = hap_serv_get_char_by_uuid(s, HAP_CHAR_UUID_CURRENT_TEMPERATURE);
    /* Home erlaubt sonst nur 0 bis 100 °C; draussen wird es kaelter. */
    hap_char_float_set_constraints(z->ist, -40.0f, 120.0f, 0.1f);
    hap_serv_set_priv(s, z);
    hap_serv_set_read_cb(s, lesen);
    hap_acc_add_serv(acc, s);
    if (mit_feuchte) {
        hap_serv_t *f = hap_serv_humidity_sensor_create(0);
        hap_serv_add_char(f, hap_char_name_create("Feuchte außen"));
        z->feuchte = hap_serv_get_char_by_uuid(f, HAP_CHAR_UUID_CURRENT_RELATIVE_HUMIDITY);
        hap_serv_set_priv(f, z);
        hap_serv_set_read_cb(f, lesen);
        hap_acc_add_serv(acc, f);
    }
    z->belegt = true;
    z->art = art;
    z->erreichbar = true;
    hap_add_bridged_accessory(acc, hap_get_unique_aid(kennung));
    ESP_LOGI(TAG, "Fuehler %s angelegt", name);
}

static zub_t *finden_raum(const char *geraet, uint8_t raum)
{
    for (int i = 0; i < ZUBEHOER_MAX; i++) {
        if (s_zub[i].belegt && s_zub[i].art == Z_RAUM && s_zub[i].raum == raum && strcmp(s_zub[i].geraet, geraet) == 0) {
            return &s_zub[i];
        }
    }
    return NULL;
}

static zub_t *finden_art(art_t art)
{
    for (int i = 0; i < ZUBEHOER_MAX; i++) {
        if (s_zub[i].belegt && s_zub[i].art == art) {
            return &s_zub[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------ Abgleich */

typedef struct {
    hap_char_t *hc;
    hap_val_t v;
} meldung_t;

#define MELDUNGEN_MAX (ZUBEHOER_MAX * 5)
static meldung_t *s_meldungen;
static int s_anzahl;

static void melden_f(hap_char_t *hc, float *merk, float neu, float schwelle)
{
    if (hc == NULL || isnan(neu) || (!isnan(*merk) && fabsf(*merk - neu) < schwelle) || s_anzahl >= MELDUNGEN_MAX) {
        return;
    }
    *merk = neu;
    s_meldungen[s_anzahl++] = (meldung_t){.hc = hc, .v = {.f = neu}};
}

static void melden_u(hap_char_t *hc, int *merk, int neu)
{
    if (hc == NULL || *merk == neu || s_anzahl >= MELDUNGEN_MAX) {
        return;
    }
    *merk = neu;
    s_meldungen[s_anzahl++] = (meldung_t){.hc = hc, .v = {.u = (uint32_t)neu}};
}

static bool fuehler(const st_plant_t *p, const char *rolle, float *wert)
{
    for (int i = 0; i < p->heat_count; i++) {
        const st_heat_t *h = &p->heat[i];
        for (int k = 0; k < h->probe_count; k++) {
            if (strcmp(h->probes[k].role, rolle) == 0 && !isnan(h->probes[k].temp_c)) {
                *wert = h->probes[k].temp_c;
                return true;
            }
        }
    }
    return false;
}

/*
 * Entfernt Raeume, die ihr Verteiler nicht mehr fuehrt -- etwa nachdem eine
 * Platine zum reinen Aussenfuehler wurde und ihren Platzhalterraum verlor.
 * Geurteilt wird nur ueber Verteiler, deren letzte Abfrage ankam; ein
 * verstummter Verteiler behaelt seine Raeume, sie zeigen dann "Keine Antwort".
 */
static void verwaiste_entfernen(const st_plant_t *p)
{
    for (int i = 0; i < ZUBEHOER_MAX; i++) {
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        zub_t *z = &s_zub[i];
        bool weg = false;
        char kennung[32] = "";
        if (z->belegt && z->art == Z_RAUM) {
            for (int k = 0; k < p->manifold_count; k++) {
                const st_manifold_t *m = &p->manifolds[k];
                if (strcmp(m->dev.id, z->geraet) != 0 || !m->dev.reachable) {
                    continue;
                }
                weg = true;
                for (int r = 0; r < m->room_count; r++) {
                    if (m->rooms[r].id == z->raum) {
                        weg = false;
                        break;
                    }
                }
            }
            if (weg) {
                snprintf(kennung, sizeof(kennung), "%s/%u", z->geraet, z->raum);
                z->belegt = false;
            }
        }
        xSemaphoreGive(s_mtx);
        if (!weg) {
            continue;
        }
        hap_acc_t *acc = hap_acc_get_by_aid(hap_get_unique_aid(kennung));
        /* Das SDK zaehlt die Konfigurationsnummer dabei selbst hoch, Home
         * laedt die Liste dann neu. */
        if (acc != NULL) {
            hap_remove_bridged_accessory(acc);
            hap_acc_delete(acc);
        }
        ESP_LOGI(TAG, "Raum %s entfernt, der Verteiler fuehrt ihn nicht mehr", kennung);
    }
}

/* Legt fehlende Zubehoere an; das geschieht ausserhalb der Sperre, weil das
 * SDK dabei selbst sperrt und in den NVS schreibt. */
static void anlegen(const st_plant_t *p)
{
    for (int i = 0; i < p->manifold_count; i++) {
        const st_manifold_t *m = &p->manifolds[i];
        for (int k = 0; k < m->room_count; k++) {
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            bool da = finden_raum(m->dev.id, m->rooms[k].id) != NULL;
            xSemaphoreGive(s_mtx);
            if (!da) {
                raum_anlegen(m, &m->rooms[k]);
            }
        }
    }
    float t;
    if (p->outdoor.valid && finden_art(Z_AUSSEN) == NULL) {
        fuehler_anlegen(Z_AUSSEN, p->outdoor.hum_valid);
    }
    if (fuehler(p, "puffer", &t) && finden_art(Z_SPEICHER) == NULL) {
        fuehler_anlegen(Z_SPEICHER, false);
    }
    if (fuehler(p, "kessel_vl", &t) && finden_art(Z_KESSEL) == NULL) {
        fuehler_anlegen(Z_KESSEL, false);
    }
}

static void abgleichen(const st_plant_t *p)
{
    int64_t jetzt = jetzt_ms();
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_anzahl = 0;
    for (int i = 0; i < p->manifold_count; i++) {
        const st_manifold_t *m = &p->manifolds[i];
        for (int k = 0; k < m->room_count; k++) {
            const st_room_t *r = &m->rooms[k];
            zub_t *z = finden_raum(m->dev.id, r->id);
            if (z == NULL) {
                continue;
            }
            z->erreichbar = m->dev.reachable;
            if (!z->erreichbar) {
                continue;
            }
            if (r->temp_valid) {
                melden_f(z->ist, &z->m_ist, r->temp_c, 0.05f);
            }
            if (r->hum_valid) {
                melden_f(z->feuchte, &z->m_feuchte, r->humidity, 0.5f);
            }
            /* Solange eine Aenderung aus Home unterwegs ist, gilt sie. */
            bool halten = jetzt < z->halten_bis_ms || z->soll_offen || z->ziel_offen;
            bool bestaetigt = (!z->soll_offen && (r->target_c < SOLL_MIN || fabsf(r->target_c - z->m_soll) < 0.05f))
                              && (!z->ziel_offen && (r->heat ? 1 : 0) == z->m_ziel);
            if (!halten || bestaetigt) {
                z->halten_bis_ms = 0;
                if (r->target_c >= SOLL_MIN) {
                    melden_f(z->soll, &z->m_soll, r->target_c, 0.05f);
                }
                melden_u(z->ziel, &z->m_ziel, r->heat ? 1 : 0);
            }
            melden_u(z->zustand, &z->m_zustand, r->heat && r->position > 0.05f ? 1 : 0);
        }
    }
    zub_t *a = finden_art(Z_AUSSEN);
    if (a != NULL) {
        a->erreichbar = p->outdoor.valid && p->outdoor.age_s < ST_OUTDOOR_STALE_S;
        if (a->erreichbar) {
            melden_f(a->ist, &a->m_ist, p->outdoor.temp_c, 0.05f);
            if (p->outdoor.hum_valid) {
                melden_f(a->feuchte, &a->m_feuchte, p->outdoor.humidity, 0.5f);
            }
        }
    }
    const art_t arten[] = {Z_SPEICHER, Z_KESSEL};
    const char *rollen[] = {"puffer", "kessel_vl"};
    for (int i = 0; i < 2; i++) {
        zub_t *f = finden_art(arten[i]);
        float t;
        if (f != NULL) {
            f->erreichbar = fuehler(p, rollen[i], &t);
            if (f->erreichbar) {
                melden_f(f->ist, &f->m_ist, t, 0.05f);
            }
        }
    }
    int n = s_anzahl;
    xSemaphoreGive(s_mtx);
    for (int i = 0; i < n; i++) {
        hap_char_update_val(s_meldungen[i].hc, &s_meldungen[i].v);
    }
}

/* ------------------------------------------------------- Schreibweg */

static bool senden(const char *host, uint8_t raum, const char *was, const char *rumpf)
{
    char url[80];
    snprintf(url, sizeof(url), "http://%s/api/room/%u/%s", host, raum, was);
    esp_http_client_config_t c = {.url = url, .method = HTTP_METHOD_POST, .timeout_ms = 4000, .disable_auto_redirect = true};
    esp_http_client_handle_t h = esp_http_client_init(&c);
    if (h == NULL) {
        return false;
    }
    esp_http_client_set_header(h, "Content-Type", "application/json");
    esp_http_client_set_post_field(h, rumpf, (int)strlen(rumpf));
    esp_err_t rc = esp_http_client_perform(h);
    int status = esp_http_client_get_status_code(h);
    esp_http_client_cleanup(h);
    if (rc != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "%s: %s, Status %d", url, esp_err_to_name(rc), status);
        return false;
    }
    ESP_LOGI(TAG, "%s %s", url, rumpf);
    return true;
}

typedef struct {
    char host[16];
    uint8_t raum;
    bool soll, ziel;
    float soll_neu;
    uint8_t ziel_neu;
    zub_t *z;
} auftrag_t;

static void ausliefern(const st_plant_t *p)
{
    int64_t jetzt = jetzt_ms();
    auftrag_t a[ZUBEHOER_MAX];
    int n = 0;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    for (int i = 0; i < ZUBEHOER_MAX; i++) {
        zub_t *z = &s_zub[i];
        if (!z->belegt || z->art != Z_RAUM || (!z->soll_offen && !z->ziel_offen) || jetzt < z->faellig_ms) {
            continue;
        }
        for (int k = 0; k < p->manifold_count; k++) {
            if (strcmp(p->manifolds[k].dev.id, z->geraet) == 0) {
                a[n] = (auftrag_t){.raum = z->raum, .soll = z->soll_offen, .ziel = z->ziel_offen,
                                   .soll_neu = z->soll_neu, .ziel_neu = z->ziel_neu, .z = z};
                snprintf(a[n].host, sizeof(a[n].host), "%s", p->manifolds[k].dev.host);
                n++;
                break;
            }
        }
    }
    xSemaphoreGive(s_mtx);

    for (int i = 0; i < n; i++) {
        char rumpf[48];
        /* Erst die Betriebsart: Ein Sollwert fuer einen ausgeschalteten Raum
         * waere sonst wirkungslos. */
        bool ok = true;
        if (a[i].ziel) {
            snprintf(rumpf, sizeof(rumpf), "{\"mode\":\"%s\"}", a[i].ziel_neu ? "heat" : "off");
            ok = senden(a[i].host, a[i].raum, "mode", rumpf);
        }
        bool ok_soll = true;
        if (ok && a[i].soll) {
            snprintf(rumpf, sizeof(rumpf), "{\"target_c\":%.1f}", (double)a[i].soll_neu);
            ok_soll = senden(a[i].host, a[i].raum, "target", rumpf);
        }
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        zub_t *z = a[i].z;
        /* Nur zuruecksetzen, was in der Zwischenzeit nicht erneut kam */
        if (ok && a[i].ziel && z->ziel_neu == a[i].ziel_neu) {
            z->ziel_offen = false;
        }
        if (ok && ok_soll && a[i].soll && fabsf(z->soll_neu - a[i].soll_neu) < 0.01f) {
            z->soll_offen = false;
        }
        if (z->soll_offen || z->ziel_offen) {
            if (++z->versuche >= VERSUCHE) {
                ESP_LOGW(TAG, "Aenderung fuer %s/%u nach %d Versuchen verworfen", z->geraet, z->raum, VERSUCHE);
                z->soll_offen = z->ziel_offen = false;
                z->halten_bis_ms = 0;
            } else {
                z->faellig_ms = jetzt_ms() + 2000;
            }
        }
        xSemaphoreGive(s_mtx);
    }
}

static void aufgabe(void *arg)
{
    (void)arg;
    uint32_t revision = UINT32_MAX;
    for (;;) {
        uint32_t neu = st_poll_revision();
        bool offen = false;
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        for (int i = 0; i < ZUBEHOER_MAX && !offen; i++) {
            offen = s_zub[i].belegt && (s_zub[i].soll_offen || s_zub[i].ziel_offen);
        }
        xSemaphoreGive(s_mtx);
        if (neu != revision || offen) {
            st_poll_snapshot(s_plant);
            verwaiste_entfernen(s_plant);
            anlegen(s_plant);
            abgleichen(s_plant);
            ausliefern(s_plant);
            revision = neu;
        }
        int gekoppelt = hap_get_paired_controller_count();
        int zubehoer = 0;
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        for (int i = 0; i < ZUBEHOER_MAX; i++) {
            zubehoer += s_zub[i].belegt;
        }
        s_stand.steuerungen = gekoppelt > 0 ? (uint8_t)gekoppelt : 0;
        s_stand.zubehoer = (uint8_t)zubehoer;
        xSemaphoreGive(s_mtx);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* ------------------------------------------------------------- Start */

esp_err_t st_hap_start(void)
{
    s_mtx = xSemaphoreCreateMutex();
    if (s_mtx == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) < 1024 * 1024) {
        s_stand.grund = "HomeKit braucht PSRAM und läuft deshalb erst auf dem Core2.";
        ESP_LOGI(TAG, "Kein PSRAM, HomeKit bleibt aus");
        return ESP_OK;
    }
    s_plant = heap_caps_calloc(1, sizeof(st_plant_t), MALLOC_CAP_SPIRAM);
    s_zub = heap_caps_calloc(ZUBEHOER_MAX, sizeof(zub_t), MALLOC_CAP_SPIRAM);
    s_meldungen = heap_caps_calloc(MELDUNGEN_MAX, sizeof(meldung_t), MALLOC_CAP_SPIRAM);
    if (s_plant == NULL || s_zub == NULL || s_meldungen == NULL) {
        s_stand.grund = "Kein Speicher für den Stand der Anlage.";
        return ESP_ERR_NO_MEM;
    }

    hap_cfg_t hc;
    hap_get_config(&hc);
    hc.unique_param = UNIQUE_NONE;
    /* Jede Eigenschaft, die Home beobachtet, belegt einen Platz: je Raum bis
     * zu fuenf, dazu die Fuehler. */
    hc.max_event_notif_chars = MELDUNGEN_MAX > 250 ? 250 : MELDUNGEN_MAX;
    hap_set_config(&hc);
    if (hap_init(HAP_TRANSPORT_WIFI) != HAP_SUCCESS) {
        s_stand.grund = "HomeKit ließ sich nicht einrichten.";
        return ESP_FAIL;
    }

    st_config_t cfg;
    st_cfg_copy(&cfg);
    snprintf(s_name, sizeof(s_name), "%s", cfg.site[0] ? cfg.site : "Leitstand");
    st_device_id(s_serie, sizeof(s_serie));
    snprintf(s_fw, sizeof(s_fw), "%s", esp_app_get_description()->version);
    /* HomeKit erwartet x.y.z; eine Beschreibung von git wie v0.4.0-1-gabc
     * wird auf die Zahlen gekuerzt. */
    char *p = s_fw[0] == 'v' ? s_fw + 1 : s_fw;
    memmove(s_fw, p, strlen(p) + 1);
    s_fw[strcspn(s_fw, "-+ ")] = '\0';

    hap_acc_t *bruecke = zubehoer_neu(s_name, "Leitstand");
    hap_acc_add_wifi_transport_service(bruecke, 0);
    hap_add_accessory(bruecke);

    char sid[5] = "";
    code_laden(s_stand.code, sid);
    hap_set_setup_code(s_stand.code);
    hap_set_setup_id(sid);
    char *nutzlast = esp_hap_get_setup_payload(s_stand.code, sid, false, HAP_CID_BRIDGE);
    if (nutzlast != NULL) {
        snprintf(s_stand.nutzlast, sizeof(s_stand.nutzlast), "%s", nutzlast);
        free(nutzlast);
    }

    /* Zubehoer, das die Abfrage schon kennt, vor dem Start; spaetere Raeume
     * kommen im Betrieb hinzu und erhoehen die Konfigurationsnummer. */
    st_poll_snapshot(s_plant);
    anlegen(s_plant);

    if (hap_start() != HAP_SUCCESS) {
        s_stand.grund = "HomeKit ließ sich nicht starten.";
        return ESP_FAIL;
    }
    if (xTaskCreate(aufgabe, "hap_abgleich", 6144, NULL, 4, NULL) != pdPASS) {
        s_stand.grund = "Kein Speicher für den Abgleich.";
        return ESP_ERR_NO_MEM;
    }
    s_stand.aktiv = true;
    s_stand.grund = NULL;
    ESP_LOGI(TAG, "HomeKit laeuft, %s", s_stand.nutzlast);
    return ESP_OK;
}

void st_hap_stand(st_hap_stand_t *out)
{
    if (s_mtx != NULL) {
        xSemaphoreTake(s_mtx, portMAX_DELAY);
    }
    *out = s_stand;
    if (s_mtx != NULL) {
        xSemaphoreGive(s_mtx);
    }
}

esp_err_t st_hap_kopplungen_loeschen(void)
{
    if (!s_stand.aktiv) {
        return ESP_ERR_INVALID_STATE;
    }
    return hap_reset_pairings() == HAP_SUCCESS ? ESP_OK : ESP_FAIL;
}
