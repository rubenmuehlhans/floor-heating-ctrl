#include "st_log.h"

#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "atc_ble.h"
#include "driver/sdspi_host.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "netmgr.h"
#include "protokoll.h"
#include "sdmmc_cmd.h"
#include "st_config.h"
#include "st_poll.h"
#include "st_ui.h"
#include "st_web.h"

static const char *TAG = "log";

#define EINHAENGEN "/sd"
/* zwei Heizungsgeraete, vier Verteiler, der Leitstand selbst */
#define GERAETE_MAX 7
/* Zustaende alle 15 Minuten */
#define ZUSTAND_S 900
/* Zeile des Leitstands selbst */
#define EIGEN_S 30
/* Die Funkthermometer melden sich nach dem Start nach und nach. Die eigene
 * Zeile beginnt erst danach, sonst begaenne mit jedem neu gehoerten Geraet
 * eine neue Datei. */
#define EIGEN_ANLAUF_S 300
#define FUNK_MAX 12
/*
 * Kopf der eigenen Zeile im unguenstigsten Fall: je Thermometer vier Spalten
 * ",funk.<Adresse>.<Groesse>", dazu die drei Spalten des Geraets. Bei zwoelf
 * Thermometern sind das gut 1470 Byte. Bis 0.5.0 war der Puffer 1280 Byte
 * gross; ab zehn Thermometern in Reichweite fehlte die eigene Zeile dann
 * ganz, mit der Meldung "Kopf zu lang".
 */
#define FUNK_SPALTE(groesse) (sizeof(",funk.00:00:00:00:00:00.") - 1 + sizeof(groesse) - 1)
#define FUNK_KOPF                                                                                  \
    (FUNK_SPALTE("temp") + FUNK_SPALTE("feuchte") + FUNK_SPALTE("batterie") + FUNK_SPALTE("rssi"))
#define EIGEN_KOPF_MAX                                                                             \
    (sizeof("zeit") - 1 + FUNK_MAX * FUNK_KOPF + sizeof(",geraet.heap,geraet.rssi,geraet.laufzeit") - 1 + 2)
/* Freier Platz, unter dem die aeltesten Dateien geloescht werden */
#define FREI_PROZENT 10

typedef struct {
    bool belegt;
    char id[24];
    char ort[32];
    char version[24];
    bool heiz;
    uint32_t laufzeit;
    bool laufzeit_bekannt;
    pk_reihe_t reihe;
    uint32_t tag;          /* Tag, fuer den teil gilt; 0 = unbekannt */
    uint8_t teil;
    uint32_t zustand_zeit; /* letzter Eintrag in zustaende.jsonl */
    char befunde[64];
    bool befunde_bekannt;
    uint32_t protokoll_tag; /* Tag, an dem die Protokolle uebernommen wurden */
} geraet_t;

static SemaphoreHandle_t s_mtx;
static geraet_t s_g[GERAETE_MAX];
static sdmmc_card_t *s_karte;
static bool s_bereit;
static bool s_geraete_neu;      /* geraete.json neu schreiben */
static uint32_t s_geraete_tag;
static char s_tag_angelegt[24];  /* zuletzt angelegtes Tagesverzeichnis */
static st_log_status_t s_status;
static uint32_t s_heute_tag;

/* Eine Zeile, ein Kopf oder die erste Zeile einer Datei. Nur unter s_mtx.
 * Der laengste Kopf ist der der eigenen Zeile, siehe EIGEN_KOPF_MAX; am
 * Verteiler Erdgeschoss mit elf Raeumen sind es 700 Byte. */
static char s_zeile[ST_LOG_ZEILE_MAX];
_Static_assert(EIGEN_KOPF_MAX <= sizeof(s_zeile), "Kopf der eigenen Zeile passt nicht in s_zeile");
static char s_ereignis[320];
static char s_pfad[128];

static uint32_t jetzt(void)
{
    time_t t = time(NULL);
    return t > 1700000000 ? (uint32_t)t : 0;
}

/* ------------------------------------------------------------------ */
/* Dateien                                                             */
/* ------------------------------------------------------------------ */

static void fehler(const char *was)
{
    s_status.fehler++;
    snprintf(s_status.meldung, sizeof(s_status.meldung), "%s", was);
    if (s_status.fehler < 20 || s_status.fehler % 100 == 0) {
        ESP_LOGW(TAG, "%s (errno %d)", was, errno);
    }
}

static void verzeichnis(const char *p)
{
    if (mkdir(p, 0775) != 0 && errno != EEXIST) {
        fehler("Verzeichnis nicht angelegt");
    }
}

/* Legt /sd/protokoll/<jahr>/<tag> an und schreibt den Pfad einer Datei darin */
static bool tagespfad(uint32_t zeit, const char *datei, char *out, size_t len)
{
    char tag[24];
    pk_tagespfad(zeit, tag, sizeof(tag));
    if (strcmp(tag, s_tag_angelegt) != 0) {
        char p[64];
        verzeichnis(ST_LOG_WURZEL);
        snprintf(p, sizeof(p), "%s/%.4s", ST_LOG_WURZEL, tag);
        verzeichnis(p);
        snprintf(p, sizeof(p), "%s/%s", ST_LOG_WURZEL, tag);
        verzeichnis(p);
        snprintf(s_tag_angelegt, sizeof(s_tag_angelegt), "%s", tag);
    }
    int n = snprintf(out, len, "%s/%s/%s", ST_LOG_WURZEL, tag, datei);
    return n > 0 && (size_t)n < len;
}

static void zaehlen(size_t n)
{
    uint32_t t = jetzt();
    if (pk_tag(t) != s_heute_tag) {
        s_heute_tag = pk_tag(t);
        s_status.heute_byte = 0;
    }
    s_status.heute_byte += n;
    s_status.zuletzt = t;
}

static void karte_pruefen(void);

static bool anhaengen(const char *pfad, const char *text, size_t n)
{
    FILE *f = fopen(pfad, "a");
    if (f == NULL) {
        fehler("Datei nicht zu oeffnen");
        karte_pruefen();
        return false;
    }
    size_t w = fwrite(text, 1, n, f);
    int e = fclose(f);
    zaehlen(w);
    if (w != n || e != 0) {
        fehler("Schreiben unvollstaendig");
        karte_pruefen();
        return false;
    }
    return true;
}

static void ereignis(uint32_t zeit, const char *geraet, const char *art, const char *felder)
{
    size_t n = pk_ereignis(s_ereignis, sizeof(s_ereignis), zeit, geraet, art, felder);
    if (n > 0 && tagespfad(zeit, "ereignisse.jsonl", s_pfad, sizeof(s_pfad))) {
        anhaengen(s_pfad, s_ereignis, n);
    }
}

