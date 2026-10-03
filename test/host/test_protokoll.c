/*
 * Pruefungen fuer das Protokoll des Leitstands (components/protokoll).
 *
 * Eigenes Programm, weil es cJSON braucht; test_logic kommt ohne aus. Die
 * Vorlagen in vorlage/ sind Zustaende der Anlage vom 23. September, mit
 * ersetzten Kennungen und Adressen.
 *
 *   make -C test/host
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "protokoll.h"

static int s_checks;
static int s_failed;

#define CHECK(cond, fmt, ...)                                            \
    do {                                                                 \
        s_checks++;                                                      \
        if (!(cond)) {                                                   \
            s_failed++;                                                  \
            printf("  FEHLER %s:%d  " fmt "\n", __FILE__, __LINE__,      \
                   ##__VA_ARGS__);                                       \
        }                                                                \
    } while (0)

#define CLOSE(a, b, eps) (fabsf((float)(a) - (float)(b)) <= (eps))

static cJSON *laden(const char *name)
{
    char pfad[256];
    snprintf(pfad, sizeof(pfad), "vorlage/%s", name);
    FILE *f = fopen(pfad, "rb");
    if (f == NULL) {
        printf("  Vorlage %s fehlt\n", pfad);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    size_t gelesen = fread(buf, 1, (size_t)n, f);
    buf[gelesen] = '\0';
    fclose(f);
    cJSON *j = cJSON_Parse(buf);
    free(buf);
    if (j == NULL) {
        printf("  Vorlage %s nicht lesbar\n", pfad);
        exit(1);
    }
    return j;
}

/* Sammelt, was der Katalog ausgibt */
typedef struct {
    char k[160][PK_SCHLUESSEL_MAX];
    float v[160];
    pk_art_t a[160];
    int n;
} sammler_t;

static void sammeln(void *ctx, const char *k, float v, pk_art_t a)
{
    sammler_t *s = ctx;
    if (s->n < 160) {
        snprintf(s->k[s->n], PK_SCHLUESSEL_MAX, "%s", k);
        s->v[s->n] = v;
        s->a[s->n] = a;
        s->n++;
    }
}

static int finden(const sammler_t *s, const char *k)
{
    for (int i = 0; i < s->n; i++) {
        if (strcmp(s->k[i], k) == 0) {
            return i;
        }
    }
    return -1;
}

static float wert(const sammler_t *s, const char *k)
{
    int i = finden(s, k);
    return i < 0 ? -9999.0f : s->v[i];
}

static void kopf_von(const sammler_t *s, pk_kopf_t *k)
{
    pk_kopf_beginnen(k);
    for (int i = 0; i < s->n; i++) {
        pk_kopf_schluessel(k, s->k[i]);
    }
}

/* ------------------------------------------------------------------ */

