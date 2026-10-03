cmake_minimum_required(VERSION 3.16)

# Leitstand: Funkempfang, Protokoll und Anzeige auf einem M5Stack Core,
# spaeter HomeKit auf dem Core2. Siehe docs/konzept-leitstand.md.
#
# Anders als Verteiler und Heizungsgeraet mit ESP-IDF 5.3: Das HomeKit-SDK
# setzt ESP-IDF 5.x voraus, und M5GFX ist mit dieser Fassung erprobt.
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
# Das HomeKit-SDK von Espressif liegt unveraendert unter third_party.
set(EXTRA_COMPONENT_DIRS "${REPO_ROOT}/components"
                         "${REPO_ROOT}/third_party/esp-homekit-sdk/components/homekit")

# Bauteile der Regelgeraete werden hier nicht gebraucht. Ausschliessen haelt
# auch fremde Abhaengigkeiten aus dependencies.lock heraus, siehe
# apps/heatsource/CMakeLists.txt.
set(EXCLUDE_COMPONENTS hw_map sr74hc595 bemf valve roomctrl ssd1327 sensors_local
                       i2cbus config_store heatlogic schedule onewire_temp mqttc)

list(APPEND CMAKE_MODULE_PATH "${REPO_ROOT}/cmake")
include(embed_www)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# __FILE__ relativ halten wie in den anderen Anwendungen.
idf_build_set_property(COMPILE_OPTIONS "-fmacro-prefix-map=${REPO_ROOT}=." APPEND)
idf_build_set_property(COMPILE_OPTIONS "-fmacro-prefix-map=${CMAKE_CURRENT_LIST_DIR}=." APPEND)

project(station)
