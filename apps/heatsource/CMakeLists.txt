cmake_minimum_required(VERSION 3.16)

# Die Komponenten liegen eine Ebene ueber den Anwendungen und werden mit der
# Verteiler-Firmware unter apps/manifold geteilt. Die Fassung von config_store
# unter apps/heatsource/components verdraengt dabei die gemeinsame: sie traegt
# das Schema dieses Geraets statt der elf Ventilkanaele des Verteilers.
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(EXTRA_COMPONENT_DIRS "${REPO_ROOT}/components")

# Bauteile des Verteilers werden hier nicht gebraucht. Ausschliessen spart
# nicht nur Uebersetzungszeit: der Komponentenverwalter loest alle gefundenen
# Manifeste auf, bevor der Abhaengigkeitsbaum beschnitten wird, und schriebe
# sonst fremde Abhaengigkeiten in dependencies.lock.
set(EXCLUDE_COMPONENTS hw_map sr74hc595 bemf valve roomctrl atc_ble ssd1327
                       sensors_local i2cbus)

# Gemeinsame Bauregeln, siehe cmake/embed_www.cmake.
list(APPEND CMAKE_MODULE_PATH "${REPO_ROOT}/cmake")
include(embed_www)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# __FILE__ soll "./main/..." und "./components/..." ergeben statt absoluter
# Pfade. idf_build_set_property nimmt genau einen Wert je Aufruf; der
# Uebersetzer prueft die Regeln in umgekehrter Angabereihenfolge, die engere
# steht deshalb zuletzt.
idf_build_set_property(COMPILE_OPTIONS "-fmacro-prefix-map=${REPO_ROOT}=." APPEND)
idf_build_set_property(COMPILE_OPTIONS "-fmacro-prefix-map=${CMAKE_CURRENT_LIST_DIR}=." APPEND)

project(heat-source-ctrl)