static void test_katalog_kessel(void)
{
    printf("Protokoll: Katalog am Kessel\n");
    cJSON *z = laden("kessel-state.json");
    sammler_t s = {0};
    pk_heizgeraet(z, sammeln, &s);

    CHECK(CLOSE(wert(&s, "fuehler.kessel_vl"), 73.9375f, 0.001f), "Kesselvorlauf %.3f",
          wert(&s, "fuehler.kessel_vl"));
    CHECK(CLOSE(wert(&s, "fuehler.abgas"), 38.75f, 0.001f), "Abgas");
    CHECK(wert(&s, "brenner") == 0.0f, "Brenner aus, nicht %.1f", wert(&s, "brenner"));
    CHECK(wert(&s, "kkp") == 1.0f, "Kesselkreispumpe laeuft");
    CHECK(wert(&s, "brenner.starts") == 3.0f, "drei Starts");
    CHECK(wert(&s, "brenner.laufzeit") == 3198.0f, "Laufzeit des Tages");
    CHECK(CLOSE(wert(&s, "brenner.oel"), 1.954f, 0.001f), "Oel geschaetzt");
    CHECK(CLOSE(wert(&s, "abgas.bezug"), 38.75f, 0.001f), "Bezugslinie");
    CHECK(wert(&s, "ladung.phase") == 1.0f, "Phase \"keine Ladung\" ist 1");
    CHECK(CLOSE(wert(&s, "ladung.spreizung"), -0.125f, 0.001f), "Spreizung");
    CHECK(wert(&s, "befunde") == 0.0f, "keine Befunde");
    CHECK(wert(&s, "geraet.heap") == 63448.0f && wert(&s, "geraet.rssi") == -56.0f &&
              wert(&s, "geraet.laufzeit") == 761.0f,
          "Zustand des Geraets");
    CHECK(finden(&s, "fuellstand") < 0, "ohne eigenen Pufferfuehler kein Fuellstand");
    CHECK(finden(&s, "pumpe.1") < 0, "ohne Heizkreise keine Pumpenspalten");
    CHECK(s.a[finden(&s, "brenner.starts")] == PK_LETZT && s.a[finden(&s, "brenner")] == PK_MITTEL,
          "Zaehler gehen als letzter Wert ein, Schalter als Anteil");
    cJSON_Delete(z);
}

static void test_katalog_speicher(void)
{
    printf("Protokoll: Katalog am Pufferspeicher\n");
    cJSON *z = laden("speicher-state.json");
    sammler_t s = {0};
    pk_heizgeraet(z, sammeln, &s);

    CHECK(CLOSE(wert(&s, "fuehler.puffer"), 70.1f, 0.001f), "Speicher");
    CHECK(finden(&s, "fuehler.hk1_vl") >= 0 && finden(&s, "fuehler.hk2_rl") >= 0, "Heizkreisfuehler");
    CHECK(finden(&s, "brenner") < 0, "ohne eigenen Abgasfuehler kein Brenner -- den zeichnet der Kessel auf");
    CHECK(finden(&s, "brenner.starts") < 0, "und keine Brennerzaehler");
    CHECK(CLOSE(wert(&s, "fuellstand"), 0.9073f, 0.001f), "Fuellstand");
    CHECK(wert(&s, "pumpe.1") == 1.0f, "Pumpe 1 laeuft");
    CHECK(wert(&s, "kreis.1.bedarf") == 1.0f && wert(&s, "kreis.1.veraltet") == 0.0f, "Bedarf je Kreis");
    CHECK(finden(&s, "kkp") < 0, "keine Kesselkreispumpe am Speicher");
    cJSON_Delete(z);
}

