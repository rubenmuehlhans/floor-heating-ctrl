idf_component_register(SRCS "atc_ble.c" "atc_decode.c"
                       INCLUDE_DIRS "include"
                       REQUIRES bt esp_timer)
