idf_component_register(SRCS "netmgr.c"
                       INCLUDE_DIRS "include"
                       REQUIRES captive_dns esp_wifi esp_netif esp_event lwip)