static void geraete_schreiben(uint32_t zeit)
{
    if (!tagespfad(zeit, "geraete.json", s_pfad, sizeof(s_pfad))) {
        return;
    }
    FILE *f = fopen(s_pfad, "w");
    if (f == NULL) {
        fehler("geraete.json nicht zu schreiben");
        return;
    }
    fputs("{\"geraete\":[", f);
    bool erstes = true;
    for (int i = 0; i < GERAETE_MAX; i++) {
        const geraet_t *g = &s_g[i];
        if (!g->belegt) {
            continue;
        }
        const char *art = g->heiz ? "heat" : (strncmp(g->id, "lst_", 4) == 0 ? "station" : "manifold");
        fprintf(f, "%s\n{\"kennung\":\"%s\",\"ort\":\"%s\",\"art\":\"%s\",\"version\":\"%s\"}",
                erstes ? "" : ",", g->id, g->ort, art, g->version);
        erstes = false;
    }
    fputs("\n]}\n", f);
    fclose(f);
    s_geraete_neu = false;
    s_geraete_tag = pk_tag(zeit);
}

static geraet_t *geraet(const char *id)
{
    geraet_t *frei = NULL;
    for (int i = 0; i < GERAETE_MAX; i++) {
        if (s_g[i].belegt && strcmp(s_g[i].id, id) == 0) {
            return &s_g[i];
        }
        if (!s_g[i].belegt && frei == NULL) {
            frei = &s_g[i];
        }
    }
    if (frei != NULL) {
        memset(frei, 0, sizeof(*frei));
        frei->belegt = true;
        snprintf(frei->id, sizeof(frei->id), "%s", id);
        pk_reihe_init(&frei->reihe);
        s_geraete_neu = true;
    }
    return frei;
}

/* ------------------------------------------------------------------ */
/* Teile und Kopf                                                      */
/* ------------------------------------------------------------------ */

static bool vorhanden(const char *pfad)
{
    struct stat st;
    return stat(pfad, &st) == 0;
}

/* Hoechster vorhandener Teil der Messwerte eines Tages, 0 wenn keiner */
static uint8_t letzter_teil(const geraet_t *g, uint32_t zeit)
{
    uint8_t letzter = 0;
    char name[48];
    for (uint8_t t = 1; t < 50; t++) {
        pk_dateiname(g->id, t, false, name, sizeof(name));
        if (!tagespfad(zeit, name, s_pfad, sizeof(s_pfad)) || !vorhanden(s_pfad)) {
            break;
        }
        letzter = t;
    }
    return letzter;
}

/* Pruefwert der Kopfzeile einer vorhandenen Datei */
static bool kopf_lesen(const char *pfad, pk_kopf_t *k)
{
    FILE *f = fopen(pfad, "r");
    if (f == NULL) {
        return false;
    }
    bool ok = fgets(s_zeile, sizeof(s_zeile), f) != NULL && strchr(s_zeile, '\n') != NULL &&
              pk_kopf_aus_zeile(s_zeile, k);
    fclose(f);
    return ok;
}

typedef struct {
    char *buf;
    size_t len, pos;
    bool voll;
} kopfzeile_t;

static void kopfzeile_aus(void *ctx, const char *k, float v, pk_art_t a)
{
    (void)v;
    (void)a;
    kopfzeile_t *z = ctx;
    int n = snprintf(z->buf + z->pos, z->len - z->pos, ",%s", k);
    if (n < 0 || (size_t)n >= z->len - z->pos) {
        z->voll = true;
        return;
    }
    z->pos += (size_t)n;
}

/* Eine Quelle von Messwerten: der Katalog fuer abgefragte Geraete oder die
 * eigene Zeile des Leitstands. */
typedef void (*quelle_t)(void *daten, pk_ausgabe_t aus, void *ctx);

/* Beginnt einen Teil: Kopf in Messwerte und Mittel */
static bool teil_beginnen(geraet_t *g, uint32_t zeit, uint8_t teil, quelle_t q, void *daten)
{
    kopfzeile_t z = {s_zeile, sizeof(s_zeile), 4, false};
    memcpy(s_zeile, "zeit", 5);
    q(daten, kopfzeile_aus, &z);
    if (z.voll || z.pos + 2 > z.len) {
        fehler("Kopf zu lang");
        return false;
    }
    s_zeile[z.pos++] = '\n';
    s_zeile[z.pos] = '\0';
    char name[48];
    for (int m = 0; m < 2; m++) {
        pk_dateiname(g->id, teil, m == 1, name, sizeof(name));
        if (!tagespfad(zeit, name, s_pfad, sizeof(s_pfad)) || !anhaengen(s_pfad, s_zeile, z.pos)) {
            return false;
        }
    }
    g->teil = teil;
    return true;
}

static void mittel_schreiben(geraet_t *g)
{
    if (g->reihe.platz == 0 || g->teil == 0) {
        return;
    }
    uint32_t zeit = g->reihe.platz * PK_PLATZ_S;
    char name[48];
    pk_dateiname(g->id, g->teil, true, name, sizeof(name));
    if (pk_mittel_zeile(&g->reihe, s_zeile, sizeof(s_zeile)) && tagespfad(zeit, name, s_pfad, sizeof(s_pfad))) {
        anhaengen(s_pfad, s_zeile, strlen(s_zeile));
    }
}

static void kopf_aus(void *ctx, const char *k, float v, pk_art_t a)
{
    (void)v;
    (void)a;
    pk_kopf_schluessel(ctx, k);
}

/* Stellt sicher, dass Teil und Felder zum Kopf dieser Abfrage passen */
static bool spalten_pruefen(geraet_t *g, uint32_t zeit, quelle_t q, void *daten)
{
    pk_kopf_t k;
    pk_kopf_beginnen(&k);
    q(daten, kopf_aus, &k);
    if (k.spalten == 0) {
        return false;
    }
    uint32_t tag = pk_tag(zeit);
    bool gleich = g->teil != 0 && g->tag == tag && g->reihe.kopf.hash == k.hash &&
                  g->reihe.kopf.spalten == k.spalten;
    if (gleich) {
        return true;
    }
    /* Laufende Mittel gehoeren noch zum alten Kopf und womoeglich zum Vortag */
    mittel_schreiben(g);

    uint8_t teil;
    if (g->teil != 0 && g->tag == tag) {
        teil = g->teil + 1; /* neue Spalten im Laufe des Tages */
    } else {
        /* Neuer Tag oder Neustart: an einen passenden Teil anschliessen */
        teil = letzter_teil(g, zeit);
        char name[48];
        pk_kopf_t alt;
        pk_dateiname(g->id, teil, false, name, sizeof(name));
        bool passt = teil != 0 && tagespfad(zeit, name, s_pfad, sizeof(s_pfad)) && kopf_lesen(s_pfad, &alt) &&
                     alt.hash == k.hash && alt.spalten == k.spalten;
        if (!passt) {
            teil++;
        } else {
            g->teil = teil;
            teil = 0; /* kein neuer Kopf noetig */
        }
    }
    if (teil != 0 && !teil_beginnen(g, zeit, teil, q, daten)) {
        return false;
    }
    if (!pk_reihe_spalten(&g->reihe, &k)) {
        fehler("kein Speicher fuer die Mittel");
        g->teil = 0;
        return false;
    }
    g->tag = tag;
    return true;
}

