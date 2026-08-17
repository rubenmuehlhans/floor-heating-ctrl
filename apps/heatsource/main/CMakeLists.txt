idf_component_register(SRCS
                           "main.c"
                           "app_burner.c"
                           "app_log.c"
                           "app_mqtt.c"
                           "app_pumps.c"
                           "app_remote.c"
                           "app_sensors.c"
                           "app_analyse.c"
                           "app_web.c"
                       INCLUDE_DIRS "."
                       REQUIRES
                           config_store
                           onewire_temp
                           heatlogic
                           schedule
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
# Woraus die Oberflaeche besteht, steht in www/sources.txt -- dieselbe Liste
# benutzen die Werkzeuge unter tools/. Zwei Orte waeren ein Ort zu viel.
set(www_liste "${CMAKE_CURRENT_SOURCE_DIR}/www/sources.txt")
file(STRINGS "${www_liste}" www_quellen)
list(FILTER www_quellen EXCLUDE REGEX "^[ \t]*(#|$)")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${www_liste}")

embed_www(TARGET  ${COMPONENT_LIB}
          OUTPUT  index.html.gz
          SOURCES ${www_quellen})
