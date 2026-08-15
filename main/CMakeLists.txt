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
idf_build_get_property(python PYTHON)
set(www_src "${CMAKE_CURRENT_SOURCE_DIR}/www/index.html")
set(www_gz "${CMAKE_CURRENT_BINARY_DIR}/index.html.gz")

add_custom_command(
    OUTPUT "${www_gz}"
    COMMAND "${python}" -c
            "import gzip,shutil,sys; shutil.copyfileobj(open(sys.argv[1],'rb'), gzip.open(sys.argv[2],'wb',9))"
            "${www_src}" "${www_gz}"
    DEPENDS "${www_src}"
    COMMENT "Oberflaeche komprimieren"
    VERBATIM)
add_custom_target(www_gzip DEPENDS "${www_gz}")
add_dependencies(${COMPONENT_LIB} www_gzip)

target_add_binary_data(${COMPONENT_LIB} "${www_gz}" BINARY)
