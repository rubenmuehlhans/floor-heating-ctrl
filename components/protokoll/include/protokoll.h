/*
 * Protokoll des Leitstands: Messgroessen, Zeilen, Fuenfminutenmittel.
 *
 * Reines Rechenmodul wie heatlogic: Es kennt weder Karte noch Netz, die Zeit
 * kommt als Parameter herein. Was hier steht, ist ohne Hardware geprueft
 * (test/host); das Schreiben der Dateien bleibt in apps/station.
 *
 * Der Katalog der Messgroessen steht in docs/katalog-messgroessen.md. Die
 * Schluessel sind dieselben wie in der App (Messgroesse in
 * apple/Packages/Verlauf), damit beide in dieselben Reihen schreiben.
 *
 * Der Arbeitsspeicher ist knapp -- der Core Basic hat keinen PSRAM. Deshalb
 * gibt es keine Liste der Messwerte: Der Katalog ruft je Messgroesse eine
 * Ausgabe auf, und der Aufrufer geht einen Zustand zweimal durch, einmal fuer
 * den Kopf und einmal fuer die Zeile. Je Geraet bleiben nur die Mittel und
 * der letzte Wert je Spalte im Speicher, die Spaltennamen nicht; sie stehen in
 * der Kopfzeile der Datei, und ein Pruefwert zeigt, ob sie noch gelten.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Laengster Schluessel, etwa "funk.A4:C1:38:12:34:56.batterie" */
#define PK_SCHLUESSEL_MAX 40

/* Wie eine Messgroesse ins Fuenfminutenmittel eingeht */
typedef enum {
    PK_MITTEL = 0, /* Mittelwert; ein Schaltzustand 0/1 wird so zum Anteil */
    PK_LETZT,      /* letzter Wert: Kennzahlen und Zaehlerstaende */
} pk_art_t;

/* Je Messgroesse ein Aufruf. `wert` ist NAN, wenn die Messgroesse zum Geraet
 * gehoert, gerade aber keinen Wert hat -- die Spalte bleibt dann leer, statt
 * zu verschwinden. */
typedef void (*pk_ausgabe_t)(void *ctx, const char *schluessel, float wert, pk_art_t art);

/* ------------------------------------------------------------------ */
/* Katalog                                                             */
/* ------------------------------------------------------------------ */

/* Kennzahlen, festgelegt im Katalog */
#define PK_BETRIEB_AUS  0
#define PK_BETRIEB_HEIZ 1
int pk_ladephase(const char *text); /* 0 unbekannt, 1 keine Ladung, 2 wird geladen, 3 geladen */

/* Zustand eines Heizungsgeraets, wie ihn GET /api/state liefert */
void pk_heizgeraet(const cJSON *zustand, pk_ausgabe_t aus, void *ctx);

/* Zustand eines Verteilers; `bedarf` ist die Antwort von GET /api/demand
 * oder NULL, dann bleiben die Bedarfsspalten leer. */
void pk_verteiler(const cJSON *zustand, const cJSON *bedarf, pk_ausgabe_t aus, void *ctx);

/* Kennungen der offenen Befunde, durch Komma getrennt, sortiert wie geliefert.
 * Grundlage der Ereignisse "befund". */
void pk_befunde(const cJSON *zustand, char *out, size_t len);

/* ------------------------------------------------------------------ */
/* Zeit und Namen                                                      */
/* ------------------------------------------------------------------ */

/* 2026-09-23T06:24:08Z */
void pk_iso(uint32_t epoch, char *out, size_t len);
/* 2026/2026-09-23 -- Verzeichnis eines Tages, gezaehlt in UTC */
void pk_tagespfad(uint32_t epoch, char *out, size_t len);
/* Tag in UTC als fortlaufende Zahl */
static inline uint32_t pk_tag(uint32_t epoch) { return epoch / 86400u; }
/* Platz der Fuenfminutenmittel */
#define PK_PLATZ_S 300u
static inline uint32_t pk_platz(uint32_t epoch) { return epoch / PK_PLATZ_S; }

/* <kennung>.csv, <kennung>.2.csv, ... und dasselbe mit .5min davor */
void pk_dateiname(const char *kennung, uint8_t teil, bool mittel, char *out, size_t len);

/* Zahl fuer die CSV-Datei: leer fuer NAN, ganze Zahlen ohne Nachkommastellen,
 * sonst hoechstens drei, ohne Nullen am Ende. */
void pk_zahl(float wert, char *out, size_t len);

/* ------------------------------------------------------------------ */
/* Kopf                                                                */
/* ------------------------------------------------------------------ */

/* Pruefwert und Zahl der Spalten. Zwei Zustaende mit denselben Schluesseln in
 * derselben Reihenfolge ergeben denselben Pruefwert. */
typedef struct {
    uint32_t hash;
    uint16_t spalten;
} pk_kopf_t;

void pk_kopf_beginnen(pk_kopf_t *k);
void pk_kopf_schluessel(pk_kopf_t *k, const char *schluessel);
/* Pruefwert einer Kopfzeile, wie sie in der Datei steht ("zeit,a,b"), fuer
 * den Vergleich nach einem Neustart. Liefert false, wenn sie nicht mit
 * "zeit" beginnt. */
bool pk_kopf_aus_zeile(const char *zeile, pk_kopf_t *k);

/* ------------------------------------------------------------------ */
/* Reihe eines Geraets                                                 */
/* ------------------------------------------------------------------ */

#define PK_SCHALTER_MAX 8

