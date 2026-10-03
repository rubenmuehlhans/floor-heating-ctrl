#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "protokoll.h"

/*
 * Messgroessen aus einem Geraetezustand. Welche Spalten ein Geraet hat,
 * folgt aus seinem Aufbau -- Fuehler, Raeume, Kanaele --, nicht daraus, ob
 * gerade ein Wert vorliegt. Ein Raum, dessen Thermometer eine Minute schweigt,
 * behaelt seine Spalte und bekommt eine leere Zelle; sonst begaenne mit jedem
 * Aussetzer eine neue Datei.
 */

static const cJSON *feld(const cJSON *o, const char *k)
{
    return o ? cJSON_GetObjectItemCaseSensitive(o, k) : NULL;
}

static float zahl(const cJSON *o, const char *k)
{
    const cJSON *j = feld(o, k);
    return cJSON_IsNumber(j) ? (float)j->valuedouble : NAN;
}

static bool wahr(const cJSON *o, const char *k)
{
    return cJSON_IsTrue(feld(o, k));
}

static const char *text(const cJSON *o, const char *k)
{
    const cJSON *j = feld(o, k);
    return cJSON_IsString(j) && j->valuestring ? j->valuestring : "";
}

static float schalter(bool bekannt, bool ein)
{
    return bekannt ? (ein ? 1.0f : 0.0f) : NAN;
}

