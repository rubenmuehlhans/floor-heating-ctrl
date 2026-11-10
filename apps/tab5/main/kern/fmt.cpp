#include "fmt.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

namespace fmt {

std::string mit_einheit(const std::string &zahl, const char *einheit)
{
    if (!einheit || !*einheit) return zahl;
    return zahl + FEST + einheit;
}

std::string zahl(double wert, int stellen)
{
    if (isnan(wert)) return STRICH;
    char b[32];
    snprintf(b, sizeof b, "%.*f", stellen, wert);
    // „-0,0“ vermeiden
    if (b[0] == '-') {
        bool null = true;
        for (char *p = b + 1; *p; p++)
            if (*p >= '1' && *p <= '9') null = false;
        if (null) memmove(b, b + 1, strlen(b));
    }
    for (char *p = b; *p; p++)
        if (*p == '.') *p = ',';
    return b;
}

std::string temperatur(double wert, int stellen)
{
    if (isnan(wert)) return STRICH;
    return mit_einheit(zahl(wert, stellen), "\xC2\xB0" "C");
}

std::string kelvin(double wert, int stellen, bool vorzeichen)
{
    if (isnan(wert)) return STRICH;
    return mit_einheit((vorzeichen && wert > 0 ? "+" : "") + zahl(wert, stellen), "K");
}

std::string prozentzahl(double anteil)
{
    if (isnan(anteil)) return STRICH;
    return std::to_string((int)lround(anteil * 100));
}

std::string prozent(double anteil)
{
    if (isnan(anteil)) return STRICH;
    return mit_einheit(prozentzahl(anteil), "%");
}

std::string liter(double wert, int stellen) { return isnan(wert) ? STRICH : mit_einheit(zahl(wert, stellen), "l"); }

std::string starts(int anzahl) { return anzahl == 1 ? "1 Start" : std::to_string(anzahl) + " Starts"; }

std::string dauer(int s)
{
    if (s < 0) s = 0;
    if (s < 60) return mit_einheit(std::to_string(s), "s");
    int min = s / 60;
    if (min < 60) return mit_einheit(std::to_string(min), "min");
    int h = min / 60, rest = min % 60;
    if (h >= 24 || rest == 0) return mit_einheit(std::to_string(h), "h");
    return mit_einheit(std::to_string(h), "h") + " " + mit_einheit(std::to_string(rest), "min");
}

std::string alter(int s) { return s < 0 ? std::string(STRICH) : "vor " + dauer(s); }

std::string seit(int s)
{
    if (s < 0) s = 0;
    if (s < 60) return "weniger als einer Minute";
    if (s < 3600) return std::to_string(s / 60) + FEST + "min";
    if (s < 48 * 3600) return std::to_string(s / 3600) + FEST + "h";
    return std::to_string(s / 86400) + " Tagen";
}

std::string signal(int dbm)
{
    if (dbm == 0) return STRICH;
    const char *g = dbm >= -55 ? "sehr gut" : dbm >= -67 ? "gut" : dbm >= -75 ? "ausreichend" : "schwach";
    return mit_einheit(std::to_string(dbm), "dBm") + " \xC2\xB7 " + g;
}

std::string wochentag(int n)
{
    static const char *T[] = {"Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"};
    return n >= 0 && n <= 6 ? T[n] : "kein Termin";
}

// Letzter Sonntag eines Monats um 01:00 UTC (EU-Regel der Sommerzeit).
// Tage seit 1970-01-01 (Howard Hinnant, days_from_civil); timegm fehlt in newlib.
static int64_t tage(int y, int m, int d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static int64_t letzter_sonntag(int jahr, int monat)
{
    int64_t letzter = tage(jahr, monat, 31);   // März und Oktober haben 31 Tage
    int wtag = (int)((letzter + 4) % 7);        // 1970-01-01 war ein Donnerstag
    return (letzter - wtag) * 86400 + 3600;
}

int ortsversatz(int64_t unix)
{
    time_t t = (time_t)unix;
    struct tm u;
    gmtime_r(&t, &u);
    int jahr = u.tm_year + 1900;
    int64_t beginn = letzter_sonntag(jahr, 3), ende = letzter_sonntag(jahr, 10);
    return unix >= beginn && unix < ende ? 7200 : 3600;
}

static bool ortszeit(int64_t unix, struct tm &o)
{
    if (unix < 1600000000) return false;   // Uhr steht noch nicht
    time_t t = (time_t)(unix + ortsversatz(unix));
    gmtime_r(&t, &o);
    return true;
}

std::string uhrzeit(int64_t unix)
{
    struct tm o;
    if (!ortszeit(unix, o)) return "";
    char b[32];
    snprintf(b, sizeof b, "%02d:%02d", o.tm_hour, o.tm_min);
    return b;
}

std::string tag(int64_t unix)
{
    struct tm o;
    if (!ortszeit(unix, o)) return "";
    char b[40];
    snprintf(b, sizeof b, "%02d.%02d.%04d", o.tm_mday, o.tm_mon + 1, o.tm_year + 1900);
    return b;
}

std::string tag_kurz(int64_t unix)
{
    struct tm o;
    if (!ortszeit(unix, o)) return "";
    char b[32];
    snprintf(b, sizeof b, "%02d.%02d.", o.tm_mday, o.tm_mon + 1);
    return b;
}

std::string tag_mit_zeit(int64_t unix) { return tag(unix) + " " + uhrzeit(unix); }

std::string wochentag_kurz(int64_t unix)
{
    static const char *T[] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
    struct tm o;
    if (!ortszeit(unix, o)) return "";
    return T[o.tm_wday];
}

std::string iso(int64_t unix)
{
    struct tm o;
    if (!ortszeit(unix, o)) return "";
    int v = ortsversatz(unix) / 3600;
    char b[80];
    snprintf(b, sizeof b, "%04d-%02d-%02dT%02d:%02d:%02d+%02d:00", o.tm_year + 1900, o.tm_mon + 1, o.tm_mday,
             o.tm_hour, o.tm_min, o.tm_sec, v);
    return b;
}

int64_t aus_iso_utc(const std::string &s)
{
    int y, mo, d, h = 0, mi = 0, se = 0;
    if (sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) < 3) return 0;
    if (mo < 1 || mo > 12 || d < 1 || d > 31) return 0;
    return tage(y, mo, d) * 86400 + h * 3600 + mi * 60 + se;
}

std::string tag_utc(int64_t unix)
{
    time_t t = (time_t)unix;
    struct tm u;
    gmtime_r(&t, &u);
    char b[32];
    snprintf(b, sizeof b, "%04d-%02d-%02d", u.tm_year + 1900, u.tm_mon + 1, u.tm_mday);
    return b;
}

std::string einheiten_fest(const std::string &t)
{
    static const char *EINHEITEN[] = {"\xC2\xB0" "C", "dBm", "l/h", "min", "ms", "mV", "k\xCE\xA9", "K", "%", "l", "h", "s", "V"};
    std::string r;
    r.reserve(t.size() + 16);
    for (size_t i = 0; i < t.size(); i++) {
        char c = t[i];
        if (c == ' ' && i > 0 && ((t[i - 1] >= '0' && t[i - 1] <= '9') || t[i - 1] == ')')) {
            for (const char *e : EINHEITEN) {
                size_t n = strlen(e);
                if (t.compare(i + 1, n, e) != 0) continue;
                // Danach kein Buchstabe und keine Ziffer: „5 Starts“ bleibt, „5 s“ wird geschützt.
                unsigned char nach = i + 1 + n < t.size() ? (unsigned char)t[i + 1 + n] : ' ';
                bool wort = (nach >= 'a' && nach <= 'z') || (nach >= 'A' && nach <= 'Z') || (nach >= '0' && nach <= '9') ||
                            nach >= 0xC3;
                if (!wort) {
                    r += FEST;
                    goto weiter;
                }
            }
        }
        r += c;
    weiter:;
    }
    return r;
}

bool nur_ziffern(const std::string &s)
{
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = (unsigned char)s[i];
        if ((c >= '0' && c <= '9') || strchr(",.:+-% /", c)) continue;
        if (c == 0xC2 && i + 1 < s.size() && ((unsigned char)s[i + 1] == 0xB0 || (unsigned char)s[i + 1] == 0xA0)) {
            i++;
            continue;
        }
        // – und − (E2 80 93, E2 88 92)
        if (c == 0xE2 && i + 2 < s.size()) {
            unsigned char a = (unsigned char)s[i + 1], b = (unsigned char)s[i + 2];
            if ((a == 0x80 && b == 0x93) || (a == 0x88 && b == 0x92)) {
                i += 2;
                continue;
            }
        }
        return false;
    }
    return !s.empty();
}

std::string gross(const std::string &s)
{
    if (s.empty()) return s;
    std::string r = s;
    unsigned char c = (unsigned char)r[0];
    if (c >= 'a' && c <= 'z') r[0] = (char)(c - 32);
    else if (c == 0xC3 && r.size() > 1) {
        unsigned char d = (unsigned char)r[1];
        if (d == 0xA4 || d == 0xB6 || d == 0xBC) r[1] = (char)(d - 0x20);   // ä ö ü
    }
    return r;
}

std::string versal(const std::string &s)
{
    std::string r;
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 'a' && c <= 'z') r += (char)(c - 32);
        else if (c == 0xC3 && i + 1 < s.size()) {
            unsigned char d = (unsigned char)s[i + 1];
            if (d == 0x9F) r += "SS";   // ß
            else {
                r += (char)c;
                r += (char)(d >= 0xA0 && d <= 0xBE ? d - 0x20 : d);
            }
            i++;
        } else r += (char)c;
    }
    return r;
}

}  // namespace fmt
