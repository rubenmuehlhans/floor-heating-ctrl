#include "bootmenu.h"

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"

namespace bootmenu {

static const esp_partition_t *menu()
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, "bootmenu");
}

bool vorhanden() { return menu() != nullptr; }

void beim_start()
{
    const esp_partition_t *m = menu();
    if (!m) return;
    esp_err_t e = esp_ota_set_boot_partition(m);
    ESP_LOGI("bootmenu", "Startpartition: Menü (%s)", esp_err_to_name(e));
}

void neustart() { esp_restart(); }

}  // namespace bootmenu
