/*
 * AES-128 und CCM-Entschluesselung fuer verschluesselte BTHome-Rundrufe.
 *
 * Bewusst in reinem C wie die Dekodierung: Dieselben Zeilen laufen auf dem
 * Geraet und in den Pruefungen auf dem Rechner, auch dort, wo keine
 * Kryptobibliothek installiert ist. Geprueft werden sie gegen den Pruefwert
 * aus FIPS-197 und gegen echte Rahmen eines Satelliten (test/host).
 *
 * Nur die Verschluesselungsrichtung von AES wird gebraucht: CCM rechnet den
 * Zaehlermodus und die Pruefsumme beide mit ihr.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Laengster Inhalt, den atc_ccm_decrypt annimmt; ein BTHome-Rundruf traegt
 * hoechstens gut zwanzig Byte. */
#define ATC_CCM_MAX 64

/* Rundenschluessel aus dem 16-Byte-Schluessel, 11 x 16 Byte. */
void atc_aes128_expand(const uint8_t key[16], uint8_t rk[176]);

/* Ein Block, mit den Rundenschluesseln aus atc_aes128_expand. */
void atc_aes128_encrypt(const uint8_t rk[176], const uint8_t in[16], uint8_t out[16]);

/*
 * AES-128-CCM nach RFC 3610 mit 13 Byte Nonce, 4 Byte Pruefsumme und ohne
 * zusaetzliche Daten -- genau die Form von BTHome v2. Schreibt `len` Byte
 * Klartext nach `out` und liefert false, wenn die Pruefsumme nicht passt
 * (falscher Schluessel, beschaedigt oder gefaelscht); `out` ist dann geleert.
 */
bool atc_ccm_decrypt(const uint8_t key[16], const uint8_t nonce[13], const uint8_t *ct,
                     size_t len, const uint8_t mic[4], uint8_t *out);

#ifdef __cplusplus
}
#endif
