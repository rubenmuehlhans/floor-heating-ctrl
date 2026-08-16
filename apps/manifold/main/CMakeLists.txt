idf_component_register(SRCS
                           "main.c"
                           "app_control.c"
                           "app_calib.c"
                           "app_web.c"
                           "app_mqtt.c"
                           "app_ui.c"
                       INCLUDE_DIRS "."
                       REQUIRES
                           hw_map
                           config_store
                           sr74hc595
                           valve
                           bemf
                           roomctrl
                           netmgr
                           i2cbus
                           peers
                           atc_ble
                           ssd1327
                           sensors_local
                           esp_driver_touch_sens
                           esp_timer
                           esp_http_server
                           esp_https_ota
                           espressif__mqtt
                           app_update)

# Die Oberflaeche wird komprimiert ins Programmabbild gelegt: sie soll ohne
# Internetzugang und ohne zusaetzliche Partition laden.
embed_www(TARGET  ${COMPONENT_LIB}
          OUTPUT  index.html.gz
          SOURCES www/index.html)