/* ------------------------------------------------------------------ */
/* Zeile                                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    pk_zeile_t z;
    geraet_t *g;
    uint32_t zeit;
} zeile_ctx_t;

static void wechsel_melden(zeile_ctx_t *c, const char *k, float alt, float neu)
{
    char felder[96];
    int raum = 0;
    switch (pk_wechsel(k, alt, neu)) {
    case PK_W_SCHALTER: {
        uint32_t dauer = pk_schalter_dauer(&c->g->reihe, c->z.i, c->zeit);
        bool brenner = strcmp(k, "brenner") == 0;
        const char *pumpe = strcmp(k, "kkp") == 0 ? "kkp" : k + 6;
        int n = brenner ? snprintf(felder, sizeof(felder), "\"ein\":%s", neu > 0.5f ? "true" : "false")
                        : snprintf(felder, sizeof(felder), "\"pumpe\":\"%s\",\"ein\":%s", pumpe,
                                   neu > 0.5f ? "true" : "false");
        if (dauer > 0 && n > 0 && (size_t)n < sizeof(felder)) {
            snprintf(felder + n, sizeof(felder) - n, ",\"dauer_vorher_s\":%lu", (unsigned long)dauer);
        }
        ereignis(c->zeit, c->g->id, brenner ? "brenner" : "pumpe", felder);
        break;
    }
    case PK_W_SOLLWERT: {
        char a[16], n[16];
        sscanf(k, "raum.%d", &raum);
        pk_zahl(alt, a, sizeof(a));
        pk_zahl(neu, n, sizeof(n));
        snprintf(felder, sizeof(felder), "\"raum\":%d,\"alt\":%s,\"neu\":%s", raum, a, n);
        ereignis(c->zeit, c->g->id, "sollwert", felder);
        break;
    }
    case PK_W_BETRIEBSART:
        sscanf(k, "raum.%d", &raum);
        snprintf(felder, sizeof(felder), "\"raum\":%d,\"alt\":\"%s\",\"neu\":\"%s\"", raum,
                 alt > 0.5f ? "heiz" : "aus", neu > 0.5f ? "heiz" : "aus");
        ereignis(c->zeit, c->g->id, "betriebsart", felder);
        break;
    default:
        break;
    }
}

static void zeile_aus(void *ctx, const char *k, float v, pk_art_t a)
{
    zeile_ctx_t *c = ctx;
    pk_reihe_t *r = &c->g->reihe;
    if (c->z.i < r->kopf.spalten) {
        wechsel_melden(c, k, r->letzt[c->z.i], v);
    }
    pk_zeile_wert(&c->z, v, a);
}

/* Schreibt eine Abtastung: Kopf pruefen, Mittel abschliessen, Zeile */
static void abtastung(geraet_t *g, uint32_t zeit, quelle_t q, void *daten)
{
    if (!spalten_pruefen(g, zeit, q, daten)) {
        return;
    }
    if (pk_mittel_faellig(&g->reihe, zeit)) {
        mittel_schreiben(g);
    }
    /* Die Ereignisse benutzen s_ereignis und s_pfad, die Zeile s_zeile. */
    zeile_ctx_t c = {.g = g, .zeit = zeit};
    pk_zeile_beginnen(&c.z, &g->reihe, zeit, s_zeile, sizeof(s_zeile));
    q(daten, zeile_aus, &c);
    if (!pk_zeile_ende(&c.z)) {
        fehler("Zeile passt nicht zum Kopf");
        g->teil = 0; /* beim naechsten Mal neu anschliessen */
        return;
    }
    char name[48];
    pk_dateiname(g->id, g->teil, false, name, sizeof(name));
    if (tagespfad(zeit, name, s_pfad, sizeof(s_pfad))) {
        anhaengen(s_pfad, s_zeile, c.z.pos);
    }
}

/* ------------------------------------------------------------------ */
/* Abgefragte Geraete                                                  */
/* ------------------------------------------------------------------ */

typedef struct {
    bool heiz;
    const cJSON *zustand, *bedarf;
} abfrage_t;

static void katalog(void *daten, pk_ausgabe_t aus, void *ctx)
{
    const abfrage_t *a = daten;
    if (a->heiz) {
        pk_heizgeraet(a->zustand, aus, ctx);
    } else {
        pk_verteiler(a->zustand, a->bedarf, aus, ctx);
    }
}

static const char *feld_text(const cJSON *o, const char *k)
{
    const cJSON *j = o ? cJSON_GetObjectItemCaseSensitive(o, k) : NULL;
    return cJSON_IsString(j) && j->valuestring ? j->valuestring : "";
}

/* Befunde: neue und erledigte Kennungen */
static void befunde_vergleichen(geraet_t *g, uint32_t zeit, const char *neu)
{
    char felder[96];
    for (int runde = 0; runde < 2; runde++) {
        const char *liste = runde == 0 ? neu : g->befunde;
        const char *gegen = runde == 0 ? g->befunde : neu;
        const char *p = liste;
        while (*p) {
            const char *e = strchr(p, ',');
            size_t n = e ? (size_t)(e - p) : strlen(p);
            char code[32];
            snprintf(code, sizeof(code), "%.*s", (int)n, p);
            /* als ganzes Wort in der anderen Liste? */
            bool da = false;
            for (const char *q = gegen; *q;) {
                const char *f = strchr(q, ',');
                size_t m = f ? (size_t)(f - q) : strlen(q);
                da = da || (m == n && strncmp(q, p, n) == 0);
                q += m + (f ? 1 : 0);
            }
            if (!da) {
                snprintf(felder, sizeof(felder), "\"code\":\"%s\",\"stand\":\"%s\"", code,
                         runde == 0 ? "neu" : "erledigt");
                ereignis(zeit, g->id, "befund", felder);
            }
            p += n + (e ? 1 : 0);
        }
    }
    snprintf(g->befunde, sizeof(g->befunde), "%s", neu);
}

static void zustand_schreiben(geraet_t *g, uint32_t zeit, const char *text, int len)
{
    if (!tagespfad(zeit, "zustaende.jsonl", s_pfad, sizeof(s_pfad))) {
        return;
    }
    FILE *f = fopen(s_pfad, "a");
    if (f == NULL) {
        fehler("zustaende.jsonl nicht zu oeffnen");
        return;
    }
    char iso[24];
    pk_iso(zeit, iso, sizeof(iso));
    int n = fprintf(f, "{\"epoch\":%lu,\"zeit\":\"%s\",\"%s\":", (unsigned long)zeit, iso, g->id);
    size_t w = fwrite(text, 1, (size_t)len, f);
    fputs("}\n", f);
    fclose(f);
    zaehlen((size_t)(n > 0 ? n : 0) + w + 2);
    g->zustand_zeit = zeit;
}