static void test_katalog_verteiler(void)
{
    printf("Protokoll: Katalog am Verteiler\n");
    cJSON *z = laden("verteiler-state.json");
    cJSON *d = laden("verteiler-demand.json");
    sammler_t s = {0};
    pk_verteiler(z, d, sammeln, &s);

    CHECK(CLOSE(wert(&s, "raum.1.ist"), 21.4f, 0.001f), "Kueche %.2f", wert(&s, "raum.1.ist"));
    CHECK(wert(&s, "raum.1.soll") == 20.0f && wert(&s, "raum.1.betrieb") == 1.0f, "Sollwert und Heizbetrieb");
    CHECK(wert(&s, "raum.1.feuchte") == 52.0f && wert(&s, "raum.1.batterie") == 67.0f, "Feuchte, Batterie");
    CHECK(finden(&s, "kanal.4.stellung") >= 0, "Kanaele");
    CHECK(finden(&s, "vorlauf.0") < 0, "ohne 1-Wire-Fuehler keine Vorlaufspalte");
    int a = finden(&s, "aussen");
    CHECK(a >= 0 && isnan(s.v[a]), "Aussenfuehler eingetragen, aber ohne Wert: leere Zelle");
    CHECK(wert(&s, "bedarf") == 1.0f && wert(&s, "raeume.rufend") == 3.0f && wert(&s, "kanaele.offen") == 4.0f,
          "Bedarfsmeldung");

    /* Dieselben Schluessel, die die App aufzeichnet */
    const char *app[] = {"raum.1.ist", "raum.1.soll", "raum.1.stellung", "raum.1.feuchte", "kanal.1.stellung"};
    for (size_t i = 0; i < sizeof(app) / sizeof(app[0]); i++) {
        CHECK(finden(&s, app[i]) >= 0, "Schluessel der App %s", app[i]);
    }

    /* Ein schweigendes Thermometer aendert die Spalten nicht. */
    pk_kopf_t vorher, nachher;
    kopf_von(&s, &vorher);
    cJSON *r1 = cJSON_GetArrayItem(cJSON_GetObjectItem(z, "rooms"), 0);
    cJSON_ReplaceItemInObject(r1, "temp_valid", cJSON_CreateFalse());
    sammler_t s2 = {0};
    pk_verteiler(z, d, sammeln, &s2);
    kopf_von(&s2, &nachher);
    CHECK(vorher.hash == nachher.hash && vorher.spalten == nachher.spalten,
          "ohne Messwert bleibt der Kopf gleich");
    CHECK(isnan(wert(&s2, "raum.1.ist")) && isnan(wert(&s2, "raum.1.feuchte")), "die Zellen bleiben leer");

    /* Ohne Bedarfsantwort bleiben die Bedarfsspalten, leer. */
    sammler_t s3 = {0};
    pk_verteiler(z, NULL, sammeln, &s3);
    CHECK(finden(&s3, "bedarf") >= 0 && isnan(wert(&s3, "bedarf")), "Bedarf ohne Antwort leer");
    cJSON_Delete(z);
    cJSON_Delete(d);
}

static void test_befunde(void)
{
    printf("Protokoll: Befunde\n");
    cJSON *z = cJSON_Parse("{\"findings\":[{\"code\":\"backflow\"},{\"code\":\"probe_errors\"}]}");
    char b[64];
    pk_befunde(z, b, sizeof(b));
    CHECK(strcmp(b, "backflow,probe_errors") == 0, "Kennungen \"%s\"", b);
    pk_befunde(z, b, 10);
    CHECK(strcmp(b, "backflow") == 0, "zu knapp: nur ganze Kennungen, nicht \"%s\"", b);
    cJSON_Delete(z);
}

static void test_zeit(void)
{
    printf("Protokoll: Zeit und Namen\n");
    char s[40];
    pk_iso(1790141877u, s, sizeof(s));
    CHECK(strcmp(s, "2026-09-23T05:37:57Z") == 0, "ISO %s", s);
    pk_iso(1835438400u, s, sizeof(s));
    CHECK(strcmp(s, "2028-02-29T12:00:00Z") == 0, "Schalttag %s", s);
    pk_tagespfad(1798761599u, s, sizeof(s));
    CHECK(strcmp(s, "2026/2026-12-31") == 0, "Silvester %s", s);
    CHECK(pk_tag(1790121480u) + 1 == pk_tag(1790121600u), "Mitternacht in UTC");

    pk_dateiname("heiz_00000d", 1, false, s, sizeof(s));
    CHECK(strcmp(s, "heiz_00000d.csv") == 0, "%s", s);
    pk_dateiname("heiz_00000d", 2, false, s, sizeof(s));
    CHECK(strcmp(s, "heiz_00000d.2.csv") == 0, "%s", s);
    pk_dateiname("heiz_00000d", 1, true, s, sizeof(s));
    CHECK(strcmp(s, "heiz_00000d.5min.csv") == 0, "%s", s);
    pk_dateiname("heiz_00000d", 3, true, s, sizeof(s));
    CHECK(strcmp(s, "heiz_00000d.3.5min.csv") == 0, "%s", s);

    const struct {
        float v;
        const char *t;
    } faelle[] = {{NAN, ""}, {21.0f, "21"}, {21.4f, "21.4"}, {0.9073f, "0.907"}, {-0.125f, "-0.125"},
                  {-0.0001f, "0"}, {63448.0f, "63448"}, {1.954333f, "1.954"}, {-56.0f, "-56"}};
    for (size_t i = 0; i < sizeof(faelle) / sizeof(faelle[0]); i++) {
        pk_zahl(faelle[i].v, s, sizeof(s));
        CHECK(strcmp(s, faelle[i].t) == 0, "Zahl %s statt %s", s, faelle[i].t);
    }
}

