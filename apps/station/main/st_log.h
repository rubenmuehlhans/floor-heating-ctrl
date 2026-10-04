/*
 * Protokoll auf der SD-Karte, siehe docs/konzept-leitstand.md, Abschnitt
 * Protokoll, und docs/katalog-messgroessen.md.
 *
 * Je Tag, gezaehlt in UTC, ein Verzeichnis /sd/protokoll/<jahr>/<tag>/ mit
 *
 *   geraete.json        Kennung, Ort, Art und Version je Geraet
 *   <kennung>.csv       Messwerte im Takt der Abfrage
 *   <kennung>.5min.csv  Mittelwerte je fuenf Minuten
 *   ereignisse.jsonl    Ereignisse aller Geraete
 *   zustaende.jsonl     vollstaendige Zustaende, alle 15 Minuten
 *
 * Geschrieben wird jede Zeile sofort: Datei oeffnen, anhaengen, schliessen.
 * Das Konzept sieht vor, eine Minute zu sammeln; auf dem Core Basic ohne
 * PSRAM ist dafuer kein Platz. Ein Stromausfall kostet so hoechstens die
 * Zeile, die gerade geschrieben wird.
 *
 * Ohne gueltige Uhrzeit wird nichts geschrieben; die Zahl der verworfenen
 * Abfragen steht im Ereignis, sobald die Zeit gilt.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ST_LOG_WURZEL "/sd/protokoll"

/*
 * Laengste Zeile einer Messwertdatei, Kopf eingeschlossen, mit Zeilenende und
 * Nullbyte. Schreiber und Leser halten sich beide daran: Liest ein Leser mit
 * kuerzerem Puffer, zerfaellt eine lange Kopfzeile in zwei, und keine Spalte
 * passt mehr zu ihrem Namen.
 */
#define ST_LOG_ZEILE_MAX 1536

/* Haengt die Karte ein und startet die eigene Aufgabe: Zeile des Leitstands,
 * Platzverwaltung, erneuter Versuch, falls die Karte fehlt. Nach der Anzeige
 * aufzurufen, weil die Karte am Bus der Anzeige haengt. */
esp_err_t st_log_start(void);

/* Ein abgefragter Zustand, aus dem Abfrageauftrag. `bedarf` ist die
 * Bedarfsantwort eines Verteilers oder NULL, `text` der Zustand, wie er kam. */
void st_log_zustand(const char *kennung, const char *ort, bool heizgeraet, const cJSON *zustand,
                    const cJSON *bedarf, const char *text, int text_len);

/* Der Verteiler meldet einen unveraenderten Zustand (304): dieselben Werte. */
void st_log_unveraendert(const char *kennung);

/* Ein Geraet ist nicht mehr oder wieder erreichbar; bei Wiederkehr mit der
 * Dauer der Luecke. */
void st_log_erreichbar(const char *kennung, bool erreichbar, uint32_t dauer_s);

/*
 * Ladungs- und Tagesprotokoll eines Heizungsgeraets, einmal am Tag komplett
 * uebernommen nach protokolle/<kennung>.<art>.csv im Verzeichnis des Tages.
 * `faellig` sagt, ob es heute noch aussteht; `lesen` liefert den Inhalt
 * stueckweise und meldet mit *n == 0 das Ende, mit false einen Fehler.
 */
bool st_log_protokolle_faellig(const char *kennung);
typedef bool (*st_log_lesen_t)(void *ctx, char *buf, int max, int *n);
bool st_log_protokoll(const char *kennung, const char *art, st_log_lesen_t lesen, void *ctx);
void st_log_protokolle_erledigt(const char *kennung);

/* Pfad einer Datei im Verzeichnis eines Tages, ohne etwas anzulegen */
bool st_log_datei(uint32_t zeit, const char *name, char *out, size_t len);

/*
 * Formatiert die ganze Karte als eine FAT32-Partition, im Auftrag der
 * Oberflaeche. Laeuft in der Aufgabe des Protokolls; der Aufruf kehrt sofort
 * zurueck. Alles auf der Karte geht verloren.
 */
void st_log_formatieren(void);

/*
 * Dauerlastpruefung (Konzept, Etappe 2): schreibt `mb` Megabyte mit einem
 * bekannten Muster nach /sd/lasttest.bin, waehrend Anzeige und Protokoll
 * weiterlaufen, liest sie zurueck und vergleicht. Die Datei wird danach
 * geloescht. Liefert false, wenn keine Karte da ist oder schon eine Pruefung
 * laeuft.
 */
bool st_log_lasttest_starten(uint32_t mb);

typedef struct {
    bool laeuft;
    const char *phase;     /* "schreiben", "lesen", "fertig", "abgebrochen" */
    uint32_t ziel_mb;
    uint32_t geschrieben_mb;
    uint32_t gelesen_mb;
    uint32_t schreibfehler;
    uint32_t abweichungen;  /* Bloecke, deren Inhalt nicht dem Muster entspricht */
    uint32_t schreiben_s, lesen_s;
    uint32_t heap_min;      /* Tiefstwert waehrend der Pruefung */
} st_log_lasttest_t;

void st_log_lasttest_stand(st_log_lasttest_t *out);

typedef struct {
    bool karte;           /* eingehaengt und beschreibbar */
    uint32_t groesse_mb;
    uint32_t frei_mb;
    uint32_t heute_byte;  /* seit dem Start geschrieben, zu Mitternacht (UTC) auf null */
    uint32_t zuletzt;     /* Zeitpunkt der letzten Schreibung */
    uint32_t verworfen;   /* Abfragen vor der ersten gueltigen Zeit */
    uint32_t fehler;      /* Schreibfehler seit dem Start */
    char meldung[48];     /* letzter Fehler, leer ohne */
} st_log_status_t;

void st_log_status(st_log_status_t *out);

/* Pruefung eines Pfads unter ST_LOG_WURZEL fuer den Abruf ueber HTTP:
 * "2026-09-23/heiz_2370ec.csv" -> "/sd/protokoll/2026/2026-09-23/...".
 * Liefert false bei allem, was kein Tag mit Dateinamen ist. */
bool st_log_pfad(const char *tag_und_datei, char *out, size_t len);

/* Ereignis "leitstand" mit "was" und weiteren Feldern, etwa beim Ausfall der
 * Versorgung. `weitere` ist JSON ohne Klammern und darf leer sein. */
void st_log_leitstand(const char *was, const char *weitere);

#ifdef __cplusplus
}
#endif
