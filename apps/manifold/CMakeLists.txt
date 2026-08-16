cmake_minimum_required(VERSION 3.16)

# Die Komponenten liegen eine Ebene ueber den Anwendungen und werden mit der
# Waermeerzeuger-Firmware unter apps/heatsource geteilt.
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(EXTRA_COMPONENT_DIRS "${REPO_ROOT}/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# __FILE__ soll weiterhin "./main/..." und "./components/..." ergeben. ESP-IDF
# bildet nur die Projektwurzel ab; die liegt seit der Aufteilung unterhalb des
# Repositoriums, sodass die Pfade der gemeinsamen Komponenten sonst absolut im
# Abbild stuenden.
#
# Zwei getrennte Aufrufe: idf_build_set_property nimmt genau einen Wert je
# Aufruf, weitere Angaben fallen still weg. Der Uebersetzer prueft die Regeln
# in umgekehrter Angabereihenfolge, die engere Regel kommt deshalb zuletzt.
idf_build_set_property(COMPILE_OPTIONS "-fmacro-prefix-map=${REPO_ROOT}=." APPEND)
idf_build_set_property(COMPILE_OPTIONS "-fmacro-prefix-map=${CMAKE_CURRENT_LIST_DIR}=." APPEND)

project(floor-heating-ctrl)