static void test_kopf(void)
{
    printf("Protokoll: Kopf und Pruefwert\n");
    pk_kopf_t a, b, c;
    pk_kopf_beginnen(&a);
    pk_kopf_schluessel(&a, "fuehler.puffer");
    pk_kopf_schluessel(&a, "pumpe.1");
    CHECK(pk_kopf_aus_zeile("zeit,fuehler.puffer,pumpe.1\r\n", &b), "Kopfzeile gelesen");
    CHECK(a.hash == b.hash && b.spalten == 2, "Datei und Zustand ergeben denselben Pruefwert");
    CHECK(pk_kopf_aus_zeile("zeit,pumpe.1,fuehler.puffer", &c) && c.hash != a.hash,
          "andere Reihenfolge, anderer Pruefwert");
    CHECK(!pk_kopf_aus_zeile("2026-09-23T05:37:57Z,1,2", &c), "eine Datenzeile ist kein Kopf");
    CHECK(pk_kopf_aus_zeile("zeit\n", &c) && c.spalten == 0, "Kopf ohne Spalten");
}

/* Drei Spalten: Temperatur, Schalter, Zaehler */
static void abtasten(pk_reihe_t *r, uint32_t t, float temp, float schalter, float zaehler, char *zeile, size_t len)
{
    pk_zeile_t z;
    pk_zeile_beginnen(&z, r, t, zeile, len);
    pk_zeile_wert(&z, temp, PK_MITTEL);
    pk_zeile_wert(&z, schalter, PK_MITTEL);
    pk_zeile_wert(&z, zaehler, PK_LETZT);
    CHECK(pk_zeile_ende(&z), "Zeile vollstaendig");
}

