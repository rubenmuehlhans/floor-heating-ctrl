#include "akkustand.h"

#include <math.h>

namespace akkustand {

namespace {

// Leerlaufspannung einer Li-Ion-Zelle (NMC) über dem Ladestand — gängige
// Kennlinie, keine Messung an diesem Akku. Zwei Zellen in Reihe.
struct Punkt {
    float v, p;
};
constexpr Punkt KURVE[] = {
    {3.27f, 0},  {3.61f, 5},  {3.69f, 10}, {3.71f, 15}, {3.73f, 20}, {3.75f, 25}, {3.77f, 30},
    {3.79f, 35}, {3.80f, 40}, {3.82f, 45}, {3.84f, 50}, {3.85f, 55}, {3.87f, 60}, {3.91f, 65},
    {3.95f, 70}, {3.98f, 75}, {4.02f, 80}, {4.08f, 85}, {4.11f, 90}, {4.15f, 95}, {4.20f, 100},
};
constexpr int N = sizeof KURVE / sizeof KURVE[0];

// Glättung des Stroms: bei einer Messung je Sekunde ≈ 60 s Zeitkonstante. Kürzer
// sprünge die Restzeit mit jedem Bildaufbau, länger hinkte sie nach einem
// Lastwechsel (Anzeige an/aus) minutenlang hinterher.
constexpr float STROM_ZK_S = 60.0f;
// Ladestand aus der Spannung ebenfalls träge nachführen — die Spannung zittert
// unter wechselnder Last um einige Millivolt, und 10 mV sind hier schnell 2 %.
constexpr float SOC_ZK_S = 120.0f;
// „voll“: Ladegerät in der Konstantspannungsphase, Strom fast weg.
constexpr float VOLL_V = 8.30f, VOLL_A = 0.10f;

}  // namespace

float prozent_aus_spannung(float pack_v)
{
    float z = pack_v / 2.0f;
    if (z <= KURVE[0].v) return 0;
    if (z >= KURVE[N - 1].v) return 100;
    for (int i = 1; i < N; i++)
        if (z <= KURVE[i].v) {
            float t = (z - KURVE[i - 1].v) / (KURVE[i].v - KURVE[i - 1].v);
            return KURVE[i - 1].p + t * (KURVE[i].p - KURVE[i - 1].p);
        }
    return 100;
}

float leerlauf(float pack_v, float strom)
{
    return strom > 0 ? pack_v + strom * INNENWIDERSTAND_OHM : pack_v;
}

void Rechner::schritt(float pack_v, float strom, float dt_s)
{
    float a = dt_s / STROM_ZK_S;
    if (a > 1) a = 1;
    strom_mittel += a * (strom - strom_mittel);

    bool laedt = strom < -RUHE_A;
    if (laedt) {
        // Mitzählen: −strom ist der Ladestrom. Ohne Ruhewert bleibt es unbekannt.
        if (soc >= 0) {
            soc += -strom * dt_s / 3600.0f / KAPAZITAET_AH * 100.0f;
            if (soc > 99) soc = 99;   // 100 erst, wenn das Ladegerät abregelt
        }
        voll = false;
        return;
    }
    // Ruhe oder Entladen: aus der Spannung, um den Lastabfall berichtigt.
    float ziel = prozent_aus_spannung(leerlauf(pack_v, strom));
    if (soc < 0) {
        soc = ziel;
    } else {
        float b = dt_s / SOC_ZK_S;
        if (b > 1) b = 1;
        float neu = soc + b * (ziel - soc);
        // Beim Entladen nie steigen: eine Anzeige, die unter Last hochspringt, liest
        // niemand als Messwert.
        soc = strom > RUHE_A && neu > soc ? soc : neu;
    }
    // Ladegerät eingeschaltet, Strom weg, Spannung oben: voll.
    voll = fabsf(strom) < VOLL_A && pack_v >= VOLL_V;
    if (voll) soc = 100;
}

int Rechner::restzeit_min() const
{
    if (soc < 0 || strom_mittel <= RUHE_A) return -1;
    float ah = soc / 100.0f * KAPAZITAET_AH;
    return (int)lroundf(ah / strom_mittel * 60.0f);
}

int Rechner::bis_voll_min() const
{
    if (soc < 0 || strom_mittel >= -RUHE_A) return -1;
    float ah = (100.0f - soc) / 100.0f * KAPAZITAET_AH;
    // ⚠️ Grob: in der Konstantspannungsphase sinkt der Strom, die letzten Prozent
    // dauern länger als gerechnet.
    return (int)lroundf(ah / -strom_mittel * 60.0f);
}

}  // namespace akkustand
