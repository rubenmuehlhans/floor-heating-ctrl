#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "protokoll.h"

/* ------------------------------------------------------------------ */
/* Zeit und Namen                                                      */
/* ------------------------------------------------------------------ */

/* Kalender aus Tagen seit 1970, ohne gmtime: Das Ergebnis haengt so weder von
 * der Zeitzone noch von der C-Bibliothek ab. Verfahren nach Howard Hinnant,
 * "civil_from_days". */
static void datum(uint32_t tage, int *jahr, unsigned *monat, unsigned *tag)
{
    long z = (long)tage + 719468;
    long era = z / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long y = (long)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    *tag = doy - (153 * mp + 2) / 5 + 1;
    *monat = mp < 10 ? mp + 3 : mp - 9;
    *jahr = (int)(y + (*monat <= 2));
}

void pk_iso(uint32_t epoch, char *out, size_t len)
{
    int j;
    unsigned m, t;
    datum(epoch / 86400u, &j, &m, &t);
    uint32_t s = epoch % 86400u;
    snprintf(out, len, "%04d-%02u-%02uT%02u:%02u:%02uZ", j, m, t, (unsigned)(s / 3600),
             (unsigned)(s / 60 % 60), (unsigned)(s % 60));
}

void pk_tagespfad(uint32_t epoch, char *out, size_t len)
{
    int j;
    unsigned m, t;
    datum(epoch / 86400u, &j, &m, &t);
    snprintf(out, len, "%04d/%04d-%02u-%02u", j, j, m, t);
}

void pk_dateiname(const char *kennung, uint8_t teil, bool mittel, char *out, size_t len)
{
    const char *art = mittel ? ".5min" : "";
    if (teil <= 1) {
        snprintf(out, len, "%s%s.csv", kennung, art);
    } else {
        snprintf(out, len, "%s.%u%s.csv", kennung, (unsigned)teil, art);
    }
}

void pk_zahl(float wert, char *out, size_t len)
{
    if (len == 0) {
        return;
    }
    if (isnan(wert) || isinf(wert)) {
        out[0] = '\0';
        return;
    }
    if (fabsf(wert) < 1e7f && wert == (float)(long)wert) {
        snprintf(out, len, "%ld", (long)wert);
        return;
    }
    snprintf(out, len, "%.3f", (double)wert);
    /* Nullen am Ende weg, und ein Punkt ohne Stellen danach auch */
    char *p = strchr(out, '.');
    if (p != NULL) {
        char *e = out + strlen(out) - 1;
        while (e > p && *e == '0') {
            *e-- = '\0';
        }
        if (e == p) {
            *e = '\0';
        }
    }
    if (strcmp(out, "-0") == 0) {
        snprintf(out, len, "0");
    }
}

/* ------------------------------------------------------------------ */
/* Kopf                                                                */
/* ------------------------------------------------------------------ */

/* FNV-1a, 32 Bit */
static uint32_t fnv(uint32_t h, const char *s)
{
    while (*s) {
        h ^= (uint8_t)*s++;
        h *= 16777619u;
    }
    return h;
}

void pk_kopf_beginnen(pk_kopf_t *k)
{
    k->hash = fnv(2166136261u, "zeit");
    k->spalten = 0;
}

void pk_kopf_schluessel(pk_kopf_t *k, const char *schluessel)
{
    k->hash = fnv(k->hash, ",");
    k->hash = fnv(k->hash, schluessel);
    k->spalten++;
}