static void aus_f(pk_ausgabe_t aus, void *ctx, float wert, pk_art_t art, const char *fmt, ...)
{
    char k[PK_SCHLUESSEL_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(k, sizeof(k), fmt, ap);
    va_end(ap);
    aus(ctx, k, wert, art);
}

int pk_ladephase(const char *t)
{
    if (t == NULL) {
        return 0;
    }
    if (strcmp(t, "keine Ladung") == 0) {
        return 1;
    }
    if (strcmp(t, "wird geladen") == 0) {
        return 2;
    }
    if (strcmp(t, "geladen") == 0) {
        return 3;
    }
    return 0;
}

/* Zustand des Geraets selbst, bei allen Arten am Ende */
static void geraet(const cJSON *z, pk_ausgabe_t aus, void *ctx)
{
    aus(ctx, "geraet.heap", zahl(z, "heap"), PK_MITTEL);
    aus(ctx, "geraet.rssi", zahl(feld(z, "net"), "rssi"), PK_MITTEL);
    aus(ctx, "geraet.laufzeit", zahl(z, "uptime_s"), PK_LETZT);
}

/* Ob das Geraet einen eigenen, zugeordneten Fuehler dieser Rolle hat */
static bool eigene_rolle(const cJSON *z, const char *rolle)
{
    const cJSON *p;
    cJSON_ArrayForEach(p, feld(z, "probes"))
    {
        if (!cJSON_IsFalse(feld(p, "assigned")) && strcmp(text(p, "role"), rolle) == 0) {
            return true;
        }
    }
    return false;
}

void pk_heizgeraet(const cJSON *z, pk_ausgabe_t aus, void *ctx)
{
    /* Eigene Fuehler, wie in der App: zugeordnet und mit Rolle */
    const cJSON *p;
    cJSON_ArrayForEach(p, feld(z, "probes"))
    {
        const char *rolle = text(p, "role");
        if (!rolle[0] || strcmp(rolle, "none") == 0 || cJSON_IsFalse(feld(p, "assigned"))) {
            continue;
        }
        aus_f(aus, ctx, zahl(p, "temp_c"), PK_MITTEL, "fuehler.%s", rolle);
    }

    /* Brenner und Fuellstand zeichnet nur das Geraet auf, das sie misst. */
    const cJSON *b = feld(z, "burner");
    bool abgas = eigene_rolle(z, "abgas");
    if (abgas) {
        bool eigen = wahr(b, "known") && !wahr(b, "remote");
        aus(ctx, "brenner", schalter(eigen, wahr(b, "running")), PK_MITTEL);
    }
    const cJSON *c = feld(z, "charge");
    if (eigene_rolle(z, "puffer")) {
        aus(ctx, "fuellstand", wahr(c, "puffer_remote") ? NAN : zahl(c, "level"), PK_MITTEL);
    }

    const cJSON *k;
    cJSON_ArrayForEach(k, feld(z, "circuits"))
    {
        int id = (int)zahl(k, "id");
        const cJSON *ein = feld(k, "on");
        aus_f(aus, ctx, schalter(cJSON_IsBool(ein), cJSON_IsTrue(ein)), PK_MITTEL, "pumpe.%d", id);
    }
    const cJSON *kp = feld(z, "boiler_pump");
    if (wahr(kp, "enabled")) {
        const cJSON *ein = feld(kp, "on");
        aus(ctx, "kkp", schalter(cJSON_IsBool(ein), cJSON_IsTrue(ein)), PK_MITTEL);
    }

    /* Ergaenzt gegenueber der App */
    if (abgas) {
        aus(ctx, "brenner.starts", zahl(b, "starts_today"), PK_LETZT);
        aus(ctx, "brenner.laufzeit", zahl(b, "runtime_today_s"), PK_LETZT);
        aus(ctx, "brenner.oel", zahl(b, "litres_today"), PK_LETZT);
        aus(ctx, "abgas.bezug", zahl(b, "baseline_c"), PK_MITTEL);
    }
    if (c != NULL) {
        aus(ctx, "ladung.phase", (float)pk_ladephase(text(c, "phase")), PK_LETZT);
        aus(ctx, "ladung.spreizung", zahl(c, "spread_k"), PK_MITTEL);
    }
    cJSON_ArrayForEach(k, feld(z, "circuits"))
    {
        int id = (int)zahl(k, "id");
        const cJSON *d = feld(k, "demand");
        const cJSON *s = feld(k, "stale");
        aus_f(aus, ctx, schalter(cJSON_IsBool(d), cJSON_IsTrue(d)), PK_MITTEL, "kreis.%d.bedarf", id);
        aus_f(aus, ctx, schalter(cJSON_IsBool(s), cJSON_IsTrue(s)), PK_MITTEL, "kreis.%d.veraltet", id);
    }
    const cJSON *f = feld(z, "findings");
    aus(ctx, "befunde", cJSON_IsArray(f) ? (float)cJSON_GetArraySize(f) : NAN, PK_LETZT);
    geraet(z, aus, ctx);
}

void pk_verteiler(const cJSON *z, const cJSON *bedarf, pk_ausgabe_t aus, void *ctx)
{
    const cJSON *r;
    cJSON_ArrayForEach(r, feld(z, "rooms"))
    {
        int n = (int)zahl(r, "id");
        bool gueltig = wahr(r, "temp_valid");
        bool heizt = strcmp(text(r, "mode"), "off") != 0;
        aus_f(aus, ctx, gueltig ? zahl(r, "temp_c") : NAN, PK_MITTEL, "raum.%d.ist", n);
        /* Ein ausgeschalteter Raum hat keinen wirksamen Sollwert; die Luecke
         * zeigt das, wie in der App. */
        aus_f(aus, ctx, heizt ? zahl(r, "target_c") : NAN, PK_MITTEL, "raum.%d.soll", n);
        aus_f(aus, ctx, zahl(r, "target_position"), PK_MITTEL, "raum.%d.stellung", n);
        aus_f(aus, ctx, gueltig ? zahl(r, "humidity") : NAN, PK_MITTEL, "raum.%d.feuchte", n);
        aus_f(aus, ctx, heizt ? (float)PK_BETRIEB_HEIZ : (float)PK_BETRIEB_AUS, PK_LETZT, "raum.%d.betrieb", n);
        aus_f(aus, ctx, zahl(r, "battery"), PK_LETZT, "raum.%d.batterie", n);
    }
    const cJSON *k;
    cJSON_ArrayForEach(k, feld(z, "channels"))
    {
        int n = (int)zahl(k, "id");
        bool bekannt = !cJSON_IsFalse(feld(k, "known"));
        aus_f(aus, ctx, bekannt ? zahl(k, "position") : NAN, PK_MITTEL, "kanal.%d.stellung", n);
    }
    int i = 0;
    const cJSON *f;
    cJSON_ArrayForEach(f, feld(feld(z, "local_sensors"), "ds18b20"))
    {
        aus_f(aus, ctx, wahr(f, "valid") ? zahl(f, "temp_c") : NAN, PK_MITTEL, "vorlauf.%d", i++);
    }
    const cJSON *a = feld(z, "outdoor");
    if (wahr(a, "set")) {
        aus(ctx, "aussen", wahr(a, "valid") ? zahl(a, "temp_c") : NAN, PK_MITTEL);
    }

    /* Bedarfsmeldung an die Heizungsgeraete, aus GET /api/demand */
    const cJSON *d = bedarf != NULL ? feld(bedarf, "demand") : NULL;
    aus(ctx, "bedarf", schalter(cJSON_IsBool(d), cJSON_IsTrue(d)), PK_MITTEL);
    aus(ctx, "raeume.rufend", bedarf != NULL ? zahl(bedarf, "rooms_calling") : NAN, PK_MITTEL);
    aus(ctx, "kanaele.offen", bedarf != NULL ? zahl(bedarf, "open_channels") : NAN, PK_MITTEL);
    geraet(z, aus, ctx);
}

void pk_befunde(const cJSON *z, char *out, size_t len)
{
    size_t pos = 0;
    if (len == 0) {
        return;
    }
    out[0] = '\0';
    const cJSON *f;
    cJSON_ArrayForEach(f, feld(z, "findings"))
    {
        const char *code = text(f, "code");
        if (!code[0]) {
            continue;
        }
        int n = snprintf(out + pos, len - pos, "%s%s", pos ? "," : "", code);
        if (n < 0 || (size_t)n >= len - pos) {
            out[pos] = '\0';
            return;
        }
        pos += (size_t)n;
    }
}
