idf_component_register(SRCS
                           "main.c"
                           "app_burner.c"
                           "app_mqtt.c"
                           "app_pumps.c"
                           "app_remote.c"
                           "app_sensors.c"
                           "app_web.c"
                       INCLUDE_DIRS "."
                       REQUIRES
                           config_store
                           onewire_temp
                           heatlogic
                           mqttc
                           netmgr
                           esp_http_client
                           peers
                           esp_timer
                           esp_http_server
                           esp_https_ota
                           app_update)

# Die Oberflaeche wird komprimiert ins Programmabbild gelegt: sie soll ohne
# Internetzugang und ohne zusaetzliche Partition laden.
embed_www(TARGET  ${COMPONENT_LIB}
          OUTPUT  index.html.gz
          SOURCES www/index.html)