void st_log_zustand(const char *kennung, const char *ort, bool heizgeraet, const cJSON *zustand,
                    const cJSON *bedarf, const char *text, int text_len)
{
    uint32_t zeit = jetzt();
    if (s_mtx == NULL) {
        return;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (!s_bereit || zeit == 0) {
        if (zeit == 0) {
            s_status.verworfen++;
        }
        xSemaphoreGive(s_mtx);
        return;
    }
    geraet_t *g = geraet(kennung);
    if (g == NULL) {
        xSemaphoreGive(s_mtx);
        return;
    }
    g->heiz = heizgeraet;
    if (ort && ort[0] && strcmp(ort, g->ort) != 0) {
        snprintf(g->ort, sizeof(g->ort), "%s", ort);
        s_geraete_neu = true;
    }

    /* Neustart, Version, Befunde */
    const cJSON *lz = cJSON_GetObjectItemCaseSensitive(zustand, "uptime_s");
    uint32_t laufzeit = cJSON_IsNumber(lz) ? (uint32_t)lz->valuedouble : 0;
    char felder[128];
    if (g->laufzeit_bekannt && laufzeit < g->laufzeit) {
        const char *grund = feld_text(zustand, "reset_reason");
        snprintf(felder, sizeof(felder), "\"laufzeit_vorher_s\":%lu%s%s%s", (unsigned long)g->laufzeit,
                 grund[0] ? ",\"grund\":\"" : "", grund, grund[0] ? "\"" : "");
        ereignis(zeit, g->id, "neustart", felder);
    }
    g->laufzeit = laufzeit;
    g->laufzeit_bekannt = true;

    const char *version = feld_text(zustand, "version");
    bool neue_version = version[0] && strcmp(version, g->version) != 0;
    if (neue_version) {
        if (g->version[0]) {
            snprintf(felder, sizeof(felder), "\"alt\":\"%s\",\"neu\":\"%s\"", g->version, version);
            ereignis(zeit, g->id, "version", felder);
        }
        snprintf(g->version, sizeof(g->version), "%s", version);
        s_geraete_neu = true;
    }
    if (heizgeraet) {
        char befunde[64];
        pk_befunde(zustand, befunde, sizeof(befunde));
        if (g->befunde_bekannt) {
            befunde_vergleichen(g, zeit, befunde);
        } else {
            snprintf(g->befunde, sizeof(g->befunde), "%s", befunde);
            g->befunde_bekannt = true;
        }
    }

    abfrage_t a = {heizgeraet, zustand, bedarf};
    abtastung(g, zeit, katalog, &a);

    if (text != NULL && text_len > 0 && (neue_version || zeit - g->zustand_zeit >= ZUSTAND_S)) {
        zustand_schreiben(g, zeit, text, text_len);
    }
    if (s_geraete_neu || s_geraete_tag != pk_tag(zeit)) {
        geraete_schreiben(zeit);
    }
    xSemaphoreGive(s_mtx);
}

void st_log_unveraendert(const char *kennung)
{
    uint32_t zeit = jetzt();
    if (s_mtx == NULL || zeit == 0) {
        return;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    geraet_t *g = s_bereit ? geraet(kennung) : NULL;
    if (g != NULL && g->teil != 0 && g->tag == pk_tag(zeit)) {
        if (pk_mittel_faellig(&g->reihe, zeit)) {
            mittel_schreiben(g);
        }
        char name[48];
        pk_dateiname(g->id, g->teil, false, name, sizeof(name));
        if (pk_zeile_wiederholen(&g->reihe, zeit, s_zeile, sizeof(s_zeile)) &&
            tagespfad(zeit, name, s_pfad, sizeof(s_pfad))) {
            anhaengen(s_pfad, s_zeile, strlen(s_zeile));
        }
    }
    xSemaphoreGive(s_mtx);
}

void st_log_erreichbar(const char *kennung, bool erreichbar, uint32_t dauer_s)
{
    uint32_t zeit = jetzt();
    if (s_mtx == NULL || zeit == 0) {
        return;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (s_bereit) {
        char felder[48] = "";
        if (erreichbar && dauer_s > 0) {
            snprintf(felder, sizeof(felder), "\"dauer_s\":%lu", (unsigned long)dauer_s);
        }
        ereignis(zeit, kennung, erreichbar ? "erreichbar" : "nicht_erreichbar", felder);
    }
    xSemaphoreGive(s_mtx);
}

/* ------------------------------------------------------------------ */
/* Protokolle der Heizungsgeraete                                      */
/* ------------------------------------------------------------------ */

bool st_log_datei(uint32_t zeit, const char *name, char *out, size_t len)
{
    char tag[24];
    pk_tagespfad(zeit, tag, sizeof(tag));
    int n = snprintf(out, len, "%s/%s/%s", ST_LOG_WURZEL, tag, name);
    return n > 0 && (size_t)n < len;
}

bool st_log_protokolle_faellig(const char *kennung)
{
    uint32_t zeit = jetzt();
    if (s_mtx == NULL || zeit == 0) {
        return false;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    geraet_t *g = s_bereit ? geraet(kennung) : NULL;
    bool faellig = g != NULL && g->protokoll_tag != pk_tag(zeit);
    xSemaphoreGive(s_mtx);
    return faellig;
}

void st_log_protokolle_erledigt(const char *kennung)
{
    uint32_t zeit = jetzt();
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    geraet_t *g = s_bereit ? geraet(kennung) : NULL;
    if (g != NULL) {
        g->protokoll_tag = pk_tag(zeit);
    }
    xSemaphoreGive(s_mtx);
}

/* Erst in eine Zwischendatei, dann umbenannt: Bricht die Uebertragung ab,
 * bleibt die Fassung vom Vortag bzw. vom letzten Versuch stehen. */
bool st_log_protokoll(const char *kennung, const char *art, st_log_lesen_t lesen, void *ctx)
{
    uint32_t zeit = jetzt();
    if (s_mtx == NULL || zeit == 0) {
        return false;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    bool ok = s_bereit;
    char ziel[128], zwischen[128];
    if (ok) {
        tagespfad(zeit, "", s_pfad, sizeof(s_pfad)); /* legt den Tag an */
        snprintf(ziel, sizeof(ziel), "%.64sprotokolle", s_pfad);
        verzeichnis(ziel);
        snprintf(ziel, sizeof(ziel), "%.64sprotokolle/%.23s.%.12s.csv", s_pfad, kennung, art);
        snprintf(zwischen, sizeof(zwischen), "%.64sprotokolle/%.23s.%.12s.tmp", s_pfad, kennung, art);
    }
    FILE *f = ok ? fopen(zwischen, "w") : NULL;
    ok = f != NULL;
    size_t gesamt = 0;
    while (ok) {
        int n = 0;
        if (!lesen(ctx, s_zeile, sizeof(s_zeile), &n)) {
            ok = false;
            break;
        }
        if (n == 0) {
            break;
        }
        ok = fwrite(s_zeile, 1, (size_t)n, f) == (size_t)n;
        gesamt += (size_t)n;
    }
    if (f != NULL) {
        ok = fclose(f) == 0 && ok;
    }
    if (ok && gesamt > 0) {
        unlink(ziel);
        ok = rename(zwischen, ziel) == 0;
        zaehlen(gesamt);
    } else {
        unlink(zwischen);
        ok = false;
    }
    if (!ok) {
        fehler("Protokoll nicht uebernommen");
    }
    xSemaphoreGive(s_mtx);
    return ok;
}

/* ------------------------------------------------------------------ */
/* Zeile des Leitstands                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    atc_device_t funk[FUNK_MAX];
    size_t n;
    netmgr_status_t netz;
} eigen_t;


static void eigen(void *daten, pk_ausgabe_t aus, void *ctx)
{
    const eigen_t *e = daten;
    char k[PK_SCHLUESSEL_MAX];
    for (size_t i = 0; i < e->n; i++) {
        const atc_device_t *d = &e->funk[i];
        char mac[18];
        snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", d->mac[0], d->mac[1], d->mac[2], d->mac[3],
                 d->mac[4], d->mac[5]);
        snprintf(k, sizeof(k), "funk.%s.temp", mac);
        aus(ctx, k, d->has_temp ? d->temp_c : NAN, PK_MITTEL);
        snprintf(k, sizeof(k), "funk.%s.feuchte", mac);
        aus(ctx, k, d->has_humidity ? d->humidity : NAN, PK_MITTEL);
        snprintf(k, sizeof(k), "funk.%s.batterie", mac);
        aus(ctx, k, d->battery ? (float)d->battery : NAN, PK_LETZT);
        snprintf(k, sizeof(k), "funk.%s.rssi", mac);
        aus(ctx, k, (float)d->rssi, PK_MITTEL);
    }
    aus(ctx, "geraet.heap", (float)esp_get_free_heap_size(), PK_MITTEL);
    aus(ctx, "geraet.rssi", e->netz.sta_connected ? (float)e->netz.rssi : NAN, PK_MITTEL);
    aus(ctx, "geraet.laufzeit", (float)(esp_timer_get_time() / 1000000), PK_LETZT);
}

/*
 * Funkthermometer: verloren, wenn eine Viertelstunde nichts kam -- dieselbe
 * Grenze wie fuer den Aussenwert --, wieder da beim naechsten Paket; dazu ein
 * falscher Schluessel. Gemerkt wird je Adresse nur, was zuletzt galt.
 */
#define FUNK_STUMM_MS (ST_OUTDOOR_STALE_S * 1000u)

typedef struct {
    uint8_t mac[6];
    bool da;
    bool falsch;
} funk_stand_t;

static funk_stand_t s_funk[FUNK_MAX];
static uint8_t s_funk_n;

static void funk_pruefen(const eigen_t *e, uint32_t zeit, const char *id)
{
    uint32_t t = (uint32_t)(esp_timer_get_time() / 1000);
    for (size_t i = 0; i < e->n; i++) {
        const atc_device_t *d = &e->funk[i];
        funk_stand_t *f = NULL;
        for (uint8_t k = 0; k < s_funk_n; k++) {
            if (memcmp(s_funk[k].mac, d->mac, 6) == 0) {
                f = &s_funk[k];
            }
        }
        bool da = t - d->last_seen_ms < FUNK_STUMM_MS;
        bool falsch = d->encrypted && d->key == ATC_KEY_WRONG;
        if (f == NULL) {
            if (s_funk_n >= FUNK_MAX) {
                continue;
            }
            /* Erstmals gesehen: nur merken, ein Ereignis gibt es erst beim
             * naechsten Wechsel. */
            f = &s_funk[s_funk_n++];
            memcpy(f->mac, d->mac, 6);
            f->da = da;
            f->falsch = falsch;
            continue;
        }
        char felder[96];
        char mac[18];
        snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", d->mac[0], d->mac[1], d->mac[2], d->mac[3],
                 d->mac[4], d->mac[5]);
        if (da != f->da) {
            snprintf(felder, sizeof(felder), "\"adresse\":\"%s\",\"name\":\"%.23s\",\"stand\":\"%s\"", mac,
                     d->name, da ? "wieder" : "verloren");
            ereignis(zeit, id, "funk", felder);
            f->da = da;
        }
        if (falsch && !f->falsch) {
            snprintf(felder, sizeof(felder), "\"adresse\":\"%s\",\"name\":\"%.23s\",\"stand\":\"schluessel_falsch\"",
                     mac, d->name);
            ereignis(zeit, id, "funk", felder);
        }
        f->falsch = falsch;
    }
}

static int mac_vergleich(const void *a, const void *b)
{
    return memcmp(((const atc_device_t *)a)->mac, ((const atc_device_t *)b)->mac, 6);
}

static void eigene_zeile(uint32_t zeit)
{
    /* Nur fuer die Dauer der Zeile: Dauerhaft belegt fehlte das Kilobyte dem
     * Abfrageauftrag. */
    eigen_t *e = malloc(sizeof(eigen_t));
    if (e == NULL) {
        return;
    }
    e->n = atc_ble_devices(e->funk, FUNK_MAX);
    /* Nach Adresse geordnet: Die Spalten haengen dann nicht davon ab, in
     * welcher Reihenfolge die Thermometer zuerst gehoert wurden. */
    qsort(e->funk, e->n, sizeof(atc_device_t), mac_vergleich);
    netmgr_status(&e->netz);

    char id[24];
    st_device_id(id, sizeof(id));
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    geraet_t *g = s_bereit ? geraet(id) : NULL;
    if (g != NULL) {
        if (!g->ort[0]) {
            st_config_t cfg;
            st_cfg_copy(&cfg);
            snprintf(g->ort, sizeof(g->ort), "%s", cfg.site);
            memset(&cfg, 0, sizeof(cfg));
        }
        if (!g->version[0]) {
            snprintf(g->version, sizeof(g->version), "%.23s", esp_app_get_description()->version);
        }
        abtastung(g, zeit, eigen, e);
        funk_pruefen(e, zeit, g->id);
        if (s_geraete_neu || s_geraete_tag != pk_tag(zeit)) {
            geraete_schreiben(zeit);
        }
    }
    xSemaphoreGive(s_mtx);
    free(e);
}

/* ------------------------------------------------------------------ */
/* Karte                                                               */
/* ------------------------------------------------------------------ */

static void platz_lesen(void)
{
    uint64_t gesamt = 0, frei = 0;
    if (esp_vfs_fat_info(EINHAENGEN, &gesamt, &frei) == ESP_OK) {
        s_status.groesse_mb = (uint32_t)(gesamt >> 20);
        s_status.frei_mb = (uint32_t)(frei >> 20);
    }
}

static bool einhaengen(void)
{
    int cs = st_ui_sd_cs();
    if (cs < 0) {
        snprintf(s_status.meldung, sizeof(s_status.meldung), "kein Kartensteckplatz bekannt");
        return false;
    }
    /* Der Bus gehoert der Anzeige und ist von ihr schon eingerichtet; die
     * Karte kommt als zweites Geraet dazu. */
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI3_HOST;
    host.max_freq_khz = 20000;
    sdspi_device_config_t dev = SDSPI_DEVICE_CONFIG_DEFAULT();
    dev.gpio_cs = (gpio_num_t)cs;
    dev.host_id = SPI3_HOST;
    esp_vfs_fat_sdmmc_mount_config_t mc = {
        .format_if_mount_failed = false,
        .max_files = 2,
        .allocation_unit_size = 16 * 1024,
    };
    st_ui_bus_sperren();
    esp_err_t e = esp_vfs_fat_sdspi_mount(EINHAENGEN, &host, &dev, &mc, &s_karte);
    st_ui_bus_freigeben();
    if (e != ESP_OK) {
        snprintf(s_status.meldung, sizeof(s_status.meldung), "%s",
                 e == ESP_FAIL ? "Karte nicht lesbar (kein FAT32?)" : "keine Karte");
        return false;
    }
    /* Schreibprobe */
    verzeichnis(ST_LOG_WURZEL);
    FILE *f = fopen(ST_LOG_WURZEL "/probe.txt", "w");
    bool ok = f != NULL && fputs("probe\n", f) >= 0;
    if (f != NULL) {
        ok = fclose(f) == 0 && ok;
    }
    unlink(ST_LOG_WURZEL "/probe.txt");
    if (!ok) {
        snprintf(s_status.meldung, sizeof(s_status.meldung), "Karte nicht beschreibbar");
        esp_vfs_fat_sdcard_unmount(EINHAENGEN, s_karte);
        s_karte = NULL;
        return false;
    }
    /* Eine abgebrochene Dauerlastpruefung laesst ihre Datei zurueck. */
    unlink(EINHAENGEN "/lasttest.bin");
    platz_lesen();
    s_status.meldung[0] = '\0';
    ESP_LOGI(TAG, "Karte eingehaengt: %lu MB, frei %lu MB", (unsigned long)s_status.groesse_mb,
             (unsigned long)s_status.frei_mb);
    return true;
}

/*
 * Der Steckplatz hat keinen Kartenschalter. Ob die eingehaengte Karte noch da
 * ist, sagt ihr Status (CMD13): Eine gezogene Karte antwortet nicht, eine neu
 * gesteckte ist nicht eingerichtet und antwortet ebenso wenig. Dann wird die
 * Einbindung aufgegeben -- sonst schriebe das Dateisystem mit der Belegung
 * der alten Karte auf die neue --, und die Aufgabe bindet neu ein. Nur unter
 * s_mtx aufzurufen.
 */
static bool s_verloren_melden;

static void karte_pruefen(void)
{
    if (!s_bereit || s_karte == NULL) {
        return;
    }
    /* Ohne Bus-Sperre der Anzeige: Diese Funktion laeuft unter s_mtx, und die
     * Anzeige haelt ihre Sperre, waehrend sie st_log_status() fragt. Beides
     * zusammen verklemmte am 23. September Anzeige, Protokoll und Abfrage.
     * Die Zugriffe auf den Bus ordnet der SPI-Treiber selbst. */
    esp_err_t e = sdmmc_get_status(s_karte);
    if (e == ESP_OK) {
        return;
    }
    ESP_LOGW(TAG, "Karte antwortet nicht mehr (%s), Einbindung aufgegeben", esp_err_to_name(e));
    esp_vfs_fat_sdcard_unmount(EINHAENGEN, s_karte);
    s_karte = NULL;
    s_bereit = false;
    s_status.karte = false;
    s_tag_angelegt[0] = '\0';
    for (int i = 0; i < GERAETE_MAX; i++) {
        s_g[i].teil = 0; /* nach dem Einhaengen an die Dateien neu anschliessen */
    }
    snprintf(s_status.meldung, sizeof(s_status.meldung), "Karte entfernt oder gestoert");
    s_verloren_melden = true;
}

/* Kleinster Eintrag eines Verzeichnisses, fuer Jahre und Tage */
static bool kleinster(const char *dir, char *out, size_t len)
{
    DIR *d = opendir(dir);
    if (d == NULL) {
        return false;
    }
    bool gefunden = false;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') {
            continue;
        }
        if (!gefunden || strcmp(e->d_name, out) < 0) {
            size_t n = strnlen(e->d_name, len - 1);
            memcpy(out, e->d_name, n);
            out[n] = '\0';
            gefunden = true;
        }
    }
    closedir(d);
    return gefunden;
}

/*
 * Wird der Platz knapp, gehen zuerst die aeltesten Zustaende, danach die
 * aeltesten Messwerte im Takt der Abfrage. Fuenfminutenmittel und Ereignisse
 * bleiben; sie belegen zusammen weniger als 150 MB im Jahr.
 */
static void platz_schaffen(void)
{
    platz_lesen();
    for (int runde = 0; runde < 40; runde++) {
        if (s_status.groesse_mb == 0 || s_status.frei_mb * 100 >= s_status.groesse_mb * FREI_PROZENT) {
            return;
        }
        char jahr[8], tag[16], dir[64];
        if (!kleinster(ST_LOG_WURZEL, jahr, sizeof(jahr))) {
            return;
        }
        snprintf(dir, sizeof(dir), "%s/%s", ST_LOG_WURZEL, jahr);
        if (!kleinster(dir, tag, sizeof(tag))) {
            rmdir(dir);
            continue;
        }
        snprintf(dir, sizeof(dir), "%s/%s/%s", ST_LOG_WURZEL, jahr, tag);
        DIR *d = opendir(dir);
        if (d == NULL) {
            return;
        }
        int geloescht = 0;
        struct dirent *e;
        char pfad[128];
        while ((e = readdir(d)) != NULL) {
            bool zustaende = strcmp(e->d_name, "zustaende.jsonl") == 0;
            bool roh = strstr(e->d_name, ".csv") != NULL && strstr(e->d_name, ".5min.") == NULL &&
                       strstr(e->d_name, ".5min") == NULL;
            if (zustaende || roh) {
                snprintf(pfad, sizeof(pfad), "%s/%.60s", dir, e->d_name);
                if (unlink(pfad) == 0) {
                    geloescht++;
                }
            }
        }
        closedir(d);
        ESP_LOGW(TAG, "Platz knapp: %d Dateien vom %s geloescht", geloescht, tag);
        if (geloescht == 0) {
            return; /* nur noch Mittel und Ereignisse -- die bleiben */
        }
        platz_lesen();
    }
}

static volatile bool s_formatieren;

void st_log_formatieren(void)
{
    s_formatieren = true;
}

/*
 * Formatieren: FatFs legt eine neue Partitionstabelle mit einer einzigen
 * Partition ueber die ganze Karte an und schreibt die FAT-Tabellen, bei
 * 64 GB rund 32 MB. Solange ruhen Abfrage und Waechter der Leerlaufaufgaben:
 * Der Kartentreiber wartet aktiv auf den Bus, die Leerlaufaufgabe kaeme ueber
 * die Dauer nicht zum Zug.
 */
static void formatieren(void)
{
    st_poll_pausieren(true);
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    esp_err_t e = ESP_ERR_INVALID_STATE;
    if (s_karte != NULL) {
        snprintf(s_status.meldung, sizeof(s_status.meldung), "Karte wird formatiert");
        const esp_task_wdt_config_t ohne = {.timeout_ms = CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000,
                                            .idle_core_mask = 0, .trigger_panic = false};
        esp_task_wdt_reconfigure(&ohne);
        esp_vfs_fat_mount_config_t mc = {.format_if_mount_failed = false, .max_files = 2,
                                         .allocation_unit_size = 32 * 1024};
        ESP_LOGW(TAG, "Formatiere die Karte");
        /* Ohne Bus-Sperre, siehe karte_pruefen */
        e = esp_vfs_fat_sdcard_format_cfg(EINHAENGEN, s_karte, &mc);
        const esp_task_wdt_config_t mit = {.timeout_ms = CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000,
                                           .idle_core_mask = (1 << portNUM_PROCESSORS) - 1, .trigger_panic = false};
        esp_task_wdt_reconfigure(&mit);
    }
    s_tag_angelegt[0] = '\0';
    for (int i = 0; i < GERAETE_MAX; i++) {
        s_g[i].teil = 0;
        s_g[i].protokoll_tag = 0;
    }
    if (e == ESP_OK) {
        platz_lesen();
        verzeichnis(ST_LOG_WURZEL);
        s_status.meldung[0] = '\0';
        s_status.fehler = 0;
        ESP_LOGW(TAG, "Karte formatiert: %lu MB", (unsigned long)s_status.groesse_mb);
    } else {
        snprintf(s_status.meldung, sizeof(s_status.meldung), "Formatieren fehlgeschlagen");
        ESP_LOGE(TAG, "Formatieren fehlgeschlagen: %s", esp_err_to_name(e));
        /* Die Einbindung ist womoeglich weg; die Aufgabe bindet neu ein. */
        s_bereit = false;
        s_status.karte = false;
        s_karte = NULL;
    }
    xSemaphoreGive(s_mtx);
    st_poll_pausieren(false);
    uint32_t zeit = jetzt();
    if (e == ESP_OK && zeit != 0) {
        char id[24], felder[96];
        st_device_id(id, sizeof(id));
        snprintf(felder, sizeof(felder), "\"was\":\"karte_formatiert\",\"groesse_mb\":%lu",
                 (unsigned long)s_status.groesse_mb);
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        ereignis(zeit, id, "leitstand", felder);
        xSemaphoreGive(s_mtx);
    }
}

static void log_task(void *arg)
{
    (void)arg;
    uint32_t start_ms = (uint32_t)(esp_timer_get_time() / 1000);
    uint32_t versuch_ms = 0, eigen_ms = 0, platz_ms = 0, status_ms = 0;
    bool zeit_gemeldet = false;
    bool einhaengen_melden = false;
    for (;;) {
        uint32_t t = (uint32_t)(esp_timer_get_time() / 1000);
        if (s_formatieren) {
            s_formatieren = false;
            if (s_bereit) {
                formatieren();
            }
        }
        if (!s_bereit && (versuch_ms == 0 || t - versuch_ms >= 60000)) {
            versuch_ms = t;
            bool ok = einhaengen();
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            s_bereit = ok;
            s_status.karte = ok;
            xSemaphoreGive(s_mtx);
            /* Beim Start meldet das Startereignis die Karte mit; ein spaeteres
             * Einhaengen bekommt ein eigenes. */
            einhaengen_melden = ok && zeit_gemeldet;
        }
        if (s_bereit && (t - status_ms >= 10000)) {
            status_ms = t;
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            karte_pruefen();
            xSemaphoreGive(s_mtx);
        }
        uint32_t zeit = jetzt();
        if (s_bereit && zeit != 0 && !zeit_gemeldet) {
            zeit_gemeldet = true;
            char id[24], felder[96];
            st_device_id(id, sizeof(id));
            snprintf(felder, sizeof(felder), "\"was\":\"start\",\"grund\":\"%s\",\"verworfen\":%lu,\"karte_mb\":%lu",
                     netmgr_reset_reason(), (unsigned long)s_status.verworfen, (unsigned long)s_status.groesse_mb);
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            ereignis(zeit, id, "leitstand", felder);
            xSemaphoreGive(s_mtx);
        }
        if (zeit != 0 && (einhaengen_melden || s_verloren_melden) && s_bereit) {
            char id[24], felder[96];
            st_device_id(id, sizeof(id));
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            if (s_verloren_melden) {
                ereignis(zeit, id, "leitstand", "\"was\":\"karte_verloren\"");
                s_verloren_melden = false;
            }
            snprintf(felder, sizeof(felder), "\"was\":\"karte\",\"groesse_mb\":%lu,\"frei_mb\":%lu",
                     (unsigned long)s_status.groesse_mb, (unsigned long)s_status.frei_mb);
            ereignis(zeit, id, "leitstand", felder);
            xSemaphoreGive(s_mtx);
            einhaengen_melden = false;
        }
        if (s_bereit && zeit != 0 && t - start_ms >= EIGEN_ANLAUF_S * 1000u &&
            (eigen_ms == 0 || t - eigen_ms >= EIGEN_S * 1000u)) {
            eigen_ms = t;
            eigene_zeile(zeit);
        }
        if (s_bereit && (platz_ms == 0 || t - platz_ms >= 3600000u)) {
            platz_ms = t;
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            platz_schaffen();
            xSemaphoreGive(s_mtx);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t st_log_start(void)
{
    s_mtx = xSemaphoreCreateMutex();
    if (s_mtx == NULL) {
        return ESP_ERR_NO_MEM;
    }
    return xTaskCreate(log_task, "log", 4096, NULL, 3, NULL) == pdPASS ? ESP_OK : ESP_FAIL;
}

void st_log_status(st_log_status_t *out)
{
    memset(out, 0, sizeof(*out));
    if (s_mtx == NULL) {
        return;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    /* Die Anzeige fragt jede Sekunde; den freien Platz zu zaehlen kostet einen
     * Blick in die FAT und genuegt einmal in der Minute. */
    static uint32_t gelesen_ms;
    uint32_t t = (uint32_t)(esp_timer_get_time() / 1000);
    if (s_bereit && (gelesen_ms == 0 || t - gelesen_ms >= 60000)) {
        gelesen_ms = t;
        platz_lesen();
    }
    *out = s_status;
    if (pk_tag(jetzt()) != s_heute_tag) {
        out->heute_byte = 0;
    }
    xSemaphoreGive(s_mtx);
}

bool st_log_pfad(const char *s, char *out, size_t len)
{
    /* JJJJ-MM-TT/name, name aus Buchstaben, Ziffern, Punkt, Unterstrich */
    if (strlen(s) < 12 || s[10] != '/') {
        return false;
    }
    for (int i = 0; i < 10; i++) {
        bool strich = i == 4 || i == 7;
        if (strich ? s[i] != '-' : (s[i] < '0' || s[i] > '9')) {
            return false;
        }
    }
    const char *name = s + 11;
    /* Ein Unterverzeichnis gibt es: die uebernommenen Protokolle */
    const char *unter = "";
    if (strncmp(name, "protokolle/", 11) == 0) {
        unter = "protokolle/";
        name += 11;
    }
    if (!name[0] || name[0] == '.' || strlen(name) > 48) {
        return false;
    }
    for (const char *p = name; *p; p++) {
        bool ok = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '.' ||
                  *p == '_' || *p == '-';
        if (!ok) {
            return false;
        }
    }
    int n = snprintf(out, len, "%s/%.4s/%.10s/%s%s", ST_LOG_WURZEL, s, s, unter, name);
    return n > 0 && (size_t)n < len;
}

/* ------------------------------------------------------------------ */
/* Dauerlastpruefung                                                   */
/* ------------------------------------------------------------------ */

#define LAST_DATEI EINHAENGEN "/lasttest.bin"
#define LAST_BLOCK 2048
#define LAST_JE_OEFFNEN (1024 * 1024)  /* je Oeffnen der Datei ein Megabyte */

static st_log_lasttest_t s_last = {.phase = ""};

static void muster(uint32_t *w, uint32_t block)
{
    for (uint32_t i = 0; i < LAST_BLOCK / 4; i++) {
        w[i] = (block * (LAST_BLOCK / 4) + i) ^ 0xA5C3F00Fu;
    }
}

static void last_heap(void)
{
    uint32_t h = esp_get_free_heap_size();
    if (s_last.heap_min == 0 || h < s_last.heap_min) {
        s_last.heap_min = h;
    }
}

static void lasttest_task(void *arg)
{
    (void)arg;
    uint32_t *w = malloc(LAST_BLOCK);
    uint32_t *r = malloc(LAST_BLOCK);
    uint32_t bloecke = s_last.ziel_mb * (1024 * 1024 / LAST_BLOCK);
    uint32_t je = LAST_JE_OEFFNEN / LAST_BLOCK;
    bool ok = w != NULL && r != NULL;
    unlink(LAST_DATEI);

    /* Schreiben, je Megabyte einmal oeffnen und schliessen: So belegt die
     * Pruefung eine Datei nur kurz, und das Protokoll kommt dazwischen. */
    s_last.phase = "schreiben";
    int64_t t0 = esp_timer_get_time();
    for (uint32_t b = 0; ok && b < bloecke; b += je) {
        FILE *f = fopen(LAST_DATEI, "a");
        if (f == NULL) {
            s_last.schreibfehler++;
            ok = false;
            break;
        }
        for (uint32_t k = b; k < b + je && k < bloecke; k++) {
            muster(w, k);
            if (fwrite(w, 1, LAST_BLOCK, f) != LAST_BLOCK) {
                s_last.schreibfehler++;
                ok = false;
                break;
            }
        }
        if (fclose(f) != 0) {
            s_last.schreibfehler++;
            ok = false;
        }
        s_last.geschrieben_mb = (b + je) / je;
        s_last.schreiben_s = (uint32_t)((esp_timer_get_time() - t0) / 1000000);
        last_heap();
        vTaskDelay(1); /* Leerlauf und Waechter kommen dran */
    }

    /* Zuruecklesen und vergleichen */
    s_last.phase = "lesen";
    t0 = esp_timer_get_time();
    FILE *f = ok ? fopen(LAST_DATEI, "r") : NULL;
    for (uint32_t k = 0; f != NULL && k < bloecke; k++) {
        if (fread(r, 1, LAST_BLOCK, f) != LAST_BLOCK) {
            s_last.abweichungen += bloecke - k;
            break;
        }
        muster(w, k);
        if (memcmp(r, w, LAST_BLOCK) != 0) {
            s_last.abweichungen++;
        }
        if (k % je == je - 1) {
            s_last.gelesen_mb = (k + 1) / je;
            s_last.lesen_s = (uint32_t)((esp_timer_get_time() - t0) / 1000000);
            last_heap();
            vTaskDelay(1);
        }
    }
    if (f != NULL) {
        fclose(f);
    }
    unlink(LAST_DATEI);
    free(w);
    free(r);
    s_last.phase = ok ? "fertig" : "abgebrochen";
    s_last.laeuft = false;
    ESP_LOGW(TAG, "Dauerlast: %lu MB geschrieben in %lu s, %lu MB gelesen in %lu s, %lu Schreibfehler, "
                  "%lu abweichende Bloecke, Speicher mindestens %lu",
             (unsigned long)s_last.geschrieben_mb, (unsigned long)s_last.schreiben_s,
             (unsigned long)s_last.gelesen_mb, (unsigned long)s_last.lesen_s,
             (unsigned long)s_last.schreibfehler, (unsigned long)s_last.abweichungen,
             (unsigned long)s_last.heap_min);
    vTaskDelete(NULL);
}

bool st_log_lasttest_starten(uint32_t mb)
{
    if (!s_bereit || s_last.laeuft || mb == 0 || mb > 4096) {
        return false;
    }
    memset(&s_last, 0, sizeof(s_last));
    s_last.laeuft = true;
    s_last.phase = "schreiben";
    s_last.ziel_mb = mb;
    /* Niedriger als Abfrage und Protokoll: Die Pruefung soll den Betrieb
     * belasten, nicht verdraengen. */
    if (xTaskCreate(lasttest_task, "lasttest", 3072, NULL, 2, NULL) != pdPASS) {
        s_last.laeuft = false;
        return false;
    }
    return true;
}

void st_log_lasttest_stand(st_log_lasttest_t *out)
{
    *out = s_last;
    if (out->phase == NULL) {
        out->phase = "";
    }
}

void st_log_leitstand(const char *was, const char *weitere)
{
    uint32_t zeit = jetzt();
    if (s_mtx == NULL || zeit == 0) {
        return;
    }
    char id[24], felder[128];
    st_device_id(id, sizeof(id));
    snprintf(felder, sizeof(felder), "\"was\":\"%s\"%s%s", was, weitere && weitere[0] ? "," : "", weitere ? weitere : "");
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    ereignis(zeit, id, "leitstand", felder);
    xSemaphoreGive(s_mtx);
}