static void test_reihe(void)
{
    printf("Protokoll: Zeilen und Fuenfminutenmittel\n");
    pk_kopf_t k;
    pk_kopf_beginnen(&k);
    pk_kopf_schluessel(&k, "fuehler.puffer");
    pk_kopf_schluessel(&k, "brenner");
    pk_kopf_schluessel(&k, "brenner.starts");
    pk_reihe_t r;
    pk_reihe_init(&r);
    CHECK(pk_reihe_spalten(&r, &k), "Felder angelegt");

    char zeile[256], mittel[256];
    uint32_t t0 = 1790121600u; /* 00:00:00Z, Beginn eines Platzes */
    abtasten(&r, t0, 60.0f, 1.0f, 2.0f, zeile, sizeof(zeile));
    CHECK(strcmp(zeile, "2026-09-23T00:00:00Z,60,1,2\n") == 0, "erste Zeile \"%s\"", zeile);

    /* Zehn Abtastungen im Platz, der Brenner laeuft sechs davon, ein Wert fehlt */
    for (int i = 1; i < 10; i++) {
        float temp = i == 5 ? NAN : 60.0f + i;
        CHECK(!pk_mittel_faellig(&r, t0 + i * 30u), "im selben Platz nichts faellig");
        abtasten(&r, t0 + i * 30u, temp, i < 6 ? 1.0f : 0.0f, i < 6 ? 2.0f : 3.0f, zeile, sizeof(zeile));
    }
    CHECK(strcmp(zeile, "2026-09-23T00:04:30Z,69,0,3\n") == 0, "letzte Zeile \"%s\"", zeile);
    CHECK(pk_mittel_faellig(&r, t0 + 300u), "der naechste Platz schliesst den vorigen ab");
    CHECK(pk_mittel_zeile(&r, mittel, sizeof(mittel)), "Mittel gebildet");
    /* Temperatur: (60+61+62+63+64+66+67+68+69)/9 = 64,444; Brenner 6 von 10 */
    CHECK(strcmp(mittel, "2026-09-23T00:00:00Z,64.444,0.6,3\n") == 0, "Mittel \"%s\"", mittel);
    CHECK(!pk_mittel_zeile(&r, mittel, sizeof(mittel)), "ein zweites Mal nichts");

    /* Unveraenderter Zustand: dieselben Werte mit neuer Zeit */
    CHECK(pk_zeile_wiederholen(&r, t0 + 330u, zeile, sizeof(zeile)), "Wiederholung");
    CHECK(strcmp(zeile, "2026-09-23T00:05:30Z,69,0,3\n") == 0, "wiederholt \"%s\"", zeile);

    /* Ruecksprung der Uhr: der laufende Platz wird abgeschlossen, einmal */
    CHECK(pk_mittel_faellig(&r, t0 + 100u), "Ruecksprung schliesst den Platz ab");
    CHECK(pk_mittel_zeile(&r, mittel, sizeof(mittel)) &&
              strcmp(mittel, "2026-09-23T00:05:00Z,69,0,3\n") == 0,
          "Mittel vor dem Ruecksprung \"%s\"", mittel);
    abtasten(&r, t0 + 100u, 61.0f, 0.0f, 3.0f, zeile, sizeof(zeile));
    CHECK(r.platz == pk_platz(t0 + 100u), "und beginnt den Platz der neuen Zeit");

    /* Tageswechsel in UTC: der Platz vor Mitternacht gehoert zum alten Tag */
    pk_reihe_spalten(&r, &k);
    abtasten(&r, 1790121480u, 55.0f, 0.0f, 9.0f, zeile, sizeof(zeile)); /* 23:58Z */
    CHECK(pk_mittel_faellig(&r, 1790121630u), "Mitternacht schliesst ab");
    CHECK(pk_tag(r.platz * PK_PLATZ_S) + 1 == pk_tag(1790121630u), "der Platz liegt am Vortag");
    pk_mittel_zeile(&r, mittel, sizeof(mittel));
    CHECK(strcmp(mittel, "2026-09-22T23:55:00Z,55,0,9\n") == 0, "Mittel des Vortags \"%s\"", mittel);

    /* Alles leer: Zeile mit leeren Zellen, Mittel ohne Inhalt */
    pk_reihe_spalten(&r, &k);
    abtasten(&r, t0, NAN, NAN, NAN, zeile, sizeof(zeile));
    CHECK(strcmp(zeile, "2026-09-23T00:00:00Z,,,\n") == 0, "leere Zellen \"%s\"", zeile);
    CHECK(!pk_mittel_zeile(&r, mittel, sizeof(mittel)), "ohne Werte kein Mittel");

    /* Zu kleiner Puffer und falsche Spaltenzahl werden erkannt */
    pk_zeile_t z;
    pk_zeile_beginnen(&z, &r, t0, zeile, 24);
    pk_zeile_wert(&z, 60.0f, PK_MITTEL);
    pk_zeile_wert(&z, 1.0f, PK_MITTEL);
    pk_zeile_wert(&z, 2.0f, PK_LETZT);
    CHECK(!pk_zeile_ende(&z), "Ueberlauf erkannt");
    pk_zeile_beginnen(&z, &r, t0, zeile, sizeof(zeile));
    pk_zeile_wert(&z, 60.0f, PK_MITTEL);
    CHECK(!pk_zeile_ende(&z), "fehlende Werte erkannt");

    /* Mehr Spalten: die Felder wachsen mit */
    pk_kopf_schluessel(&k, "kkp");
    CHECK(pk_reihe_spalten(&r, &k) && r.kapazitaet >= 4, "vierte Spalte angelegt");
    pk_reihe_frei(&r);
}

