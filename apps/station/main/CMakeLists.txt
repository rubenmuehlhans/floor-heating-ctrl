idf_component_register(SRCS
                           "main.c"
                           "st_config.c"
                           "st_poll.c"
                           "st_web.c"
                           "st_log.c"
                           "st_ui.cpp"
                           "st_hap.c"
                           "st_hap_mdns.c"
                           "st_verlauf.c"
                       INCLUDE_DIRS "."
                       REQUIRES
                           netmgr
                           peers
                           atc_ble
                           cfgjson
                           nvs_flash
                           esp_timer
                           esp_http_server
                           esp_http_client
                           esp_driver_uart
                           app_update
                           protokoll
                           fatfs
                           sdmmc
                           esp_driver_sdspi
                           vfs
                           espcoredump
                           esp_hap_core
                           esp_hap_apple_profiles
                           mdns)

# mDNS teilen sich peers und das HomeKit-SDK, siehe st_hap_mdns.c.
# -u holt die Ersatzfunktionen aus dem Archiv, auch wenn peers und das SDK
# erst nach main gebunden werden.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=mdns_init" "-Wl,--wrap=mdns_hostname_set"
                                                 "-Wl,-u,__wrap_mdns_init" "-Wl,-u,__wrap_mdns_hostname_set")

# Die Oberflaeche liegt komprimiert im Programmabbild, wie bei Verteiler und
# Heizungsgeraet. Woraus sie besteht, steht in www/sources.txt.
set(www_liste "${CMAKE_CURRENT_SOURCE_DIR}/www/sources.txt")
file(STRINGS "${www_liste}" www_quellen)
list(FILTER www_quellen EXCLUDE REGEX "^[ \t]*(#|$)")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${www_liste}")

embed_www(TARGET  ${COMPONENT_LIB}
          OUTPUT  index.html.gz
          SOURCES ${www_quellen})
