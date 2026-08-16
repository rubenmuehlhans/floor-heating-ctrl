idf_component_register(SRCS
                           "main.c"
                           "app_sensors.c"
                           "app_web.c"
                       INCLUDE_DIRS "."
                       REQUIRES
                           config_store
                           onewire_temp
                           netmgr
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