static void test_wechsel(void)
{
    printf("Protokoll: Wechsel und Ereignisse\n");
    CHECK(pk_wechsel("brenner", 0.0f, 1.0f) == PK_W_SCHALTER, "Brenner an");
    CHECK(pk_wechsel("pumpe.2", 1.0f, 0.0f) == PK_W_SCHALTER, "Pumpe aus");
    CHECK(pk_wechsel("kkp", NAN, 1.0f) == PK_W_KEINER, "ohne Vorwert kein Ereignis");
    CHECK(pk_wechsel("kkp", 1.0f, 1.0f) == PK_W_KEINER, "ohne Aenderung keines");
    CHECK(pk_wechsel("raum.3.soll", 20.0f, 21.5f) == PK_W_SOLLWERT, "Sollwert");
    CHECK(pk_wechsel("raum.3.betrieb", 1.0f, 0.0f) == PK_W_BETRIEBSART, "Betriebsart");
    CHECK(pk_wechsel("fuehler.puffer", 60.0f, 61.0f) == PK_W_KEINER, "Temperaturen sind keine Ereignisse");
    CHECK(pk_wechsel("raum.3.ist", 20.0f, 21.0f) == PK_W_KEINER, "Isttemperatur auch nicht");

    pk_reihe_t r;
    pk_reihe_init(&r);
    CHECK(pk_schalter_dauer(&r, 4, 1000u) == 0, "erster Wechsel: Dauer unbekannt");
    CHECK(pk_schalter_dauer(&r, 4, 1720u) == 720u, "zweiter: zwoelf Minuten");
    CHECK(pk_schalter_dauer(&r, 7, 1800u) == 0, "andere Spalte fuer sich");

    char b[160];
    size_t n = pk_ereignis(b, sizeof(b), 1790141877u, "heiz_00000d", "brenner", "\"ein\":true,\"dauer_s\":720");
    CHECK(n > 0 && strcmp(b, "{\"zeit\":\"2026-09-23T05:37:57Z\",\"geraet\":\"heiz_00000d\",\"art\":\"brenner\","
                             "\"ein\":true,\"dauer_s\":720}\n") == 0,
          "Ereigniszeile %s", b);
    n = pk_ereignis(b, sizeof(b), 1790141877u, "lst_00000c", "leitstand", NULL);
    CHECK(n > 0 && strstr(b, "\"art\":\"leitstand\"}") != NULL, "ohne weitere Felder %s", b);
    CHECK(pk_ereignis(b, 20, 1790141877u, "x", "y", NULL) == 0, "zu knapp: nichts");
}

/* Der ganze Weg an einem Zustand: Kopf, Zeile, Spaltenzahl */
static void kopf_ausgabe(void *ctx, const char *k, float v, pk_art_t a)
{
    (void)v;
    (void)a;
    pk_kopf_schluessel(ctx, k);
}

static void zeile_ausgabe(void *ctx, const char *k, float v, pk_art_t a)
{
    (void)k;
    pk_zeile_wert(ctx, v, a);
}

static void test_durchgang(void)
{
    printf("Protokoll: vom Zustand zur Zeile\n");
    cJSON *z = laden("verteiler-state.json");
    cJSON *d = laden("verteiler-demand.json");
    pk_kopf_t k;
    pk_kopf_beginnen(&k);
    pk_verteiler(z, d, kopf_ausgabe, &k);
    pk_reihe_t r;
    pk_reihe_init(&r);
    CHECK(pk_reihe_spalten(&r, &k), "Felder");
    char zeile[1024];
    pk_zeile_t zl;
    pk_zeile_beginnen(&zl, &r, 1790141877u, zeile, sizeof(zeile));
    pk_verteiler(z, d, zeile_ausgabe, &zl);
    CHECK(pk_zeile_ende(&zl), "Zeile passt zum Kopf");
    int kommas = 0;
    for (const char *p = zeile; *p; p++) {
        kommas += *p == ',';
    }
    CHECK(kommas == k.spalten, "%d Kommas fuer %u Spalten", kommas, k.spalten);
    CHECK(strncmp(zeile, "2026-09-23T05:37:57Z,21.4,20,", 29) == 0, "beginnt mit Raum 1: %.40s", zeile);
    pk_reihe_frei(&r);
    cJSON_Delete(z);
    cJSON_Delete(d);
}

