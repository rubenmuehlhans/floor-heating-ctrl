idf_component_register(SRCS "config_store.c"
                       INCLUDE_DIRS "include"
                       REQUIRES hw_map nvs_flash espressif__cjson netmgr cfgjson)