typedef struct {
    pk_kopf_t kopf;        /* Spalten, fuer die die Felder angelegt sind */
    uint16_t kapazitaet;
    float *letzt;          /* letzter Wert je Spalte, NAN ohne */
    float *summe;
    uint8_t *anzahl;
    uint32_t platz;        /* laufender Platz der Mittel, 0 = keiner */
    uint32_t zuletzt;      /* Zeit der letzten Abtastung */
    /* Seit wann ein Schalter (Brenner, Pumpe) steht, fuer die Dauer im
     * Ereignis; Spalte und Zeitpunkt */
    struct {
        uint16_t spalte;
        uint32_t seit;
    } schalter[PK_SCHALTER_MAX];
    uint8_t schalter_n;
} pk_reihe_t;

void pk_reihe_init(pk_reihe_t *r);
void pk_reihe_frei(pk_reihe_t *r);

/* Legt die Felder fuer einen neuen Kopf an. Laufende Mittel gehen dabei
 * verloren; der Aufrufer schreibt sie vorher mit pk_mittel_zeile. Liefert
 * false, wenn der Speicher nicht reicht. */
bool pk_reihe_spalten(pk_reihe_t *r, const pk_kopf_t *k);

/* Ist ein Fuenfminutenmittel abzuschliessen, bevor `epoch` aufgenommen wird?
 * Auch bei einem Ruecksprung der Uhr. */
bool pk_mittel_faellig(const pk_reihe_t *r, uint32_t epoch);

/* Zeile der Mittel des laufenden Platzes, Zeitstempel am Beginn des Platzes;
 * setzt die Summen zurueck. Liefert false, wenn nichts vorliegt oder der
 * Puffer nicht reicht. */
bool pk_mittel_zeile(pk_reihe_t *r, char *buf, size_t len);

/* Eine Zeile im Takt der Abtastung. Zwischen beginnen und ende kommt fuer
 * jede Spalte des Kopfs genau ein Wert, in der Reihenfolge des Kopfs. */
typedef struct {
    pk_reihe_t *r;
    char *buf;
    size_t len, pos;
    uint16_t i;
    bool voll;
} pk_zeile_t;

void pk_zeile_beginnen(pk_zeile_t *z, pk_reihe_t *r, uint32_t epoch, char *buf, size_t len);
void pk_zeile_wert(pk_zeile_t *z, float wert, pk_art_t art);
/* Schliesst mit Zeilenende ab. false, wenn der Puffer nicht reichte oder die
 * Zahl der Werte nicht zum Kopf passt; die Zeile ist dann zu verwerfen. */
bool pk_zeile_ende(pk_zeile_t *z);

/* Dieselben Werte wie zuletzt, fuer einen unveraenderten Zustand (304). */
bool pk_zeile_wiederholen(pk_reihe_t *r, uint32_t epoch, char *buf, size_t len);

/* ------------------------------------------------------------------ */
/* Wechsel                                                             */
/* ------------------------------------------------------------------ */

typedef enum {
    PK_W_KEINER = 0,
    PK_W_SCHALTER,    /* brenner, kkp, pumpe.<id> ein oder aus */
    PK_W_SOLLWERT,    /* raum.<n>.soll */
    PK_W_BETRIEBSART, /* raum.<n>.betrieb */
} pk_wechsel_t;

/* Ob der Uebergang von `alt` nach `neu` in Spalte `schluessel` ein Ereignis
 * ist. Ohne alten Wert (NAN) nie: Nach einem Neustart des Leitstands ist
 * unbekannt, was vorher war. */
pk_wechsel_t pk_wechsel(const char *schluessel, float alt, float neu);

/* Merkt den Zeitpunkt eines Schalterwechsels und liefert die Dauer des
 * vorigen Zustands in Sekunden, 0 wenn unbekannt. */
uint32_t pk_schalter_dauer(pk_reihe_t *r, uint16_t spalte, uint32_t epoch);

/* Eine Zeile fuer ereignisse.jsonl. `felder` sind weitere Paare als JSON ohne
 * Klammern ("\"dauer_s\":120") oder NULL. Liefert die Laenge oder 0. */
size_t pk_ereignis(char *buf, size_t len, uint32_t epoch, const char *geraet, const char *art,
                   const char *felder);

/* ------------------------------------------------------------------ */
/* Lesen                                                               */
/* ------------------------------------------------------------------ */

/* Umkehrung von pk_iso; nimmt nur genau diese Form an. */
bool pk_iso_lesen(const char *s, uint32_t *epoch);

/* Zu jedem gesuchten Schluessel die Spalte in der Kopfzeile, -1 wenn es ihn
 * dort nicht gibt. Die Zeitspalte zaehlt nicht mit: Spalte 0 ist der erste
 * Schluessel. Liefert false, wenn `kopf` keine Kopfzeile ist. */
bool pk_spalten_waehlen(const char *kopf, const char *const *schluessel, int n, int16_t *spalte);

/* Zeit und die gewaehlten Werte einer Datenzeile; NAN fuer leere Zellen und
 * fehlende Spalten. false fuer eine Zeile ohne gueltige Zeit. */
bool pk_zeile_lesen(const char *zeile, const int16_t *spalte, int n, uint32_t *zeit, float *werte);

/* Zeitpunkt aus einer Zeile von ereignisse.jsonl ({"zeit":"...Z",...}) */
bool pk_ereignis_zeit(const char *zeile, uint32_t *epoch);

#ifdef __cplusplus
}
#endif