static void test_lesen(void)
{
    printf("Protokoll: Dateien lesen\n");
    /* Hin und zurueck ueber Jahre, Schalttage und Mitternacht */
    int falsch = 0;
    for (uint32_t t = 0; t < 2000000000u; t += 7919u * 131u) {
        char s[24];
        uint32_t z = 0;
        pk_iso(t, s, sizeof(s));
        if (!pk_iso_lesen(s, &z) || z != t) {
            falsch++;
        }
    }
    CHECK(falsch == 0, "%d Zeiten nicht umkehrbar", falsch);
    uint32_t z;
    CHECK(pk_iso_lesen("2028-02-29T12:00:00Z", &z) && z == 1835438400u, "Schalttag");
    CHECK(!pk_iso_lesen("2026-09-23 05:37:57", &z), "ohne T und Z abgelehnt");
    CHECK(!pk_iso_lesen("2026-13-01T00:00:00Z", &z), "Monat 13 abgelehnt");

    const char *gesucht[] = {"brenner", "fuehler.puffer", "gibt.es.nicht"};
    int16_t sp[3];
    CHECK(pk_spalten_waehlen("zeit,fuehler.puffer,pumpe.1,brenner\r\n", gesucht, 3, sp), "Kopf gelesen");
    CHECK(sp[0] == 2 && sp[1] == 0 && sp[2] == -1, "Spalten %d %d %d", sp[0], sp[1], sp[2]);
    CHECK(!pk_spalten_waehlen("2026-09-23T00:00:00Z,1", gesucht, 3, sp), "Datenzeile ist kein Kopf");
    pk_spalten_waehlen("zeit,fuehler.puffer,pumpe.1,brenner", gesucht, 3, sp);

    float w[3];
    CHECK(pk_zeile_lesen("2026-09-23T00:05:00Z,64.444,,0.6\n", sp, 3, &z, w), "Zeile gelesen");
    CHECK(z == 1790121900u, "Zeit %u", (unsigned)z);
    CHECK(CLOSE(w[0], 0.6f, 1e-6f) && CLOSE(w[1], 64.444f, 1e-4f) && isnan(w[2]), "Werte %f %f %f",
          (double)w[0], (double)w[1], (double)w[2]);
    CHECK(pk_zeile_lesen("2026-09-23T00:05:00Z,,,\n", sp, 3, &z, w) && isnan(w[0]) && isnan(w[1]),
          "leere Zellen");
    CHECK(pk_zeile_lesen("2026-09-23T00:05:00Z,61", sp, 3, &z, w) && CLOSE(w[1], 61.0f, 1e-6f) && isnan(w[0]),
          "abgebrochene letzte Zeile: vorhandene Werte gelten, der Rest fehlt");
    CHECK(!pk_zeile_lesen("zeit,a,b", sp, 3, &z, w), "Kopf ist keine Datenzeile");

    CHECK(pk_ereignis_zeit("{\"zeit\":\"2026-09-23T05:37:57Z\",\"geraet\":\"x\"}", &z) && z == 1790141877u,
          "Zeit eines Ereignisses");
    CHECK(!pk_ereignis_zeit("{\"geraet\":\"x\"}", &z), "ohne Zeit");
}

int main(void)
{
    test_katalog_kessel();
    test_katalog_speicher();
    test_katalog_verteiler();
    test_befunde();
    test_zeit();
    test_kopf();
    test_reihe();
    test_wechsel();
    test_durchgang();
    test_lesen();
    printf("\n%d Pruefungen, %d Fehler\n", s_checks, s_failed);
    return s_failed == 0 ? 0 : 1;
}
