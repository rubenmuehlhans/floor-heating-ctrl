/*
 * mDNS fuer HomeKit und die eigene Anmeldung gemeinsam.
 *
 * Das HomeKit-SDK startet mDNS beim hap_start() selbst und bricht ab, wenn
 * es schon laeuft; gelingt ihm der Start, setzt es den Rechnernamen fest auf
 * „MyHost". Beim Leitstand startet peers mDNS vorher und meldet den Namen aus
 * den Einstellungen (leitstand.local), unter dem App und Geraete ihn finden.
 *
 * Statt das SDK zu aendern, leitet der Linker beide Aufrufe hierher um
 * (--wrap in CMakeLists.txt): Ein zweiter Start gilt als gelungen, und der
 * feste Name des SDK wird uebergangen. Alle anderen Aufrufe gehen unveraendert
 * an die mDNS-Komponente.
 */
#include <string.h>

#include "esp_err.h"
#include "mdns.h"

esp_err_t __real_mdns_init(void);
esp_err_t __real_mdns_hostname_set(const char *hostname);

esp_err_t __wrap_mdns_init(void)
{
    esp_err_t rc = __real_mdns_init();
    return rc == ESP_ERR_INVALID_STATE ? ESP_OK : rc;
}

esp_err_t __wrap_mdns_hostname_set(const char *hostname)
{
    if (hostname != NULL && strcmp(hostname, "MyHost") == 0) {
        return ESP_OK;
    }
    return __real_mdns_hostname_set(hostname);
}