bool pk_kopf_aus_zeile(const char *zeile, pk_kopf_t *k)
{
    if (strncmp(zeile, "zeit", 4) != 0 || (zeile[4] != ',' && zeile[4] != '\0' && zeile[4] != '\r' &&
                                           zeile[4] != '\n')) {
        return false;
    }
    pk_kopf_beginnen(k);
    const char *p = zeile + 4;
    char s[PK_SCHLUESSEL_MAX];
    while (*p == ',') {
        p++;
        size_t n = 0;
        while (*p && *p != ',' && *p != '\r' && *p != '\n') {
            if (n + 1 < sizeof(s)) {
                s[n++] = *p;
            }
            p++;
        }
        s[n] = '\0';
        pk_kopf_schluessel(k, s);
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Reihe                                                               */
/* ------------------------------------------------------------------ */

void pk_reihe_init(pk_reihe_t *r)
{
    memset(r, 0, sizeof(*r));
}

void pk_reihe_frei(pk_reihe_t *r)
{
    free(r->letzt);
    free(r->summe);
    free(r->anzahl);
    pk_reihe_init(r);
}

bool pk_reihe_spalten(pk_reihe_t *r, const pk_kopf_t *k)
{
    if (k->spalten > r->kapazitaet) {
        float *l = realloc(r->letzt, k->spalten * sizeof(float));
        if (l != NULL) {
            r->letzt = l;
        }
        float *s = realloc(r->summe, k->spalten * sizeof(float));
        if (s != NULL) {
            r->summe = s;
        }
        uint8_t *a = realloc(r->anzahl, k->spalten);
        if (a != NULL) {
            r->anzahl = a;
        }
        if (l == NULL || s == NULL || a == NULL) {
            return false;
        }
        r->kapazitaet = k->spalten;
    }
    for (uint16_t i = 0; i < k->spalten; i++) {
        r->letzt[i] = NAN;
        r->summe[i] = 0.0f;
        r->anzahl[i] = 0;
    }
    r->kopf = *k;
    r->platz = 0;
    r->schalter_n = 0;
    return true;
}

bool pk_mittel_faellig(const pk_reihe_t *r, uint32_t epoch)
{
    return r->platz != 0 && pk_platz(epoch) != r->platz;
}

bool pk_mittel_zeile(pk_reihe_t *r, char *buf, size_t len)
{
    if (r->platz == 0) {
        return false;
    }
    bool etwas = false;
    for (uint16_t i = 0; i < r->kopf.spalten; i++) {
        etwas = etwas || r->anzahl[i] > 0;
    }
    size_t pos = 0;
    bool ok = etwas;
    if (ok) {
        pk_iso(r->platz * PK_PLATZ_S, buf, len);
        pos = strlen(buf);
        for (uint16_t i = 0; i < r->kopf.spalten && ok; i++) {
            char z[24];
            pk_zahl(r->anzahl[i] ? r->summe[i] / r->anzahl[i] : NAN, z, sizeof(z));
            int n = snprintf(buf + pos, len - pos, ",%s", z);
            ok = n >= 0 && (size_t)n < len - pos;
            pos += ok ? (size_t)n : 0;
        }
        if (ok && pos + 2 <= len) {
            buf[pos++] = '\n';
            buf[pos] = '\0';
        } else {
            ok = false;
        }
    }
    for (uint16_t i = 0; i < r->kopf.spalten; i++) {
        r->summe[i] = 0.0f;
        r->anzahl[i] = 0;
    }
    r->platz = 0;
    return ok;
}

static void anhaengen(pk_zeile_t *z, const char *s)
{
    size_t n = strlen(s);
    if (z->voll || z->pos + n + 1 >= z->len) {
        z->voll = true;
        return;
    }
    memcpy(z->buf + z->pos, s, n + 1);
    z->pos += n;
}

void pk_zeile_beginnen(pk_zeile_t *z, pk_reihe_t *r, uint32_t epoch, char *buf, size_t len)
{
    memset(z, 0, sizeof(*z));
    z->r = r;
    z->buf = buf;
    z->len = len;
    if (len > 0) {
        buf[0] = '\0';
    }
    char t[24];
    pk_iso(epoch, t, sizeof(t));
    anhaengen(z, t);
    if (r->platz == 0) {
        r->platz = pk_platz(epoch);
    }
    r->zuletzt = epoch;
}

void pk_zeile_wert(pk_zeile_t *z, float wert, pk_art_t art)
{
    pk_reihe_t *r = z->r;
    char s[24];
    s[0] = ',';
    pk_zahl(wert, s + 1, sizeof(s) - 1);
    anhaengen(z, s);
    if (z->i < r->kopf.spalten) {
        r->letzt[z->i] = wert;
        if (!isnan(wert)) {
            if (art == PK_LETZT) {
                r->summe[z->i] = wert;
                r->anzahl[z->i] = 1;
            } else if (r->anzahl[z->i] < 255) {
                r->summe[z->i] += wert;
                r->anzahl[z->i]++;
            }
        }
    }
    z->i++;
}

bool pk_zeile_ende(pk_zeile_t *z)
{
    anhaengen(z, "\n");
    return !z->voll && z->i == z->r->kopf.spalten;
}

bool pk_zeile_wiederholen(pk_reihe_t *r, uint32_t epoch, char *buf, size_t len)
{
    if (r->kopf.spalten == 0 || r->letzt == NULL) {
        return false;
    }
    /* Ob ein Wert als Mittel oder als letzter Wert eingeht, steht nicht in
     * der Reihe. Fuer eine Wiederholung ist das gleich: Der Mittelwert
     * gleicher Werte ist derselbe Wert. */
    pk_zeile_t z;
    pk_zeile_beginnen(&z, r, epoch, buf, len);
    for (uint16_t i = 0; i < r->kopf.spalten; i++) {
        pk_zeile_wert(&z, r->letzt[i], PK_MITTEL);
    }
    return pk_zeile_ende(&z);
}

/* ------------------------------------------------------------------ */
/* Wechsel und Ereignisse                                              */
/* ------------------------------------------------------------------ */

static bool endet(const char *s, const char *ende)
{
    size_t a = strlen(s), b = strlen(ende);
    return a >= b && strcmp(s + a - b, ende) == 0;
}

pk_wechsel_t pk_wechsel(const char *k, float alt, float neu)
{
    if (isnan(alt) || isnan(neu) || alt == neu) {
        return PK_W_KEINER;
    }
    if (strcmp(k, "brenner") == 0 || strcmp(k, "kkp") == 0 || strncmp(k, "pumpe.", 6) == 0) {
        return PK_W_SCHALTER;
    }
    if (strncmp(k, "raum.", 5) == 0 && endet(k, ".soll")) {
        return PK_W_SOLLWERT;
    }
    if (strncmp(k, "raum.", 5) == 0 && endet(k, ".betrieb")) {
        return PK_W_BETRIEBSART;
    }
    return PK_W_KEINER;
}

uint32_t pk_schalter_dauer(pk_reihe_t *r, uint16_t spalte, uint32_t epoch)
{
    for (uint8_t i = 0; i < r->schalter_n; i++) {
        if (r->schalter[i].spalte == spalte) {
            uint32_t d = epoch >= r->schalter[i].seit ? epoch - r->schalter[i].seit : 0;
            r->schalter[i].seit = epoch;
            return d;
        }
    }
    if (r->schalter_n < PK_SCHALTER_MAX) {
        r->schalter[r->schalter_n].spalte = spalte;
        r->schalter[r->schalter_n].seit = epoch;
        r->schalter_n++;
    }
    return 0;
}

size_t pk_ereignis(char *buf, size_t len, uint32_t epoch, const char *geraet, const char *art,
                   const char *felder)
{
    char t[24];
    pk_iso(epoch, t, sizeof(t));
    int n = snprintf(buf, len, "{\"zeit\":\"%s\",\"geraet\":\"%s\",\"art\":\"%s\"%s%s}\n", t, geraet, art,
                     felder && felder[0] ? "," : "", felder ? felder : "");
    return n > 0 && (size_t)n < len ? (size_t)n : 0;
}

/* ------------------------------------------------------------------ */
/* Lesen                                                               */
/* ------------------------------------------------------------------ */

/* Tage seit 1970 aus einem Datum, Umkehrung von datum() ("days_from_civil") */
static uint32_t tage(int j, unsigned m, unsigned t)
{
    j -= m <= 2;
    long era = (j >= 0 ? j : j - 399) / 400;
    unsigned yoe = (unsigned)(j - era * 400);
    unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + t - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (uint32_t)(era * 146097 + (long)doe - 719468);
}

static bool ziffern(const char *s, int n, unsigned *out)
{
    unsigned v = 0;
    for (int i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return false;
        }
        v = v * 10 + (unsigned)(s[i] - '0');
    }
    *out = v;
    return true;
}

bool pk_iso_lesen(const char *s, uint32_t *epoch)
{
    unsigned j, m, t, h, mi, se;
    if (strlen(s) < 20 || s[4] != '-' || s[7] != '-' || s[10] != 'T' || s[13] != ':' || s[16] != ':' ||
        s[19] != 'Z' || !ziffern(s, 4, &j) || !ziffern(s + 5, 2, &m) || !ziffern(s + 8, 2, &t) ||
        !ziffern(s + 11, 2, &h) || !ziffern(s + 14, 2, &mi) || !ziffern(s + 17, 2, &se) || j < 1970 || m < 1 ||
        m > 12 || t < 1 || t > 31 || h > 23 || mi > 59 || se > 60) {
        return false;
    }
    *epoch = tage((int)j, m, t) * 86400u + h * 3600u + mi * 60u + se;
    return true;
}

bool pk_spalten_waehlen(const char *kopf, const char *const *schluessel, int n, int16_t *spalte)
{
    pk_kopf_t k;
    if (!pk_kopf_aus_zeile(kopf, &k)) {
        return false;
    }
    for (int i = 0; i < n; i++) {
        spalte[i] = -1;
    }
    const char *p = kopf + 4;
    int16_t nr = 0;
    while (*p == ',') {
        p++;
        const char *e = p;
        while (*e && *e != ',' && *e != '\r' && *e != '\n') {
            e++;
        }
        for (int i = 0; i < n; i++) {
            if (spalte[i] < 0 && strlen(schluessel[i]) == (size_t)(e - p) &&
                strncmp(p, schluessel[i], (size_t)(e - p)) == 0) {
                spalte[i] = nr;
            }
        }
        nr++;
        p = e;
    }
    return true;
}

bool pk_zeile_lesen(const char *zeile, const int16_t *spalte, int n, uint32_t *zeit, float *werte)
{
    if (!pk_iso_lesen(zeile, zeit) || zeile[20] != ',') {
        return false;
    }
    for (int i = 0; i < n; i++) {
        werte[i] = NAN;
    }
    const char *p = zeile + 20;
    int16_t nr = 0;
    while (*p == ',') {
        p++;
        const char *e = p;
        while (*e && *e != ',' && *e != '\r' && *e != '\n') {
            e++;
        }
        if (e > p) {
            for (int i = 0; i < n; i++) {
                if (spalte[i] == nr) {
                    char z[24];
                    size_t l = (size_t)(e - p) < sizeof(z) - 1 ? (size_t)(e - p) : sizeof(z) - 1;
                    memcpy(z, p, l);
                    z[l] = '\0';
                    char *ende;
                    float v = strtof(z, &ende);
                    werte[i] = ende != z ? v : NAN;
                }
            }
        }
        nr++;
        p = e;
    }
    return true;
}

bool pk_ereignis_zeit(const char *zeile, uint32_t *epoch)
{
    const char *p = strstr(zeile, "\"zeit\":\"");
    return p != NULL && pk_iso_lesen(p + 8, epoch);
}
